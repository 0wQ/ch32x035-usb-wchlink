#include "bsp/bsp_delay.h"
#include "wchlink/rvswd/rvswd_debug.h"
#include "wchlink/rvswd/rvswd_memory.h"
#include "wchlink/rvswd/rvswd_operation.h"
#include "wchlink/rvswd/rvswd_types.h"
#include "wchlink/target/rvswd_target_module.h"
#include "wchlink/target/rvswd_target_registry.h"
#include "wchlink/target/wchlink_target_control.h"
#include "wchlink/target/wchlink_target_ports_internal.h"
#include "wchlink/transport/rvswd_transport.h"

#include <stddef.h>

// 连接实现封装唤醒、Debug Module 解锁、halt 和身份探测
// 所有 helper 保持文件私有，调用者只观察 target ports 的连接结果
static const uint32_t rvswd_debug_unlock = 0x5aa50400u;
static const uint8_t rvswd_target_connect_short_probe_count = 20u;
static const uint8_t rvswd_target_connect_long_probe_cycle_count = 4u;
static const uint32_t rvswd_target_connect_probe_interval_us = 1000u;

static void rvswd_target_connect_reset_identity(
    struct wchlink_target_ports *ports) {
    ports->info.chip_id = 0u;
    ports->family_hint_active = false;
}

// 候选 module 自己完成身份访问，连接编排只校验 ChipID 并锁定结果
static bool rvswd_target_connect_identify_module(
    struct wchlink_target_ports *ports, struct rvswd_operation *operation,
    const struct rvswd_target_module *module, bool allow_protected_hint) {
    const struct rvswd_target_profile *profile;
    const struct rvswd_target_identity_profile *identity;
    uint32_t chip_id = 0u;
    uint32_t option_status;

    if (module == NULL || module->probe == NULL ||
        module->capabilities == NULL ||
        module->capabilities->packet_mode !=
            rvswd_transport_packet_mode(operation->transport) ||
        module->probe->read_chip_id == NULL) {
        return false;
    }
    if (module->probe->read_chip_id(operation, &chip_id)) {
        if (chip_id == 0u || module->matches_chip_id == NULL ||
            !module->matches_chip_id(chip_id)) {
            return false;
        }
        ports->module = module;
        ports->info.chip_id = chip_id;
        profile = module->profile;
        rvswd_transport_set_fast_timing(
            operation->transport, profile != NULL && profile->fast_timing);
        return true;
    }

    if (!allow_protected_hint) {
        return false;
    }
    profile = module->profile;
    identity = profile == NULL ? NULL : profile->identity;
    if (identity == NULL || identity->option_status_address == 0u ||
        module->memory == NULL || module->memory->read32 == NULL ||
        !module->memory->read32(operation, identity->option_status_address,
                                &option_status) ||
        (option_status & identity->option_status_read_protected_mask) == 0u) {
        return false;
    }
    // 受保护目标无法读出 ChipID 时仍锁定主机指定的候选 module
    ports->module = module;
    ports->family_hint_active = true;
    return true;
}

static bool rvswd_target_connect_identify(
    struct wchlink_target_ports *ports, struct rvswd_operation *operation) {
    const struct rvswd_target_module *hint = ports->module;
    const enum rvswd_packet_mode packet_mode =
        rvswd_transport_packet_mode(operation->transport);

    if (hint != NULL && hint->probe != NULL && hint->capabilities != NULL &&
        hint->capabilities->packet_mode == packet_mode &&
        hint->probe->read_chip_id != NULL &&
        rvswd_target_connect_identify_module(ports, operation, hint, true)) {
        return true;
    }
    for (size_t index = 0u;
         index < rvswd_target_registry_module_count(); ++index) {
        const struct rvswd_target_module *module =
            rvswd_target_registry_module_at(index);

        if (module != hint && module->probe != NULL &&
            module->capabilities != NULL &&
            module->capabilities->packet_mode == packet_mode &&
            rvswd_target_connect_identify_module(ports, operation, module,
                                                 false)) {
            return true;
        }
    }
    return false;
}

static bool rvswd_target_connect_known_mode(
    struct wchlink_target_ports *ports, struct rvswd_operation *operation) {
    struct rvswd_transport *transport = &ports->transport;

    ports->connect_error = 0u;
    rvswd_target_connect_reset_identity(ports);
    if (rvswd_transport_packet_mode(transport) == RVSWD_PACKET_LONG) {
        // CH58X 的 V4A 调试模块连接前需要先清空两线接口的唤醒状态
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
        if (!rvswd_debug_restore_unlock(operation)) {
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
        // OpenOCD 未预先提供 CH58X family 时，补一次 long frame 探测
        rvswd_transport_set_packet_mode(transport, RVSWD_PACKET_LONG);
        return rvswd_target_connect_known_mode(ports, operation);
    }
    return false;
}

static bool rvswd_target_connect_short_autodetect(
    struct wchlink_target_ports *ports, struct rvswd_operation *operation) {
    ports->connect_error = 0u;
    rvswd_target_connect_reset_identity(ports);

    for (uint8_t attempt = 0u;
         attempt < rvswd_target_connect_short_probe_count; ++attempt) {
        uint32_t dmstatus;
        uint64_t halt_start;
        bool halted = false;
        struct rvswd_transport_result read_result;

        // 官方抓包中最后一个 long STOP 到首个 short START 约为 212 us
        bsp_delay_us(600u);
        // BUSY 是当前管线结果，三笔不同地址请求必须连续推进，不能在 transport 内重发
        (void)rvswd_transport_probe_short(
            &ports->transport, false, RVSWD_DMI_WCH_SHADOW,
            rvswd_debug_unlock);
        (void)rvswd_transport_probe_short(
            &ports->transport, false, RVSWD_DMI_WCH_CONFIG,
            rvswd_debug_unlock);
        read_result = rvswd_transport_probe_short(
            &ports->transport, true, RVSWD_DMI_STATUS, 0u);
        if (!read_result.ok) {
            ports->connect_error = 0xa3u;
            if (attempt + 1u < rvswd_target_connect_short_probe_count) {
                bsp_delay_us(rvswd_target_connect_probe_interval_us);
            }
            continue;
        }
        dmstatus = read_result.value;

        // 官方 LinkE 连续写入两次 haltreq，随后轮询 allhalted 再访问 ChipID 和目标内存
        if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_CONTROL, 0x80000001u).ok) {
            ports->connect_error = 0xa4u;
            continue;
        }
        if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_CONTROL, 0x80000001u).ok) {
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
    if (ports->module == NULL) {
        // wlink status 使用通用 RISC-V 值 1，未知 family 按官方 LinkE 序列自动识别
        // CH58X 冷启动时 202 个探测帧只返回 BUSY，不能用成功签名决定是否尝试 long mode
        for (uint8_t cycle = 0u;
             cycle < rvswd_target_connect_long_probe_cycle_count; ++cycle) {
            rvswd_transport_set_packet_mode(transport, RVSWD_PACKET_LONG);
            // 官方 LinkE 的 202 个长帧使用相同的主机请求，首帧也必须保持写操作奇偶校验
            (void)rvswd_transport_probe_long(
                transport, 1u, RVSWD_DMI_STATUS, 0u, 1u);
            for (uint16_t probe = 0u; probe < 201u; ++probe) {
                (void)rvswd_transport_probe_long(
                    transport, 1u, RVSWD_DMI_STATUS, 0u, 1u);
            }
            rvswd_transport_set_packet_mode(transport, RVSWD_PACKET_SHORT);
            if (rvswd_target_connect_short_autodetect(ports, operation)) {
                return true;
            }
            // 官方无目标抓包在每组短帧后约等待 1 ms，再开始下一组 long 探测
            if (cycle + 1u < rvswd_target_connect_long_probe_cycle_count) {
                bsp_delay_us(rvswd_target_connect_probe_interval_us);
            }
        }
        // 自动探测失败后仍保留已知 long-mode 目标的连接路径
        rvswd_transport_set_packet_mode(transport, RVSWD_PACKET_LONG);
        return rvswd_target_connect_known_mode(ports, operation);
    }
    return rvswd_target_connect_known_mode(ports, operation);
}

struct rvswd_target_result wchlink_target_ports_connect(
    struct wchlink_target_ports *ports) {
    struct rvswd_operation operation;

    if (ports == NULL) {
        return rvswd_target_result_failure(RVSWD_TARGET_RESULT_CONNECT, 0x01u, false);
    }
    rvswd_operation_init(&operation, &ports->transport);
    if (rvswd_target_connect_transport(ports, &operation)) {
        const struct rvswd_target_module *module = ports->module;
        ports->connect_error = 0u;
        ports->info.family = module == NULL ? 0u : module->family;
        ports->info.connected = module != NULL;
        wchlink_target_ports_refresh_info(ports);
        // 主机 SetSpeed 请求优先于 profile 默认时序，连接探测后恢复请求速度
        if (ports->requested_speed != 0u) {
            rvswd_transport_set_fast_timing(&ports->transport, ports->requested_speed != 0x03u);
        }
        return rvswd_target_result_success();
    }

    ports->info.connected = false;
    wchlink_target_ports_refresh_info(ports);
    return rvswd_target_result_failure(RVSWD_TARGET_RESULT_CONNECT, ports->connect_error, true);
}
