#include "bsp/bsp_delay.h"
#include "wchlink/rvswd/rvswd_debug.h"
#include "wchlink/rvswd/rvswd_memory.h"
#include "wchlink/rvswd/rvswd_operation.h"
#include "wchlink/rvswd/rvswd_types.h"
#include "wchlink/target/rvswd_target_profile.h"
#include "wchlink/target/wchlink_target_control.h"
#include "wchlink/target/wchlink_target_ports_internal.h"
#include "wchlink/transport/rvswd_transport.h"

#include <stddef.h>

// 连接实现封装唤醒、Debug Module 解锁、halt 和身份探测
// 所有 helper 保持文件私有，调用者只观察 target ports 的连接结果
static const uint32_t rvswd_debug_unlock = 0x5aa50400u;
static const uint8_t rvswd_long_status_ok = 0u;

static const uint32_t rvswd_ch5xx_chip_id_address = 0x40001041u;
static const uint8_t rvswd_ch5xx_chip_id_ch591 = 0x91u;
static const uint8_t rvswd_ch5xx_chip_id_ch592 = 0x92u;
static const uint8_t rvswd_ch5xx_chip_id_ch582 = 0x82u;
static const uint8_t rvswd_ch5xx_chip_id_ch583 = 0x83u;

static const uint32_t rvswd_ch5xx_debug_data_address = 0xe0000380u;
static const uint32_t rvswd_flash_obr_address = 0x4002201cu;
static const uint32_t rvswd_flash_obr_read_protected = 1u << 1u;

static const struct rvswd_target_profile *rvswd_target_connect_memory_profile(
    const struct wchlink_target_ports *ports) {
    const struct rvswd_target_profile *profile = rvswd_target_profile_resolve(
        ports->info.chip_id, ports->family_hint,
        ports->family_hint_active);

    if (profile != NULL) {
        return profile;
    }
    return rvswd_target_profile_from_family(ports->family_hint);
}

static bool rvswd_target_connect_read_memory32(
    struct wchlink_target_ports *ports, struct rvswd_operation *operation,
    uint32_t address, uint32_t *value) {
    return rvswd_memory_read32(
        operation, rvswd_target_connect_memory_profile(ports),
        ports->info.chip_id != 0u, address, value);
}

static void rvswd_target_connect_reset_identity(
    struct wchlink_target_ports *ports) {
    ports->info.chip_id = 0u;
    ports->family_hint_active = false;
}

static bool rvswd_target_connect_restore_debug_module(
    struct rvswd_operation *operation) {
    bool success = true;

    // QingKe 调试模块启动和异常恢复时会丢失关键写入，重复配置确保命令生效
    success = rvswd_operation_write_dmi(operation, RVSWD_DMI_WCH_SHADOW,
                                        rvswd_debug_unlock)
                  .ok &&
              success;
    success = rvswd_operation_write_dmi(operation, RVSWD_DMI_WCH_CONFIG,
                                        rvswd_debug_unlock)
                  .ok &&
              success;
    success = rvswd_operation_write_dmi(operation, RVSWD_DMI_WCH_SHADOW,
                                        rvswd_debug_unlock)
                  .ok &&
              success;
    success = rvswd_operation_write_dmi(operation, RVSWD_DMI_WCH_CONFIG,
                                        rvswd_debug_unlock)
                  .ok &&
              success;
    success = rvswd_operation_write_dmi(operation, RVSWD_DMI_CONTROL,
                                        0x80000001u)
                  .ok &&
              success;
    success = rvswd_operation_write_dmi(operation, RVSWD_DMI_CONTROL,
                                        0x80000001u)
                  .ok &&
              success;
    success = rvswd_operation_write_dmi(operation, RVSWD_DMI_CONTROL,
                                        0x80000001u)
                  .ok &&
              success;
    return success;
}

static bool rvswd_target_connect_read_memory8_ch5xx(
    struct rvswd_operation *operation, uint32_t address,
    uint8_t *value) {
    uint32_t abstractcs;
    struct rvswd_transport_result read_result;

    // LinkE 通过 data0 传入地址，Program Buffer 将目标字节写回 data1
    if (value == NULL ||
        !rvswd_debug_write_raw_gpr(operation, 13u,
                                   rvswd_ch5xx_debug_data_address) ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_ABSTRACTCS, 0x00000700u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_PROGBUF0, 0x00058483u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_PROGBUF1, 0x00968223u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_PROGBUF2, 0x00100073u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_DATA0, address).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_COMMAND, 0x0027100bu).ok ||
        !rvswd_debug_wait_abstract_idle(operation, &abstractcs) ||
        ((abstractcs >> 8u) & 0x07u) != 0u) {
        return false;
    }
    read_result = rvswd_operation_read_dmi(operation, RVSWD_DMI_DATA1);
    if (!read_result.ok) {
        return false;
    }

    *value = (uint8_t)read_result.value;
    return true;
}

static bool rvswd_target_connect_identify(
    struct wchlink_target_ports *ports, struct rvswd_operation *operation) {
    const struct rvswd_target_profile *expected_profile =
        rvswd_target_profile_from_family(ports->family_hint);
    const struct rvswd_target_profile *direct_profile;
    uint32_t direct_chip_id;
    uint32_t memory_chip_id = 0u;
    uint32_t option_status;
    uint8_t ch5xx_chip_id;
    struct rvswd_transport *transport = operation->transport;
    struct rvswd_transport_result read_result;

    // V30X 的官方 LinkE 直接从 DMI 0x7f 返回 ChipID，避免先执行抽象内存命令
    read_result = rvswd_operation_read_dmi(operation, RVSWD_DMI_WCH_CHIP_ID);
    direct_chip_id = read_result.value;
    if (read_result.ok && direct_chip_id != 0u) {
        direct_profile = rvswd_target_profile_from_chip_id(direct_chip_id);
        if (direct_profile != NULL && !direct_profile->ch5xx_protocol) {
            ports->info.chip_id = direct_chip_id;
            rvswd_transport_set_fast_timing(transport,
                                            direct_profile->fast_timing);
            return true;
        }
    }

    // CH5xx 通过专用 8 位寄存器报告型号，失败后才进入通用内存 ChipID 路径
    if (rvswd_target_connect_read_memory8_ch5xx(
            operation, rvswd_ch5xx_chip_id_address, &ch5xx_chip_id) &&
        (ch5xx_chip_id == rvswd_ch5xx_chip_id_ch582 ||
         ch5xx_chip_id == rvswd_ch5xx_chip_id_ch583 ||
         ch5xx_chip_id == rvswd_ch5xx_chip_id_ch591 ||
         ch5xx_chip_id == rvswd_ch5xx_chip_id_ch592)) {
        // 协议层使用 family 高字节形式，Flash 命令口在实际擦除流程中单独解锁
        ports->info.chip_id = (uint32_t)ch5xx_chip_id << 24u;
        return true;
    }
    if (rvswd_target_connect_read_memory32(ports, operation, 0x1ffff704u,
                                           &memory_chip_id) &&
        memory_chip_id != 0u) {
        ports->info.chip_id = memory_chip_id;
        direct_profile = rvswd_target_profile_from_chip_id(memory_chip_id);
        rvswd_transport_set_fast_timing(
            transport, direct_profile != NULL && direct_profile->fast_timing);
        return true;
    }
    if (expected_profile != NULL &&
        rvswd_target_connect_read_memory32(
            ports, operation, rvswd_flash_obr_address, &option_status) &&
        (option_status & rvswd_flash_obr_read_protected) != 0u) {
        // ChipID 读取失败时，受限会话才使用主机 SetSpeed 提示的 profile
        ports->family_hint_active = true;
        return true;
    }
    return false;
}

static bool rvswd_target_connect_known_mode(
    struct wchlink_target_ports *ports, struct rvswd_operation *operation) {
    struct rvswd_transport *transport = &ports->transport;

    ports->connect_error = 0u;
    rvswd_target_connect_reset_identity(ports);
    if (rvswd_transport_packet_mode(transport) == RVSWD_PACKET_LONG) {
        // CH58x 的 V4A 调试模块连接前需要先清空两线接口的唤醒状态
        rvswd_transport_wakeup(transport, true);
        rvswd_transport_wakeup(transport, false);
    }

    for (uint8_t attempt = 0u; attempt < 3u; ++attempt) {
        uint32_t config = 0u;
        uint32_t dmstatus = 0u;
        bool config_read;
        struct rvswd_transport_result read_result;

        // 目标上电或复位后需要留出调试模块启动时间
        bsp_delay_ms(16u);

        // 初始化阶段按 DTM 管线推进请求，单次 BUSY 不重复占用同一请求
        if (!rvswd_target_connect_restore_debug_module(operation)) {
            ports->connect_error = 0x12u;
            continue;
        }
        // 只有目标核进入 Debug Mode 后，Program Buffer 和 abstract command 才可执行
        if (!rvswd_debug_halt(operation)) {
            ports->connect_error = 0x14u;
            continue;
        }
        read_result = rvswd_operation_read_dmi(operation, RVSWD_DMI_WCH_CONFIG);
        config_read = read_result.ok;
        config = read_result.value;
        if (config_read && (config & 0xffff0000u) == 0x5aa50000u) {
            if (rvswd_target_connect_identify(ports, operation)) {
                return true;
            }
            ports->connect_error = 0x13u;
        }

        // 失败诊断继续读取 DMSTATUS，区分严格签名不匹配和链路不可用
        read_result = rvswd_operation_read_dmi(operation, RVSWD_DMI_STATUS);
        dmstatus = read_result.value;
        if (read_result.ok) {
            uint8_t version = (uint8_t)(dmstatus & 0x0fu);

            // 当前支持的 QingKe V4 目标仅接受 Debug 0.13.2
            if (version == 2u) {
                if (rvswd_target_connect_identify(ports, operation)) {
                    return true;
                }
                ports->connect_error = 0x13u;
                continue;
            }
            ports->connect_error = 0x20u | version;
        } else {
            ports->connect_error = config_read ? 0x11u : 0x12u;
        }
    }
    if (rvswd_transport_packet_mode(transport) == RVSWD_PACKET_SHORT) {
        // OpenOCD 可能未预先提供 CH58x family，补一次 long frame 探测
        rvswd_transport_set_packet_mode(transport, RVSWD_PACKET_LONG);
        return rvswd_target_connect_known_mode(ports, operation);
    }
    return false;
}

static bool rvswd_target_connect_short_autodetect(
    struct wchlink_target_ports *ports, struct rvswd_operation *operation) {
    ports->connect_error = 0u;
    rvswd_target_connect_reset_identity(ports);

    for (uint8_t attempt = 0u; attempt < 3u; ++attempt) {
        uint32_t dmstatus;
        uint64_t halt_start;
        bool halted = false;
        struct rvswd_transport_result read_result;

        // 官方 V307 抓包中最后一个 long STOP 到首个 short START 约为 212 us
        bsp_delay_us(200u);
        // 初始化帧的即时状态属于 DMI 管线，继续发送官方序列并以最终 halt 状态验收
        if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_WCH_SHADOW,
                                       rvswd_debug_unlock)
                 .ok) {
            ports->connect_error = 0xa1u;
            continue;
        }
        if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_WCH_CONFIG,
                                       rvswd_debug_unlock)
                 .ok) {
            ports->connect_error = 0xa2u;
            continue;
        }
        read_result = rvswd_operation_read_dmi(operation, RVSWD_DMI_STATUS);
        if (!read_result.ok) {
            ports->connect_error = 0xa3u;
            continue;
        }
        dmstatus = read_result.value;

        // 官方 LinkE 连续写入两次 haltreq，随后轮询 allhalted 再访问 ChipID 和目标内存
        if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_CONTROL,
                                       0x80000001u)
                 .ok) {
            ports->connect_error = 0xa4u;
            continue;
        }
        if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_CONTROL,
                                       0x80000001u)
                 .ok) {
            ports->connect_error = 0xa5u;
            continue;
        }
        halt_start = bsp_time_us();
        do {
            read_result = rvswd_operation_read_dmi(operation, RVSWD_DMI_STATUS);
            if (!read_result.ok) {
                ports->connect_error = 0xa6u;
                break;
            }
            dmstatus = read_result.value;
            if ((dmstatus & (1u << 9u)) != 0u) {
                halted = true;
                break;
            }
            bsp_delay_us(100u);
        } while ((bsp_time_us() - halt_start) < 100000u);
        if (!halted) {
            if (ports->connect_error != 0xa6u) {
                ports->connect_error = 0xa7u;
            }
            continue;
        }
        if (rvswd_target_connect_identify(ports, operation)) {
            return true;
        }
        ports->connect_error = 0x13u;
    }
    return false;
}

static bool rvswd_target_connect_transport(
    struct wchlink_target_ports *ports, struct rvswd_operation *operation) {
    struct rvswd_transport *transport = &ports->transport;

    rvswd_transport_set_fast_timing(transport, false);
    if (rvswd_target_profile_from_family(ports->family_hint) == NULL) {
        struct rvswd_transport_probe_result probe_result;
        uint16_t long_signature_count = 0u;

        // wlink status 使用通用 RISC-V 值 1，未知 family 先按官方 LinkE 序列自动识别
        // 自动识别先发送一个探测帧和 201 个轮询帧，再回到 short frame 连接 CH32
        rvswd_transport_set_packet_mode(transport, RVSWD_PACKET_LONG);
        // V30X 的长帧把 host parity 设为 operation 的奇偶校验位，CH5xx 长帧则固定为 0
        (void)rvswd_transport_probe_long(transport, 0u, RVSWD_DMI_STATUS, 0x19u,
                                         0u);
        for (uint16_t probe = 0u; probe < 201u; ++probe) {
            probe_result =
                rvswd_transport_probe_long(transport, 1u, RVSWD_DMI_STATUS, 0u,
                                           1u);
            if (probe_result.address == RVSWD_DMI_STATUS &&
                probe_result.status == rvswd_long_status_ok &&
                (probe_result.value & 0x0fu) == 2u) {
                ++long_signature_count;
            }
        }
        rvswd_transport_set_packet_mode(transport, RVSWD_PACKET_SHORT);
        if (rvswd_target_connect_short_autodetect(ports, operation)) {
            return true;
        }
        if (long_signature_count >= 8u) {
            uint8_t short_error = ports->connect_error;

            rvswd_transport_set_packet_mode(transport, RVSWD_PACKET_LONG);
            if (rvswd_target_connect_known_mode(ports, operation)) {
                return true;
            }
            ports->connect_error = short_error;
        }
        return false;
    }
    return rvswd_target_connect_known_mode(ports, operation);
}

static uint8_t rvswd_target_connect_family(
    const struct wchlink_target_ports *ports) {
    const struct rvswd_target_profile *profile = rvswd_target_profile_resolve(
        ports->info.chip_id, ports->family_hint,
        ports->family_hint_active);

    if (profile != NULL) {
        return profile->wchlink_family;
    }
    profile = rvswd_target_profile_from_family(ports->family_hint);
    return profile == NULL ? 0u : profile->wchlink_family;
}

struct rvswd_target_result wchlink_target_ports_connect(
    struct wchlink_target_ports *ports) {
    struct rvswd_operation operation;

    if (ports == NULL) {
        return rvswd_target_result_failure(RVSWD_TARGET_RESULT_CONNECT, 0x01u,
                                           false);
    }
    rvswd_operation_init(&operation, &ports->transport);
    if (rvswd_target_connect_transport(ports, &operation)) {
        ports->connect_error = 0u;
        ports->info.family = rvswd_target_connect_family(ports);
        ports->profile = rvswd_target_profile_resolve(
            ports->info.chip_id, ports->family_hint,
            ports->family_hint_active);
        ports->info.connected = ports->profile != NULL;
        wchlink_target_ports_refresh_info(ports);
        return rvswd_target_result_success();
    }

    ports->info.connected = false;
    wchlink_target_ports_refresh_info(ports);
    return rvswd_target_result_failure(RVSWD_TARGET_RESULT_CONNECT,
                                       ports->connect_error, true);
}
