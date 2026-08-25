#include "rvswd_gpio.h"

#include "bsp/bsp_delay.h"
#include "rvswd_frame.h"
#include "rvswd_dmi.h"
#include "rvswd_debug.h"
#include "rvswd_memory.h"
#include "rvswd_reset.h"
#include "rvswd_phy_gpio.h"
#include "rvswd_target.h"
#include "rvswd_types.h"

#include <stddef.h>

#include <ch32x035.h>

#define RVSWD_DMI_CONTROL 0x10u
#define RVSWD_DMI_CONFIG  0x7du
#define RVSWD_DMI_SHADOW  0x7eu
#define RVSWD_DMI_CHIP_ID 0x7fu
#define RVSWD_DEBUG_UNLOCK              0x5aa50400u
#define RVSWD_LONG_STATUS_OK 0u

#define RVSWD_CH5XX_CHIP_ID_ADDRESS 0x40001041u
#define RVSWD_CH5XX_CHIP_ID_CH591   0x91u
#define RVSWD_CH5XX_CHIP_ID_CH592   0x92u
#define RVSWD_CH5XX_CHIP_ID_CH582   0x82u
#define RVSWD_CH5XX_CHIP_ID_CH583   0x83u

#define RVSWD_CH5XX_DEBUG_DATA_ADDRESS     0xe0000380u
#define RVSWD_FLASH_OBR_ADDRESS      0x4002201cu
#define RVSWD_FLASH_OBR_READ_PROTECTED  (1u << 1u)

static uint8_t rvswd_connect_last_error;

extern const uint8_t ch5xx_flash_erase_stub_start[];
extern const uint8_t ch5xx_flash_erase_stub_end[];


void rvswd_gpio_init(void) {
    rvswd_dmi_reset();
    rvswd_phy_gpio_init();
}

void rvswd_gpio_disconnect(void) {
    rvswd_phy_gpio_disconnect();
}

static bool rvswd_gpio_restore_debug_module(void) {
    bool success = true;

    // QingKe 调试模块启动和异常恢复时会丢失关键写入，重复配置确保命令生效
    success = rvswd_gpio_write_dmi(RVSWD_DMI_SHADOW, RVSWD_DEBUG_UNLOCK) && success;
    success = rvswd_gpio_write_dmi(RVSWD_DMI_CONFIG, RVSWD_DEBUG_UNLOCK) && success;
    success = rvswd_gpio_write_dmi(RVSWD_DMI_SHADOW, RVSWD_DEBUG_UNLOCK) && success;
    success = rvswd_gpio_write_dmi(RVSWD_DMI_CONFIG, RVSWD_DEBUG_UNLOCK) && success;
    success = rvswd_gpio_write_dmi(RVSWD_DMI_CONTROL, 0x80000001u) && success;
    success = rvswd_gpio_write_dmi(RVSWD_DMI_CONTROL, 0x80000001u) && success;
    success = rvswd_gpio_write_dmi(RVSWD_DMI_CONTROL, 0x80000001u) && success;
    return success;
}

bool rvswd_gpio_write_dmi(uint8_t address, uint32_t value) {
    return rvswd_dmi_write(address, value);
}

bool rvswd_gpio_read_memory32(uint32_t address, uint32_t *value) {
    return rvswd_memory_read32(address, value);
}

bool rvswd_gpio_write_memory32(uint32_t address, uint32_t value) {
    return rvswd_memory_write32(address, value);
}

bool rvswd_gpio_write_memory(uint32_t address, const uint8_t *data, uint32_t length) {
    return rvswd_memory_write(address, data, length);
}

uint8_t rvswd_gpio_memory_last_error(void) {
    return rvswd_memory_last_error();
}

uint8_t rvswd_gpio_memory_failure_dmi_status(void) {
    return rvswd_memory_failure_dmi_status();
}

uint32_t rvswd_gpio_memory_failure_address(void) {
    return rvswd_memory_failure_address();
}

uint32_t rvswd_gpio_memory_failure_abstractcs(void) {
    return rvswd_memory_failure_abstractcs();
}

bool rvswd_gpio_reset_and_halt(void) {
    return rvswd_reset_and_halt();
}

bool rvswd_gpio_soft_reset_and_run(void) {
    return rvswd_soft_reset_and_run();
}

bool rvswd_gpio_reset_and_run(void) {
    return rvswd_reset_and_run();
}

static bool rvswd_gpio_read_memory8_ch5xx(uint32_t address, uint8_t *value) {
    uint32_t abstractcs;
    uint32_t data;

    // LinkE 通过 data0 传入地址，Program Buffer 将目标字节写回 data1
    if (value == NULL ||
        !rvswd_debug_write_raw_gpr(13u, RVSWD_CH5XX_DEBUG_DATA_ADDRESS) ||
        !rvswd_gpio_write_dmi(0x16u, 0x00000700u) ||
        !rvswd_gpio_write_dmi(0x20u, 0x00058483u) ||
        !rvswd_gpio_write_dmi(0x21u, 0x00968223u) ||
        !rvswd_gpio_write_dmi(0x22u, 0x00100073u) ||
        !rvswd_gpio_write_dmi(0x04u, address) ||
        !rvswd_gpio_write_dmi(0x17u, 0x0027100bu) ||
        !rvswd_debug_wait_abstract_idle(&abstractcs) ||
        ((abstractcs >> 8u) & 0x07u) != 0u ||
        !rvswd_gpio_read_dmi(0x05u, &data)) {
        return false;
    }

    *value = (uint8_t)data;
    return true;
}

bool rvswd_gpio_write_register(uint16_t regno, uint32_t value) {
    return rvswd_debug_write_register(regno, value);
}

bool rvswd_gpio_read_register(uint16_t regno, uint32_t *value) {
    return rvswd_debug_read_register(regno, value);
}

bool rvswd_gpio_halt(void) {
    return rvswd_debug_halt();
}

bool rvswd_gpio_execute(uint32_t entry, uint32_t stack_top, uint32_t mode,
                        uint32_t address, uint32_t length, uint32_t data_address,
                        uint32_t *result) {
    return rvswd_debug_execute(entry, stack_top, mode, address, length,
                               data_address, result);
}

bool rvswd_gpio_read_dmi(uint8_t address, uint32_t *value) {
    return rvswd_dmi_read(address, value);
}

bool rvswd_gpio_dmi_failure_retryable(void) {
    return rvswd_dmi_failure_retryable();
}

static bool rvswd_gpio_identify_target(void) {
    const struct rvswd_target_profile *expected_profile =
        rvswd_target_profile_from_family(rvswd_target_family_hint());
    const struct rvswd_target_profile *direct_profile;
    uint32_t direct_chip_id;
    uint32_t option_status;
    uint8_t ch5xx_chip_id;

    // V30X 的官方 LinkE 直接从 DMI 0x7f 返回 ChipID，避免先执行抽象内存命令
    if (rvswd_gpio_read_dmi(RVSWD_DMI_CHIP_ID, &direct_chip_id) &&
        direct_chip_id != 0u) {
        direct_profile = rvswd_target_profile_from_chip_id(direct_chip_id);
        if (direct_profile != NULL && !direct_profile->ch5xx_protocol) {
            rvswd_target_set_chip_id(direct_chip_id);
            rvswd_phy_gpio_set_fast_timing(direct_profile->fast_timing);
            return true;
        }
    }

    if (rvswd_gpio_read_memory8_ch5xx(RVSWD_CH5XX_CHIP_ID_ADDRESS,
                                      &ch5xx_chip_id) &&
        (ch5xx_chip_id == RVSWD_CH5XX_CHIP_ID_CH582 ||
         ch5xx_chip_id == RVSWD_CH5XX_CHIP_ID_CH583 ||
         ch5xx_chip_id == RVSWD_CH5XX_CHIP_ID_CH591 ||
         ch5xx_chip_id == RVSWD_CH5XX_CHIP_ID_CH592)) {
        // CH5xx 通过专用 8 位寄存器报告型号，协议层使用 family 高字节形式
        rvswd_target_set_chip_id((uint32_t)ch5xx_chip_id << 24u);
        // 连接阶段只确认目标身份，Flash 命令口在实际擦除流程中单独解锁
        return true;
    }
    uint32_t memory_chip_id = 0u;
    if (rvswd_gpio_read_memory32(0x1ffff704u, &memory_chip_id) &&
        memory_chip_id != 0u) {
        rvswd_target_set_chip_id(memory_chip_id);
        direct_profile = rvswd_target_profile_from_chip_id(memory_chip_id);
        rvswd_phy_gpio_set_fast_timing(direct_profile != NULL &&
                                       direct_profile->fast_timing);
        return true;
    }
    if (expected_profile != NULL &&
        rvswd_gpio_read_memory32(RVSWD_FLASH_OBR_ADDRESS, &option_status) &&
        (option_status & RVSWD_FLASH_OBR_READ_PROTECTED) != 0u) {
        // ChipID 读取失败时，受限会话使用主机 SetSpeed 提示的 profile
        rvswd_target_set_family_hint_active(true);
        return true;
    }
    return false;
}

static bool rvswd_try_connect(void) {
    rvswd_connect_last_error = 0u;
    rvswd_target_reset();
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

        // 通过当前目标族的 DMI 帧解锁调试模块并读回配置签名
        // 初始化阶段按 DTM 管线推进请求，单次 BUSY 不重复占用同一请求
        if (!rvswd_gpio_restore_debug_module()) {
            rvswd_connect_last_error = 0x12u;
            continue;
        }
        // 只有目标核进入 Debug Mode 后，Program Buffer 和 abstract command 才可执行
        if (!rvswd_gpio_halt()) {
            rvswd_connect_last_error = 0x14u;
            continue;
        }
        config_read = rvswd_gpio_read_dmi(RVSWD_DMI_CONFIG, &config);
        if (config_read && (config & 0xffff0000u) == 0x5aa50000u) {
            if (rvswd_gpio_identify_target()) {
                return true;
            }
            rvswd_connect_last_error = 0x13u;
        }

        // 失败诊断继续读取 DMSTATUS，区分严格签名不匹配和链路不可用
        if (rvswd_gpio_read_dmi(0x11u, &dmstatus)) {
            uint8_t version = (uint8_t)(dmstatus & 0x0fu);

            // 当前支持的 QingKe V4 目标仅接受 Debug 0.13.2
            if (version == 2u) {
                if (rvswd_gpio_identify_target()) {
                    return true;
                }
                rvswd_connect_last_error = 0x13u;
                continue;
            }
            rvswd_connect_last_error = 0x20u | version;
        } else {
            rvswd_connect_last_error = config_read ? 0x11u : 0x12u;
        }
    }
    if (rvswd_dmi_packet_mode() == RVSWD_PACKET_SHORT) {
        // OpenOCD 可能未预先提供 CH58x family，补一次 long frame 探测
        rvswd_dmi_set_packet_mode(RVSWD_PACKET_LONG);
        return rvswd_try_connect();
    }
    return false;
}

static bool rvswd_try_connect_short_autodetect(void) {
    rvswd_connect_last_error = 0u;
    rvswd_target_reset();

    for (uint8_t attempt = 0u; attempt < 3u; ++attempt) {
        uint32_t dmstatus;
        uint64_t halt_start;
        bool halted = false;

        // 官方 V307 抓包中最后一个 long STOP 到首个 short START 约为 212 us
        bsp_delay_us(200u);
        // 初始化帧的即时状态属于 DMI 管线，继续发送官方序列并以最终 halt 状态验收
        if (!rvswd_gpio_write_dmi(RVSWD_DMI_SHADOW, RVSWD_DEBUG_UNLOCK)) {
            rvswd_connect_last_error = 0xa1u;
            continue;
        }
        if (!rvswd_gpio_write_dmi(RVSWD_DMI_CONFIG, RVSWD_DEBUG_UNLOCK)) {
            rvswd_connect_last_error = 0xa2u;
            continue;
        }
        if (!rvswd_gpio_read_dmi(0x11u, &dmstatus)) {
            rvswd_connect_last_error = 0xa3u;
            continue;
        }

        // 官方 LinkE 连续写入两次 haltreq，随后轮询 allhalted 再访问 ChipID 和目标内存
        if (!rvswd_gpio_write_dmi(RVSWD_DMI_CONTROL, 0x80000001u)) {
            rvswd_connect_last_error = 0xa4u;
            continue;
        }
        if (!rvswd_gpio_write_dmi(RVSWD_DMI_CONTROL, 0x80000001u)) {
            rvswd_connect_last_error = 0xa5u;
            continue;
        }
        halt_start = bsp_time_us();
        do {
            if (!rvswd_gpio_read_dmi(0x11u, &dmstatus)) {
                rvswd_connect_last_error = 0xa6u;
                break;
            }
            if ((dmstatus & (1u << 9u)) != 0u) {
                halted = true;
                break;
            }
            bsp_delay_us(100u);
        } while ((bsp_time_us() - halt_start) < 100000u);
        if (!halted) {
            if (rvswd_connect_last_error != 0xa6u) {
                rvswd_connect_last_error = 0xa7u;
            }
            continue;
        }
        if (rvswd_gpio_identify_target()) {
            return true;
        }
        rvswd_connect_last_error = 0x13u;
    }
    return false;
}

bool rvswd_gpio_connect(void) {
    rvswd_phy_gpio_set_fast_timing(false);
    if (rvswd_target_profile_from_family(rvswd_target_family_hint()) == NULL) {
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
            if (target_address == 0x11u && status == RVSWD_LONG_STATUS_OK &&
                (target_data & 0x0fu) == 2u) {
                ++long_signature_count;
            }
        }
        rvswd_dmi_set_packet_mode(RVSWD_PACKET_SHORT);
        if (rvswd_try_connect_short_autodetect()) {
            return true;
        }
        if (long_signature_count >= 8u) {
            uint8_t short_error = rvswd_connect_last_error;

            rvswd_dmi_set_packet_mode(RVSWD_PACKET_LONG);
            if (rvswd_try_connect()) {
                return true;
            }
            rvswd_connect_last_error = short_error;
        }
        return false;
    }
    return rvswd_try_connect();
}

uint8_t rvswd_gpio_connect_last_error(void) {
    return rvswd_connect_last_error;
}

uint32_t rvswd_gpio_target_chip_id(void) {
    return rvswd_target_chip_id();
}

void rvswd_gpio_set_target_wchlink_family_hint(uint8_t family) {
    rvswd_target_set_family_hint(family);
    rvswd_target_set_family_hint_active(false);
    rvswd_dmi_set_packet_mode(family == WCHLINK_TARGET_FAMILY_CH58X
                                   ? RVSWD_PACKET_LONG
                                   : RVSWD_PACKET_SHORT);
}

uint8_t rvswd_gpio_target_wchlink_family(void) {
    const struct rvswd_target_profile *profile = rvswd_target_profile_current();

    if (profile != NULL) {
        return profile->wchlink_family;
    }
    profile = rvswd_target_profile_from_family(rvswd_target_family_hint());
    return profile == NULL ? 0u : profile->wchlink_family;
}

bool rvswd_gpio_target_supports_memory_streaming(void) {
    const struct rvswd_target_profile *profile = rvswd_target_profile_current();

    return profile != NULL &&
           profile->memory_write_mode == RVSWD_MEMORY_WRITE_STREAMING;
}
