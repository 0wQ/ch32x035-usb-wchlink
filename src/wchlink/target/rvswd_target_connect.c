#include "bsp/bsp_delay.h"
#include "rvswd_debug.h"
#include "rvswd_dmi.h"
#include "rvswd_memory.h"
#include "rvswd_phy_gpio.h"
#include "rvswd_target_profile.h"
#include "rvswd_target_session.h"
#include "rvswd_types.h"

#include <stddef.h>

// 连接实现封装唤醒、Debug Module 解锁、halt 和身份探测
// 所有 helper 保持文件私有，调用者只观察 target session 的连接结果
static const uint8_t rvswd_dmi_control = 0x10u;
static const uint8_t rvswd_dmi_config = 0x7du;
static const uint8_t rvswd_dmi_shadow = 0x7eu;
static const uint8_t rvswd_dmi_chip_id = 0x7fu;
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
    const struct rvswd_target_session *session) {
    const struct rvswd_target_profile *profile = rvswd_target_profile_resolve(
        session->info.chip_id, session->family_hint,
        session->family_hint_active);

    if (profile != NULL) {
        return profile;
    }
    return rvswd_target_profile_from_family(session->family_hint);
}

static bool rvswd_target_connect_read_memory32(
    struct rvswd_target_session *session, uint32_t address, uint32_t *value) {
    return rvswd_memory_read32(rvswd_target_connect_memory_profile(session),
                               session->info.chip_id != 0u, address, value);
}

static void rvswd_target_connect_reset_identity(
    struct rvswd_target_session *session) {
    session->info.chip_id = 0u;
    session->family_hint_active = false;
}

static bool rvswd_target_connect_restore_debug_module(void) {
    bool success = true;

    // QingKe 调试模块启动和异常恢复时会丢失关键写入，重复配置确保命令生效
    success = rvswd_dmi_write(rvswd_dmi_shadow, rvswd_debug_unlock) && success;
    success = rvswd_dmi_write(rvswd_dmi_config, rvswd_debug_unlock) && success;
    success = rvswd_dmi_write(rvswd_dmi_shadow, rvswd_debug_unlock) && success;
    success = rvswd_dmi_write(rvswd_dmi_config, rvswd_debug_unlock) && success;
    success = rvswd_dmi_write(rvswd_dmi_control, 0x80000001u) && success;
    success = rvswd_dmi_write(rvswd_dmi_control, 0x80000001u) && success;
    success = rvswd_dmi_write(rvswd_dmi_control, 0x80000001u) && success;
    return success;
}

static bool rvswd_target_connect_read_memory8_ch5xx(uint32_t address,
                                                    uint8_t *value) {
    uint32_t abstractcs;
    uint32_t data;

    // LinkE 通过 data0 传入地址，Program Buffer 将目标字节写回 data1
    if (value == NULL ||
        !rvswd_debug_write_raw_gpr(13u, rvswd_ch5xx_debug_data_address) ||
        !rvswd_dmi_write(0x16u, 0x00000700u) ||
        !rvswd_dmi_write(0x20u, 0x00058483u) ||
        !rvswd_dmi_write(0x21u, 0x00968223u) ||
        !rvswd_dmi_write(0x22u, 0x00100073u) ||
        !rvswd_dmi_write(0x04u, address) ||
        !rvswd_dmi_write(0x17u, 0x0027100bu) ||
        !rvswd_debug_wait_abstract_idle(&abstractcs) ||
        ((abstractcs >> 8u) & 0x07u) != 0u ||
        !rvswd_dmi_read(0x05u, &data)) {
        return false;
    }

    *value = (uint8_t)data;
    return true;
}

static bool rvswd_target_connect_identify(
    struct rvswd_target_session *session) {
    const struct rvswd_target_profile *expected_profile =
        rvswd_target_profile_from_family(session->family_hint);
    const struct rvswd_target_profile *direct_profile;
    uint32_t direct_chip_id;
    uint32_t memory_chip_id = 0u;
    uint32_t option_status;
    uint8_t ch5xx_chip_id;

    // V30X 的官方 LinkE 直接从 DMI 0x7f 返回 ChipID，避免先执行抽象内存命令
    if (rvswd_dmi_read(rvswd_dmi_chip_id, &direct_chip_id) &&
        direct_chip_id != 0u) {
        direct_profile = rvswd_target_profile_from_chip_id(direct_chip_id);
        if (direct_profile != NULL && !direct_profile->ch5xx_protocol) {
            session->info.chip_id = direct_chip_id;
            rvswd_phy_gpio_set_fast_timing(direct_profile->fast_timing);
            return true;
        }
    }

    // CH5xx 通过专用 8 位寄存器报告型号，失败后才进入通用内存 ChipID 路径
    if (rvswd_target_connect_read_memory8_ch5xx(
            rvswd_ch5xx_chip_id_address, &ch5xx_chip_id) &&
        (ch5xx_chip_id == rvswd_ch5xx_chip_id_ch582 ||
         ch5xx_chip_id == rvswd_ch5xx_chip_id_ch583 ||
         ch5xx_chip_id == rvswd_ch5xx_chip_id_ch591 ||
         ch5xx_chip_id == rvswd_ch5xx_chip_id_ch592)) {
        // 协议层使用 family 高字节形式，Flash 命令口在实际擦除流程中单独解锁
        session->info.chip_id = (uint32_t)ch5xx_chip_id << 24u;
        return true;
    }
    if (rvswd_target_connect_read_memory32(session, 0x1ffff704u,
                                           &memory_chip_id) &&
        memory_chip_id != 0u) {
        session->info.chip_id = memory_chip_id;
        direct_profile = rvswd_target_profile_from_chip_id(memory_chip_id);
        rvswd_phy_gpio_set_fast_timing(direct_profile != NULL &&
                                       direct_profile->fast_timing);
        return true;
    }
    if (expected_profile != NULL &&
        rvswd_target_connect_read_memory32(session, rvswd_flash_obr_address,
                                           &option_status) &&
        (option_status & rvswd_flash_obr_read_protected) != 0u) {
        // ChipID 读取失败时，受限会话才使用主机 SetSpeed 提示的 profile
        session->family_hint_active = true;
        return true;
    }
    return false;
}

static bool rvswd_target_connect_known_mode(
    struct rvswd_target_session *session) {
    session->connect_error = 0u;
    rvswd_target_connect_reset_identity(session);
    if (rvswd_dmi_packet_mode() == RVSWD_PACKET_LONG) {
        // CH58x 的 V4A 调试模块连接前需要先清空两线接口的唤醒状态
        rvswd_phy_gpio_wakeup(true);
        rvswd_phy_gpio_wakeup(false);
    }

    for (uint8_t attempt = 0u; attempt < 3u; ++attempt) {
        uint32_t config = 0u;
        uint32_t dmstatus = 0u;
        bool config_read;

        // 目标上电或复位后需要留出调试模块启动时间
        bsp_delay_ms(16u);

        // 初始化阶段按 DTM 管线推进请求，单次 BUSY 不重复占用同一请求
        if (!rvswd_target_connect_restore_debug_module()) {
            session->connect_error = 0x12u;
            continue;
        }
        // 只有目标核进入 Debug Mode 后，Program Buffer 和 abstract command 才可执行
        if (!rvswd_debug_halt()) {
            session->connect_error = 0x14u;
            continue;
        }
        config_read = rvswd_dmi_read(rvswd_dmi_config, &config);
        if (config_read && (config & 0xffff0000u) == 0x5aa50000u) {
            if (rvswd_target_connect_identify(session)) {
                return true;
            }
            session->connect_error = 0x13u;
        }

        // 失败诊断继续读取 DMSTATUS，区分严格签名不匹配和链路不可用
        if (rvswd_dmi_read(0x11u, &dmstatus)) {
            uint8_t version = (uint8_t)(dmstatus & 0x0fu);

            // 当前支持的 QingKe V4 目标仅接受 Debug 0.13.2
            if (version == 2u) {
                if (rvswd_target_connect_identify(session)) {
                    return true;
                }
                session->connect_error = 0x13u;
                continue;
            }
            session->connect_error = 0x20u | version;
        } else {
            session->connect_error = config_read ? 0x11u : 0x12u;
        }
    }
    if (rvswd_dmi_packet_mode() == RVSWD_PACKET_SHORT) {
        // OpenOCD 可能未预先提供 CH58x family，补一次 long frame 探测
        rvswd_dmi_set_packet_mode(RVSWD_PACKET_LONG);
        return rvswd_target_connect_known_mode(session);
    }
    return false;
}

static bool rvswd_target_connect_short_autodetect(
    struct rvswd_target_session *session) {
    session->connect_error = 0u;
    rvswd_target_connect_reset_identity(session);

    for (uint8_t attempt = 0u; attempt < 3u; ++attempt) {
        uint32_t dmstatus;
        uint64_t halt_start;
        bool halted = false;

        // 官方 V307 抓包中最后一个 long STOP 到首个 short START 约为 212 us
        bsp_delay_us(200u);
        // 初始化帧的即时状态属于 DMI 管线，继续发送官方序列并以最终 halt 状态验收
        if (!rvswd_dmi_write(rvswd_dmi_shadow, rvswd_debug_unlock)) {
            session->connect_error = 0xa1u;
            continue;
        }
        if (!rvswd_dmi_write(rvswd_dmi_config, rvswd_debug_unlock)) {
            session->connect_error = 0xa2u;
            continue;
        }
        if (!rvswd_dmi_read(0x11u, &dmstatus)) {
            session->connect_error = 0xa3u;
            continue;
        }

        // 官方 LinkE 连续写入两次 haltreq，随后轮询 allhalted 再访问 ChipID 和目标内存
        if (!rvswd_dmi_write(rvswd_dmi_control, 0x80000001u)) {
            session->connect_error = 0xa4u;
            continue;
        }
        if (!rvswd_dmi_write(rvswd_dmi_control, 0x80000001u)) {
            session->connect_error = 0xa5u;
            continue;
        }
        halt_start = bsp_time_us();
        do {
            if (!rvswd_dmi_read(0x11u, &dmstatus)) {
                session->connect_error = 0xa6u;
                break;
            }
            if ((dmstatus & (1u << 9u)) != 0u) {
                halted = true;
                break;
            }
            bsp_delay_us(100u);
        } while ((bsp_time_us() - halt_start) < 100000u);
        if (!halted) {
            if (session->connect_error != 0xa6u) {
                session->connect_error = 0xa7u;
            }
            continue;
        }
        if (rvswd_target_connect_identify(session)) {
            return true;
        }
        session->connect_error = 0x13u;
    }
    return false;
}

static bool rvswd_target_connect_transport(
    struct rvswd_target_session *session) {
    rvswd_phy_gpio_set_fast_timing(false);
    if (rvswd_target_profile_from_family(session->family_hint) == NULL) {
        uint8_t status;
        uint8_t target_address;
        uint32_t target_data;
        uint16_t long_signature_count = 0u;

        // wlink status 使用通用 RISC-V 值 1，未知 family 先按官方 LinkE 序列自动识别
        // 自动识别先发送一个探测帧和 201 个轮询帧，再回到 short frame 连接 CH32
        rvswd_dmi_set_packet_mode(RVSWD_PACKET_LONG);
        // V30X 的长帧把 host parity 设为 operation 的奇偶校验位，CH5xx 长帧则固定为 0
        (void)rvswd_dmi_transaction_long(0u, 0x11u, 0x19u, 0u,
                                         &target_address, &target_data,
                                         &status);
        for (uint16_t probe = 0u; probe < 201u; ++probe) {
            (void)rvswd_dmi_transaction_long(1u, 0x11u, 0u, 1u,
                                             &target_address, &target_data,
                                             &status);
            if (target_address == 0x11u && status == rvswd_long_status_ok &&
                (target_data & 0x0fu) == 2u) {
                ++long_signature_count;
            }
        }
        rvswd_dmi_set_packet_mode(RVSWD_PACKET_SHORT);
        if (rvswd_target_connect_short_autodetect(session)) {
            return true;
        }
        if (long_signature_count >= 8u) {
            uint8_t short_error = session->connect_error;

            rvswd_dmi_set_packet_mode(RVSWD_PACKET_LONG);
            if (rvswd_target_connect_known_mode(session)) {
                return true;
            }
            session->connect_error = short_error;
        }
        return false;
    }
    return rvswd_target_connect_known_mode(session);
}

static uint8_t rvswd_target_connect_family(
    const struct rvswd_target_session *session) {
    const struct rvswd_target_profile *profile = rvswd_target_profile_resolve(
        session->info.chip_id, session->family_hint,
        session->family_hint_active);

    if (profile != NULL) {
        return profile->wchlink_family;
    }
    profile = rvswd_target_profile_from_family(session->family_hint);
    return profile == NULL ? 0u : profile->wchlink_family;
}

struct rvswd_target_result rvswd_target_session_connect(
    struct rvswd_target_session *session) {
    if (session == NULL) {
        return rvswd_target_result_failure(RVSWD_TARGET_RESULT_CONNECT, 0x01u,
                                           false);
    }
    if (rvswd_target_connect_transport(session)) {
        session->connect_error = 0u;
        session->info.family = rvswd_target_connect_family(session);
        session->info.profile = rvswd_target_profile_resolve(
            session->info.chip_id, session->family_hint,
            session->family_hint_active);
        session->info.connected = session->info.profile != NULL;
        return rvswd_target_result_success();
    }

    session->info.connected = false;
    return rvswd_target_result_failure(RVSWD_TARGET_RESULT_CONNECT,
                                       session->connect_error, true);
}
