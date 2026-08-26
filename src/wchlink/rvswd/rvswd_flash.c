#include "rvswd_flash.h"

#include "bsp/bsp_delay.h"
#include "rvswd_debug.h"
#include "rvswd_dmi.h"
#include "rvswd_memory.h"
#include "rvswd_reset.h"
#include "rvswd_target.h"
#include "rvswd_types.h"

#include <stddef.h>

#include <ch32x035.h>

#define RVSWD_DMI_CONTROL      0x10u
#define RVSWD_DMI_CONFIG       0x7du
#define RVSWD_DMI_SHADOW       0x7eu
#define RVSWD_DMI_CHIP_ID      0x7fu
#define RVSWD_DMI_HARTINFO     0x12u
#define RVSWD_DMI_DATA0        0x04u
#define RVSWD_DMI_DATA1        0x05u
#define RVSWD_DMI_ABSTRACTCS   0x16u
#define RVSWD_DMI_COMMAND      0x17u
#define RVSWD_DMI_ABSTRACTAUTO 0x18u
#define RVSWD_DMI_PROGBUF0     0x20u
#define RVSWD_DMI_PROGBUF1     0x21u
#define RVSWD_DMI_PROGBUF2     0x22u

#define RVSWD_STATUS_OK           1u
#define RVSWD_STATUS_BUSY         3u
#define RVSWD_LONG_STATUS_OK      0u
#define RVSWD_LONG_STATUS_BUSY    3u
#define RVSWD_INTERFRAME_GUARD_US 0u

#define RVSWD_DMI_WRITE_RETRY_COUNT     16u
#define RVSWD_DMI_READ_RETRY_COUNT      64u
#define RVSWD_MEMORY_READ_RETRY_COUNT   3u
#define RVSWD_DMI_BUSY_DELAY_US         100u
#define RVSWD_DMI_ERROR_DELAY_US        50u
#define RVSWD_ABSTRACT_COMMAND_DELAY_US 100u
#define RVSWD_ABSTRACT_TIMEOUT_US       10000u
#define RVSWD_RESUME_MIN_DELAY_US       1000u
#define RVSWD_EXECUTE_TIMEOUT_MS        5000u
#define RVSWD_DEBUG_UNLOCK              0x5aa50400u
#define RVSWD_ABSTRACT_COMMAND_EXECUTE  0x00240000u
#define RVSWD_ABSTRACTAUTO_DATA0        0x00000001u
#define RVSWD_DEBUG_DATA_ADDRESS_BASE   0xe0000000u

#define RVSWD_CH5XX_CHIP_ID_ADDRESS 0x40001041u
#define RVSWD_CH5XX_CHIP_ID_CH591   0x91u
#define RVSWD_CH5XX_CHIP_ID_CH592   0x92u
#define RVSWD_CH5XX_CHIP_ID_CH582   0x82u
#define RVSWD_CH5XX_CHIP_ID_CH583   0x83u

#define RVSWD_CH5XX_FLASH_KEY_ADDRESS       0x40001040u
#define RVSWD_CH5XX_FLASH_WORD_DATA_ADDRESS 0x40001800u
#define RVSWD_CH5XX_FLASH_BYTE_DATA_ADDRESS 0x40001804u
#define RVSWD_CH5XX_FLASH_CONTROL_ADDRESS   0x40001806u
#define RVSWD_CH5XX_DEBUG_DATA_ADDRESS      0xe0000380u
#define RVSWD_CH5XX_FLASH_END               0x00078000u
#define RVSWD_CH5XX_FLASH_PAGE_SIZE         0x00000100u
#define RVSWD_CH5XX_FLASH_BLOCK_4K          0x00001000u
#define RVSWD_CH5XX_FLASH_STATUS_RETRIES    102u
#define RVSWD_CH5XX_PAGE_PROGRAM_TIMEOUT_US 100000u
#define RVSWD_CH5XX_ERASE_STUB_ADDRESS      0x20004000u
#define RVSWD_CH5XX_ERASE_STUB_STACK_TOP    0x20007000u
#define RVSWD_CH5XX_ERASE_STUB_MAX_SIZE     512u

#define RVSWD_FLASH_ERROR_CH5XX_COMMAND_BEGIN          0xc2u
#define RVSWD_FLASH_ERROR_CH5XX_COMMAND_ADDRESS        0xc3u
#define RVSWD_FLASH_ERROR_CH5XX_COMMAND_FINISH         0xc4u
#define RVSWD_FLASH_ERROR_CH5XX_STATUS_BEGIN           0xc5u
#define RVSWD_FLASH_ERROR_CH5XX_STATUS_READ_FIRST      0xc6u
#define RVSWD_FLASH_ERROR_CH5XX_STATUS_READ_SECOND     0xc7u
#define RVSWD_FLASH_ERROR_CH5XX_STATUS_FINISH          0xc8u
#define RVSWD_FLASH_ERROR_CH5XX_STATUS_TIMEOUT         0xc9u
#define RVSWD_FLASH_ERROR_CH5XX_PAGE_UNALIGNED         0xcau
#define RVSWD_FLASH_ERROR_CH5XX_PAGE_ERASE_CODE_MODE   0xcbu
#define RVSWD_FLASH_ERROR_CH5XX_PAGE_ERASE_OPEN        0xccu
#define RVSWD_FLASH_ERROR_CH5XX_PAGE_PROGRAM_CODE_MODE 0xcdu
#define RVSWD_FLASH_ERROR_CH5XX_PAGE_PROGRAM_OPEN      0xceu
#define RVSWD_FLASH_ERROR_CH5XX_PAGE_PROGRAM_BEGIN     0xd0u
#define RVSWD_FLASH_ERROR_CH5XX_PAGE_PROGRAM_SETUP     0xd1u
#define RVSWD_FLASH_ERROR_CH5XX_PAGE_PROGRAM_DATA      0xd2u
#define RVSWD_FLASH_ERROR_CH5XX_PAGE_PROGRAM_EXECUTE   0xd3u
#define RVSWD_FLASH_ERROR_CH5XX_PAGE_PROGRAM_WAIT      0xd4u
#define RVSWD_FLASH_ERROR_CH5XX_PAGE_PROGRAM_STATUS    0xd5u

#define RVSWD_FLASH_KEYR_ADDRESS     0x40022004u
#define RVSWD_FLASH_OBKEYR_ADDRESS   0x40022008u
#define RVSWD_FLASH_STATR_ADDRESS    0x4002200cu
#define RVSWD_FLASH_CTLR_ADDRESS     0x40022010u
#define RVSWD_FLASH_ADDR_ADDRESS     0x40022014u
#define RVSWD_FLASH_OBR_ADDRESS      0x4002201cu
#define RVSWD_FLASH_WPR_ADDRESS      0x40022020u
#define RVSWD_FLASH_MODEKEYR_ADDRESS 0x40022024u
#define RVSWD_OPTION_BYTES_ADDRESS   0x1ffff800u

#define RVSWD_FLASH_KEY1 0x45670123u
#define RVSWD_FLASH_KEY2 0xcdef89abu

#define RVSWD_FLASH_STATR_BUSY          (1u << 0u)
#define RVSWD_FLASH_STATR_WRPRTERR      (1u << 4u)
#define RVSWD_FLASH_STATR_EOP           (1u << 5u)
#define RVSWD_FLASH_CTLR_MER            (1u << 2u)
#define RVSWD_FLASH_CTLR_OPTION_PROGRAM (1u << 4u)
#define RVSWD_FLASH_CTLR_OPTER          (1u << 5u)
#define RVSWD_FLASH_CTLR_STRT           (1u << 6u)
#define RVSWD_FLASH_CTLR_LOCK           (1u << 7u)
#define RVSWD_FLASH_CTLR_OPTION_WRITE   (1u << 9u)
#define RVSWD_FLASH_CTLR_FAST_LOCK      (1u << 15u)
#define RVSWD_FLASH_CTLR_FAST_PROGRAM   (1u << 16u)
#define RVSWD_FLASH_CTLR_BUFFER_LOAD    (1u << 18u)
#define RVSWD_FLASH_CTLR_BUFFER_RESET   (1u << 19u)
#define RVSWD_FLASH_OBR_READ_PROTECTED  (1u << 1u)
#define RVSWD_OPTION_BYTES_WORD_COUNT   4u
#define RVSWD_OPTION_RDP_PROTECTED      0x00ffu
#define RVSWD_OPTION_RDP_UNPROTECTED    0x5aa5u
#define RVSWD_FLASH_ERASE_TIMEOUT_US    6000000u

static uint32_t rvswd_flash_last_error_value;

extern const uint8_t ch5xx_flash_erase_stub_start[];
extern const uint8_t ch5xx_flash_erase_stub_end[];

enum rvswd_ch5xx_byte_access_mode {
    RVSWD_CH5XX_BYTE_ACCESS_NONE,
    RVSWD_CH5XX_BYTE_ACCESS_READ,
    RVSWD_CH5XX_BYTE_ACCESS_WRITE,
};

struct rvswd_ch5xx_byte_access {
    enum rvswd_ch5xx_byte_access_mode mode;
};

static bool rvswd_flash_ch5xx_prepare_byte_access(
    struct rvswd_ch5xx_byte_access *access,
    enum rvswd_ch5xx_byte_access_mode mode) {
    uint32_t load_instruction;
    uint32_t store_instruction;

    if (access->mode == mode) {
        return true;
    }

    if (mode == RVSWD_CH5XX_BYTE_ACCESS_READ) {
        load_instruction = 0x00058483u;
        store_instruction = 0x00968223u;
    } else if (mode == RVSWD_CH5XX_BYTE_ACCESS_WRITE) {
        load_instruction = 0x00468483u;
        store_instruction = 0x00958023u;
    } else {
        return false;
    }

    // 全擦期间连续访问 Flash 命令口，切换读写方向时才重建 Program Buffer
    if (!rvswd_debug_write_raw_gpr(13u, RVSWD_CH5XX_DEBUG_DATA_ADDRESS) ||
        !rvswd_dmi_write(0x16u, 0x00000700u) ||
        !rvswd_dmi_write(0x20u, load_instruction) ||
        !rvswd_dmi_write(0x21u, store_instruction) ||
        !rvswd_dmi_write(0x22u, 0x00100073u)) {
        access->mode = RVSWD_CH5XX_BYTE_ACCESS_NONE;
        return false;
    }

    access->mode = mode;
    return true;
}

static bool rvswd_flash_ch5xx_erase_write8(struct rvswd_ch5xx_byte_access *access,
                                           uint32_t address, uint8_t value) {
    uint32_t abstractcs;

    if (!rvswd_flash_ch5xx_prepare_byte_access(access, RVSWD_CH5XX_BYTE_ACCESS_WRITE) ||
        !rvswd_dmi_write(0x05u, value) ||
        !rvswd_dmi_write(0x04u, address) ||
        !rvswd_dmi_write(0x17u, 0x0027100bu) ||
        !rvswd_debug_wait_abstract_idle(&abstractcs)) {
        return false;
    }

    return ((abstractcs >> 8u) & 0x07u) == 0u;
}

static bool rvswd_flash_ch5xx_erase_read8(struct rvswd_ch5xx_byte_access *access,
                                          uint32_t address, uint8_t *value) {
    uint32_t abstractcs;
    uint32_t data;

    if (value == NULL ||
        !rvswd_flash_ch5xx_prepare_byte_access(access, RVSWD_CH5XX_BYTE_ACCESS_READ) ||
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

static bool rvswd_memory_write16(uint32_t address, uint16_t value,
                                 uint32_t timeout_us) {
    uint32_t abstractcs;

    // 使用 x8 保存数据，x9 保存目标地址，Program Buffer 执行 sh
    if (!rvswd_dmi_write(0x18u, 0u) ||
        !rvswd_dmi_write(0x16u, 0x00000700u) ||
        !rvswd_dmi_write(0x20u, 0x00849023u) ||
        !rvswd_dmi_write(0x21u, 0x00100073u) ||
        !rvswd_dmi_write(0x04u, address) ||
        !rvswd_dmi_write(0x17u, 0x00231009u) ||
        !rvswd_debug_wait_abstract_idle(&abstractcs) ||
        ((abstractcs >> 8u) & 0x07u) != 0u ||
        !rvswd_dmi_write(0x04u, value) ||
        !rvswd_dmi_write(0x17u, 0x00271008u) ||
        !rvswd_debug_wait_abstract_idle_timeout(&abstractcs, timeout_us)) {
        return false;
    }

    return ((abstractcs >> 8u) & 0x07u) == 0u;
}

static bool rvswd_flash_wait_ready(const struct rvswd_target_profile *profile,
                                   uint32_t *status, uint8_t read_error,
                                   uint8_t timeout_error) {
    uint64_t start = bsp_time_us();

    do {
        if (!rvswd_memory_read32(profile, true, RVSWD_FLASH_STATR_ADDRESS,
                                 status)) {
            rvswd_flash_last_error_value = read_error;
            return false;
        }
        if ((*status & RVSWD_FLASH_STATR_BUSY) == 0u) {
            return true;
        }
        bsp_delay_us(100u);
    } while ((bsp_time_us() - start) < RVSWD_FLASH_ERASE_TIMEOUT_US);

    rvswd_flash_last_error_value = timeout_error;
    return false;
}

static bool rvswd_flash_unlock_main_option_and_fast(uint32_t control) {
    if ((control & (RVSWD_FLASH_CTLR_LOCK | RVSWD_FLASH_CTLR_FAST_LOCK)) == 0u) {
        return true;
    }

    // L103 需要同时解锁主存储区、用户字和快速编程模式
    return rvswd_memory_write32(RVSWD_FLASH_KEYR_ADDRESS, RVSWD_FLASH_KEY1) &&
           rvswd_memory_write32(RVSWD_FLASH_KEYR_ADDRESS, RVSWD_FLASH_KEY2) &&
           rvswd_memory_write32(RVSWD_FLASH_OBKEYR_ADDRESS, RVSWD_FLASH_KEY1) &&
           rvswd_memory_write32(RVSWD_FLASH_OBKEYR_ADDRESS, RVSWD_FLASH_KEY2) &&
           rvswd_memory_write32(RVSWD_FLASH_MODEKEYR_ADDRESS, RVSWD_FLASH_KEY1) &&
           rvswd_memory_write32(RVSWD_FLASH_MODEKEYR_ADDRESS, RVSWD_FLASH_KEY2);
}

static bool rvswd_flash_unlock_main_and_fast(uint32_t control) {
    if ((control & RVSWD_FLASH_CTLR_LOCK) != 0u &&
        (!rvswd_memory_write32(RVSWD_FLASH_KEYR_ADDRESS, RVSWD_FLASH_KEY1) ||
         !rvswd_memory_write32(RVSWD_FLASH_KEYR_ADDRESS, RVSWD_FLASH_KEY2))) {
        return false;
    }
    if ((control & RVSWD_FLASH_CTLR_FAST_LOCK) != 0u &&
        (!rvswd_memory_write32(RVSWD_FLASH_MODEKEYR_ADDRESS, RVSWD_FLASH_KEY1) ||
         !rvswd_memory_write32(RVSWD_FLASH_MODEKEYR_ADDRESS, RVSWD_FLASH_KEY2))) {
        return false;
    }
    return true;
}

static bool rvswd_flash_ch5xx_flash_issue(struct rvswd_ch5xx_byte_access *access,
                                          uint8_t command) {
    return rvswd_flash_ch5xx_erase_write8(access, RVSWD_CH5XX_FLASH_CONTROL_ADDRESS, 0u) &&
           rvswd_flash_ch5xx_erase_write8(access, RVSWD_CH5XX_FLASH_CONTROL_ADDRESS, 5u) &&
           rvswd_flash_ch5xx_erase_write8(access, RVSWD_CH5XX_FLASH_BYTE_DATA_ADDRESS,
                                          command);
}

static bool rvswd_flash_ch5xx_flash_wait_control_ready(
    struct rvswd_ch5xx_byte_access *access) {
    for (uint32_t retry = 0u; retry < RVSWD_CH5XX_FLASH_STATUS_RETRIES; ++retry) {
        uint8_t control;

        if (!rvswd_flash_ch5xx_erase_read8(access, RVSWD_CH5XX_FLASH_CONTROL_ADDRESS,
                                           &control)) {
            return false;
        }
        if ((control & 0x80u) == 0u) {
            return true;
        }
    }
    return false;
}

static bool rvswd_flash_ch5xx_flash_end(struct rvswd_ch5xx_byte_access *access) {
    return rvswd_flash_ch5xx_flash_wait_control_ready(access) &&
           rvswd_flash_ch5xx_erase_write8(access, RVSWD_CH5XX_FLASH_CONTROL_ADDRESS, 0u);
}

static bool rvswd_flash_ch5xx_flash_out(struct rvswd_ch5xx_byte_access *access,
                                        uint8_t value) {
    return rvswd_flash_ch5xx_flash_wait_control_ready(access) &&
           rvswd_flash_ch5xx_erase_write8(access, RVSWD_CH5XX_FLASH_BYTE_DATA_ADDRESS,
                                          value);
}

static bool rvswd_flash_ch5xx_flash_in(struct rvswd_ch5xx_byte_access *access,
                                       uint8_t *value) {
    return rvswd_flash_ch5xx_flash_wait_control_ready(access) &&
           rvswd_flash_ch5xx_erase_read8(access, RVSWD_CH5XX_FLASH_BYTE_DATA_ADDRESS,
                                         value);
}

static bool rvswd_flash_ch5xx_flash_begin(struct rvswd_ch5xx_byte_access *access,
                                          uint8_t command) {
    // CH5xx 每次切换命令前先完成命令 6，命令口状态不能跨命令复用
    return rvswd_flash_ch5xx_flash_issue(access, 6u) &&
           rvswd_flash_ch5xx_flash_end(access) &&
           rvswd_flash_ch5xx_flash_issue(access, command);
}

static bool rvswd_flash_ch5xx_flash_open(struct rvswd_ch5xx_byte_access *access) {
    // FlashOpen 先使能命令口，再完成 0xff 初始化命令，擦页和写页均从此状态开始
    return rvswd_flash_ch5xx_erase_write8(access, RVSWD_CH5XX_FLASH_CONTROL_ADDRESS, 4u) &&
           rvswd_flash_ch5xx_flash_begin(access, 0xffu) &&
           rvswd_flash_ch5xx_flash_end(access);
}

static bool rvswd_flash_ch5xx_flash_enable_code_mode(void) {
    uint32_t abstractcs;

    // SAFE_ACCESS_SIG 必须在修改 ROM 配置后清零，避免命令口继承未完成的安全访问状态
    if (!rvswd_debug_write_raw_gpr(13u, RVSWD_CH5XX_FLASH_KEY_ADDRESS) ||
        !rvswd_debug_write_raw_gpr(10u, 0x57u) ||
        !rvswd_debug_write_raw_gpr(11u, 0xa8u) ||
        !rvswd_debug_write_raw_gpr(12u, 0xe0u) ||
        !rvswd_dmi_write(0x18u, 0u) ||
        !rvswd_dmi_write(0x16u, 0x00000700u) ||
        !rvswd_dmi_write(0x20u, 0x00a68023u) ||
        !rvswd_dmi_write(0x21u, 0x00b68023u) ||
        !rvswd_dmi_write(0x22u, 0x00010001u) ||
        !rvswd_dmi_write(0x23u, 0x00c68223u) ||
        !rvswd_dmi_write(0x24u, 0x00068023u) ||
        !rvswd_dmi_write(0x25u, 0x00100073u) ||
        !rvswd_dmi_write(0x17u, 0x00271000u) ||
        !rvswd_debug_wait_abstract_idle(&abstractcs)) {
        return false;
    }

    return ((abstractcs >> 8u) & 0x07u) == 0u;
}

static bool rvswd_flash_ch5xx_flash_write_address(struct rvswd_ch5xx_byte_access *access,
                                                  uint32_t address) {
    return rvswd_flash_ch5xx_flash_out(access, (uint8_t)(address >> 16u)) &&
           rvswd_flash_ch5xx_flash_out(access, (uint8_t)(address >> 8u)) &&
           rvswd_flash_ch5xx_flash_out(access, (uint8_t)address);
}

static bool rvswd_flash_ch5xx_flash_wait_ready(struct rvswd_ch5xx_byte_access *access) {
    if (!rvswd_flash_ch5xx_flash_end(access)) {
        rvswd_flash_last_error_value = RVSWD_FLASH_ERROR_CH5XX_COMMAND_FINISH;
        return false;
    }

    for (uint32_t retry = 0u; retry < RVSWD_CH5XX_FLASH_STATUS_RETRIES; ++retry) {
        uint8_t status;

        // LinkE 连续读取两次状态，第二次读数用于判断命令是否仍在执行
        if (!rvswd_flash_ch5xx_flash_begin(access, 5u)) {
            rvswd_flash_last_error_value = RVSWD_FLASH_ERROR_CH5XX_STATUS_BEGIN;
            return false;
        }
        if (!rvswd_flash_ch5xx_flash_in(access, &status)) {
            rvswd_flash_last_error_value = RVSWD_FLASH_ERROR_CH5XX_STATUS_READ_FIRST;
            return false;
        }
        if (!rvswd_flash_ch5xx_flash_in(access, &status)) {
            rvswd_flash_last_error_value = RVSWD_FLASH_ERROR_CH5XX_STATUS_READ_SECOND;
            return false;
        }
        if (!rvswd_flash_ch5xx_flash_end(access)) {
            rvswd_flash_last_error_value = RVSWD_FLASH_ERROR_CH5XX_STATUS_FINISH;
            return false;
        }
        if ((status & 1u) == 0u) {
            return true;
        }
    }
    rvswd_flash_last_error_value = RVSWD_FLASH_ERROR_CH5XX_STATUS_TIMEOUT;
    return false;
}

static bool rvswd_flash_ch5xx_flash_erase_command(
    struct rvswd_ch5xx_byte_access *access, uint32_t address, uint8_t command) {
    if (!rvswd_flash_ch5xx_flash_begin(access, command)) {
        rvswd_flash_last_error_value = RVSWD_FLASH_ERROR_CH5XX_COMMAND_BEGIN;
        return false;
    }
    if (!rvswd_flash_ch5xx_flash_write_address(access, address)) {
        rvswd_flash_last_error_value = RVSWD_FLASH_ERROR_CH5XX_COMMAND_ADDRESS;
        return false;
    }
    if (!rvswd_flash_ch5xx_flash_wait_ready(access)) {
        return false;
    }
    return true;
}

static bool rvswd_flash_ch5xx_flash_program_page(
    struct rvswd_ch5xx_byte_access *access, const uint8_t *data) {
    uint32_t abstractcs;

    // 官方 0x33D4 与 CH5xx 参考实现都使用该 Program Buffer，将 a4 的字写入 Flash 数据口
    access->mode = RVSWD_CH5XX_BYTE_ACCESS_NONE;
    if (!rvswd_debug_write_raw_gpr(13u, RVSWD_CH5XX_FLASH_WORD_DATA_ADDRESS) ||
        !rvswd_debug_write_raw_gpr(5u, 21u) ||
        !rvswd_dmi_write(0x18u, 0u) ||
        !rvswd_dmi_write(0x16u, 0x00000700u) ||
        !rvswd_dmi_write(0x20u, 0x4791c298u) ||
        !rvswd_dmi_write(0x21u, 0x00668703u) ||
        !rvswd_dmi_write(0x22u, 0xfe074ee3u) ||
        !rvswd_dmi_write(0x23u, 0x00568323u) ||
        !rvswd_dmi_write(0x24u, 0xfbed17fdu) ||
        !rvswd_dmi_write(0x25u, 0x00100073u)) {
        rvswd_flash_last_error_value = RVSWD_FLASH_ERROR_CH5XX_PAGE_PROGRAM_SETUP;
        return false;
    }

    for (uint32_t offset = 0u; offset < RVSWD_CH5XX_FLASH_PAGE_SIZE; offset += 4u) {
        uint32_t value = (uint32_t)data[offset + 0u] |
                         ((uint32_t)data[offset + 1u] << 8u) |
                         ((uint32_t)data[offset + 2u] << 16u) |
                         ((uint32_t)data[offset + 3u] << 24u);

        if (!rvswd_dmi_write(0x04u, value)) {
            rvswd_flash_last_error_value = RVSWD_FLASH_ERROR_CH5XX_PAGE_PROGRAM_DATA;
            return false;
        }
        // 该 Abstract Command 同时把 data0 传入 a4 并执行 Program Buffer
        if (!rvswd_dmi_write(0x17u, 0x0027100eu)) {
            rvswd_flash_last_error_value = RVSWD_FLASH_ERROR_CH5XX_PAGE_PROGRAM_EXECUTE;
            return false;
        }
        // 目标执行真实 Flash 写入时 busy 可长于普通内存访问，单字仍受 100 ms 上限约束
        if (!rvswd_debug_wait_abstract_idle_timeout(
                &abstractcs, RVSWD_CH5XX_PAGE_PROGRAM_TIMEOUT_US)) {
            rvswd_flash_last_error_value = RVSWD_FLASH_ERROR_CH5XX_PAGE_PROGRAM_WAIT;
            return false;
        }
        if (((abstractcs >> 8u) & 0x07u) != 0u) {
            rvswd_flash_last_error_value = RVSWD_FLASH_ERROR_CH5XX_PAGE_PROGRAM_STATUS;
            return false;
        }
    }
    return true;
}

bool rvswd_flash_rewrite_page(uint32_t address, const uint8_t *data) {
    const struct rvswd_target_profile *profile = rvswd_target_profile_current();
    struct rvswd_ch5xx_byte_access access = {
        .mode = RVSWD_CH5XX_BYTE_ACCESS_NONE,
    };

    rvswd_flash_last_error_value = 0u;
    if (profile == NULL || !profile->ch5xx_protocol || data == NULL) {
        rvswd_flash_last_error_value = 0x0fu;
        return false;
    }
    if ((address & 0xffu) != 0u) {
        rvswd_flash_last_error_value = RVSWD_FLASH_ERROR_CH5XX_PAGE_UNALIGNED;
        return false;
    }

    // 官方 0x0A 路径先用命令 0x81 擦页，再独立建立命令 0x02 的整页写入状态
    if (!rvswd_flash_ch5xx_flash_enable_code_mode()) {
        rvswd_flash_last_error_value = RVSWD_FLASH_ERROR_CH5XX_PAGE_ERASE_CODE_MODE;
        return false;
    }
    if (!rvswd_flash_ch5xx_flash_open(&access)) {
        rvswd_flash_last_error_value = RVSWD_FLASH_ERROR_CH5XX_PAGE_ERASE_OPEN;
        return false;
    }
    if (!rvswd_flash_ch5xx_flash_erase_command(&access, address, 0x81u)) {
        return false;
    }
    // 擦页会结束前一条 Flash 命令，写页前重新写入 SAFE_ACCESS_SIG 和 code mode
    if (!rvswd_flash_ch5xx_flash_enable_code_mode()) {
        rvswd_flash_last_error_value = RVSWD_FLASH_ERROR_CH5XX_PAGE_PROGRAM_CODE_MODE;
        return false;
    }
    if (!rvswd_flash_ch5xx_flash_open(&access)) {
        rvswd_flash_last_error_value = RVSWD_FLASH_ERROR_CH5XX_PAGE_PROGRAM_OPEN;
        return false;
    }
    if (!rvswd_flash_ch5xx_flash_begin(&access, 0x02u) ||
        !rvswd_flash_ch5xx_flash_write_address(&access, address)) {
        rvswd_flash_last_error_value = RVSWD_FLASH_ERROR_CH5XX_PAGE_PROGRAM_BEGIN;
        return false;
    }
    if (!rvswd_flash_ch5xx_flash_program_page(&access, data)) {
        return false;
    }
    if (!rvswd_flash_ch5xx_flash_wait_ready(&access)) {
        return false;
    }
    return true;
}

static bool rvswd_flash_ch5xx_flash_erase_all(
    const struct rvswd_target_profile *profile) {
    size_t stub_length =
        (size_t)(ch5xx_flash_erase_stub_end - ch5xx_flash_erase_stub_start);
    uint32_t result;

    // RAM stub 按 LinkE 固件的 4 KiB 扇区路径完成整片擦除
    if (stub_length == 0u || stub_length > RVSWD_CH5XX_ERASE_STUB_MAX_SIZE ||
        (stub_length & 3u) != 0u) {
        rvswd_flash_last_error_value = 0xc1u;
        return false;
    }
    if (!rvswd_memory_write(profile, RVSWD_CH5XX_ERASE_STUB_ADDRESS,
                            ch5xx_flash_erase_stub_start,
                            (uint32_t)stub_length)) {
        rvswd_flash_last_error_value = 0xc2u;
        return false;
    }
    if (!rvswd_flash_ch5xx_flash_enable_code_mode()) {
        rvswd_flash_last_error_value = 0xc3u;
        return false;
    }
    if (!rvswd_debug_execute(RVSWD_CH5XX_ERASE_STUB_ADDRESS,
                             RVSWD_CH5XX_ERASE_STUB_STACK_TOP, 0u, 0u,
                             RVSWD_CH5XX_FLASH_END, 0u, &result)) {
        rvswd_flash_last_error_value = 0xc4u;
        return false;
    }
    if (result != 0u) {
        rvswd_flash_last_error_value = 0xc5u;
        return false;
    }
    return true;
}

bool rvswd_flash_erase_all(void) {
    const struct rvswd_target_profile *profile = rvswd_target_profile_current();
    uint32_t control;
    uint32_t idle_control;
    uint32_t status;
    bool unlocked;
    bool success = false;

    rvswd_flash_last_error_value = 0u;
    if (profile == NULL) {
        rvswd_flash_last_error_value = 0x0fu;
        return false;
    }
    if (profile->ch5xx_protocol) {
        return rvswd_flash_ch5xx_flash_erase_all(profile);
    }

    if (!rvswd_flash_wait_ready(profile, &status, 0x11u, 0x12u)) {
        return false;
    }

    if (!rvswd_memory_read32(profile, true, RVSWD_FLASH_CTLR_ADDRESS,
                             &control)) {
        rvswd_flash_last_error_value = 0x13u;
        return false;
    }

    switch (profile->erase_unlock) {
        case RVSWD_FLASH_UNLOCK_MAIN_OPTION_AND_FAST:
            unlocked = rvswd_flash_unlock_main_option_and_fast(control);
            break;
        case RVSWD_FLASH_UNLOCK_MAIN_AND_FAST:
            unlocked = rvswd_flash_unlock_main_and_fast(control);
            break;
        default:
            rvswd_flash_last_error_value = 0x0fu;
            return false;
    }
    if (!unlocked) {
        rvswd_flash_last_error_value = 0x14u;
        return false;
    }

    if (!rvswd_memory_read32(profile, true, RVSWD_FLASH_CTLR_ADDRESS,
                             &control)) {
        rvswd_flash_last_error_value = 0x15u;
        return false;
    }
    if ((control & (RVSWD_FLASH_CTLR_LOCK | RVSWD_FLASH_CTLR_FAST_LOCK)) != 0u) {
        rvswd_flash_last_error_value = 0x16u;
        return false;
    }

    idle_control = control & ~(RVSWD_FLASH_CTLR_MER | RVSWD_FLASH_CTLR_STRT);

    // 清除上一次操作遗留的完成和写保护状态，避免误判本次擦除
    if ((status & (RVSWD_FLASH_STATR_EOP | RVSWD_FLASH_STATR_WRPRTERR)) != 0u &&
        !rvswd_memory_write32(
            RVSWD_FLASH_STATR_ADDRESS,
            status & (RVSWD_FLASH_STATR_EOP | RVSWD_FLASH_STATR_WRPRTERR))) {
        rvswd_flash_last_error_value = 0x17u;
        goto cleanup;
    }

    if (!rvswd_memory_write32(RVSWD_FLASH_CTLR_ADDRESS, idle_control)) {
        rvswd_flash_last_error_value = 0x18u;
        goto cleanup;
    }
    if (!rvswd_memory_write32(RVSWD_FLASH_CTLR_ADDRESS,
                              idle_control | RVSWD_FLASH_CTLR_MER)) {
        rvswd_flash_last_error_value = 0x19u;
        goto cleanup;
    }
    if (!rvswd_memory_write32(
            RVSWD_FLASH_CTLR_ADDRESS,
            idle_control | RVSWD_FLASH_CTLR_MER | RVSWD_FLASH_CTLR_STRT)) {
        rvswd_flash_last_error_value = 0x1au;
        goto cleanup;
    }
    if (!rvswd_flash_wait_ready(profile, &status, 0x1bu, 0x1cu)) {
        goto cleanup;
    }
    if ((status & RVSWD_FLASH_STATR_WRPRTERR) != 0u) {
        rvswd_flash_last_error_value = 0x1du;
        goto cleanup;
    }
    success = true;

cleanup:
    if (!rvswd_memory_write32(RVSWD_FLASH_CTLR_ADDRESS, idle_control)) {
        if (rvswd_flash_last_error_value == 0u) {
            rvswd_flash_last_error_value = 0x1eu;
        }
        success = false;
    }
    return success;
}

bool rvswd_flash_read_protected(bool *protected) {
    const struct rvswd_target_profile *profile = rvswd_target_profile_current();
    uint32_t option_status;

    rvswd_flash_last_error_value = 0u;
    if (protected == NULL) {
        rvswd_flash_last_error_value = 0x21u;
        return false;
    }
    if (profile == NULL || profile->ch5xx_protocol) {
        rvswd_flash_last_error_value = 0x22u;
        return false;
    }
    if (!rvswd_memory_read32(profile, true, RVSWD_FLASH_OBR_ADDRESS,
                             &option_status)) {
        rvswd_flash_last_error_value = 0x23u;
        return false;
    }

    *protected = (option_status & RVSWD_FLASH_OBR_READ_PROTECTED) != 0u;
    return true;
}

bool rvswd_flash_write_protected(bool *protected) {
    const struct rvswd_target_profile *profile = rvswd_target_profile_current();
    uint32_t write_protection;

    rvswd_flash_last_error_value = 0u;
    if (protected == NULL) {
        rvswd_flash_last_error_value = 0x24u;
        return false;
    }
    if (profile == NULL || profile->ch5xx_protocol) {
        rvswd_flash_last_error_value = 0x25u;
        return false;
    }
    if (!rvswd_memory_read32(profile, true, RVSWD_FLASH_WPR_ADDRESS,
                             &write_protection)) {
        rvswd_flash_last_error_value = 0x26u;
        return false;
    }

    *protected = write_protection != 0xffffffffu;
    return true;
}

static bool rvswd_flash_unlock_option_bytes(bool unlock_fast_mode) {
    if (!rvswd_memory_write32(RVSWD_FLASH_KEYR_ADDRESS, RVSWD_FLASH_KEY1)) {
        rvswd_flash_last_error_value = 0xa1u;
        return false;
    }
    if (!rvswd_memory_write32(RVSWD_FLASH_KEYR_ADDRESS, RVSWD_FLASH_KEY2)) {
        rvswd_flash_last_error_value = 0xa2u;
        return false;
    }
    if (unlock_fast_mode) {
        if (!rvswd_memory_write32(RVSWD_FLASH_MODEKEYR_ADDRESS,
                                  RVSWD_FLASH_KEY1)) {
            rvswd_flash_last_error_value = 0xa3u;
            return false;
        }
        if (!rvswd_memory_write32(RVSWD_FLASH_MODEKEYR_ADDRESS,
                                  RVSWD_FLASH_KEY2)) {
            rvswd_flash_last_error_value = 0xa4u;
            return false;
        }
    }
    if (!rvswd_memory_write32(RVSWD_FLASH_OBKEYR_ADDRESS,
                              RVSWD_FLASH_KEY1)) {
        rvswd_flash_last_error_value = 0xa5u;
        return false;
    }
    if (!rvswd_memory_write32(RVSWD_FLASH_OBKEYR_ADDRESS,
                              RVSWD_FLASH_KEY2)) {
        rvswd_flash_last_error_value = 0xa6u;
        return false;
    }
    return true;
}

static bool rvswd_flash_write_option_bytes_fast_buffer(
    const struct rvswd_target_profile *profile, const uint32_t *option_words) {
    uint32_t control;
    uint32_t idle_control = 0u;
    uint32_t status;
    bool success = false;

    if (!rvswd_flash_wait_ready(profile, &status, 0x31u, 0x32u) ||
        !rvswd_memory_read32(profile, true, RVSWD_FLASH_CTLR_ADDRESS,
                             &control)) {
        if (rvswd_flash_last_error_value == 0u) {
            rvswd_flash_last_error_value = 0x33u;
        }
        return false;
    }
    if (!rvswd_flash_unlock_option_bytes(true)) {
        if (rvswd_flash_last_error_value == 0u) {
            rvswd_flash_last_error_value = 0x34u;
        }
        return false;
    }
    if (!rvswd_memory_read32(profile, true, RVSWD_FLASH_CTLR_ADDRESS,
                             &control)) {
        rvswd_flash_last_error_value = 0x35u;
        return false;
    }
    if ((control & (RVSWD_FLASH_CTLR_LOCK | RVSWD_FLASH_CTLR_FAST_LOCK)) != 0u ||
        (control & RVSWD_FLASH_CTLR_OPTION_WRITE) == 0u) {
        rvswd_flash_last_error_value = 0x36u;
        return false;
    }

    idle_control = control & ~(RVSWD_FLASH_CTLR_OPTER | RVSWD_FLASH_CTLR_STRT |
                               RVSWD_FLASH_CTLR_FAST_PROGRAM |
                               RVSWD_FLASH_CTLR_BUFFER_LOAD |
                               RVSWD_FLASH_CTLR_BUFFER_RESET);

    // Option Bytes 擦除和重写必须保持完整的 16 字节镜像
    if (!rvswd_memory_write32(RVSWD_FLASH_CTLR_ADDRESS,
                              idle_control | RVSWD_FLASH_CTLR_OPTER) ||
        !rvswd_memory_write32(
            RVSWD_FLASH_CTLR_ADDRESS,
            idle_control | RVSWD_FLASH_CTLR_OPTER | RVSWD_FLASH_CTLR_STRT) ||
        !rvswd_flash_wait_ready(profile, &status, 0x36u, 0x37u)) {
        if (rvswd_flash_last_error_value == 0u) {
            rvswd_flash_last_error_value = 0x38u;
        }
        goto cleanup;
    }
    if ((status & RVSWD_FLASH_STATR_WRPRTERR) != 0u) {
        rvswd_flash_last_error_value = 0x39u;
        goto cleanup;
    }

    // 选项字擦除完成后重新解锁快速编程模式，缓冲写入不保持 OPTWRE
    if (!rvswd_memory_read32(profile, true, RVSWD_FLASH_CTLR_ADDRESS,
                             &control) ||
        !rvswd_flash_unlock_main_and_fast(control) ||
        !rvswd_memory_read32(profile, true, RVSWD_FLASH_CTLR_ADDRESS,
                             &control)) {
        rvswd_flash_last_error_value = 0x3au;
        goto cleanup;
    }
    if ((control & (RVSWD_FLASH_CTLR_LOCK | RVSWD_FLASH_CTLR_FAST_LOCK)) != 0u) {
        rvswd_flash_last_error_value = 0x3bu;
        goto cleanup;
    }

    idle_control = control & ~(RVSWD_FLASH_CTLR_OPTION_WRITE |
                               RVSWD_FLASH_CTLR_OPTER | RVSWD_FLASH_CTLR_STRT |
                               RVSWD_FLASH_CTLR_FAST_PROGRAM |
                               RVSWD_FLASH_CTLR_BUFFER_LOAD |
                               RVSWD_FLASH_CTLR_BUFFER_RESET);

    if (!rvswd_memory_write32(RVSWD_FLASH_CTLR_ADDRESS,
                              idle_control | RVSWD_FLASH_CTLR_FAST_PROGRAM) ||
        !rvswd_memory_write32(
            RVSWD_FLASH_CTLR_ADDRESS,
            idle_control | RVSWD_FLASH_CTLR_FAST_PROGRAM |
                RVSWD_FLASH_CTLR_BUFFER_RESET) ||
        !rvswd_flash_wait_ready(profile, &status, 0x3au, 0x3bu) ||
        !rvswd_memory_write32(RVSWD_FLASH_CTLR_ADDRESS, idle_control)) {
        if (rvswd_flash_last_error_value == 0u) {
            rvswd_flash_last_error_value = 0x3cu;
        }
        goto cleanup;
    }

    for (uint32_t index = 0u; index < RVSWD_OPTION_BYTES_WORD_COUNT; ++index) {
        if (!rvswd_memory_write32(RVSWD_FLASH_CTLR_ADDRESS,
                                  idle_control | RVSWD_FLASH_CTLR_FAST_PROGRAM) ||
            !rvswd_memory_write32(profile->option_base + index * 4u,
                                  option_words[index]) ||
            !rvswd_memory_write32(
                RVSWD_FLASH_CTLR_ADDRESS,
                idle_control | RVSWD_FLASH_CTLR_FAST_PROGRAM |
                    RVSWD_FLASH_CTLR_BUFFER_LOAD) ||
            !rvswd_flash_wait_ready(profile, &status, 0x3du, 0x3eu) ||
            !rvswd_memory_write32(RVSWD_FLASH_CTLR_ADDRESS, idle_control)) {
            if (rvswd_flash_last_error_value == 0u) {
                rvswd_flash_last_error_value = 0x3fu;
            }
            goto cleanup;
        }
    }

    if (!rvswd_memory_write32(RVSWD_FLASH_CTLR_ADDRESS,
                              idle_control | RVSWD_FLASH_CTLR_FAST_PROGRAM) ||
        !rvswd_memory_write32(RVSWD_FLASH_ADDR_ADDRESS, profile->option_base) ||
        !rvswd_memory_write32(
            RVSWD_FLASH_CTLR_ADDRESS,
            idle_control | RVSWD_FLASH_CTLR_FAST_PROGRAM | RVSWD_FLASH_CTLR_STRT) ||
        !rvswd_flash_wait_ready(profile, &status, 0x40u, 0x41u)) {
        if (rvswd_flash_last_error_value == 0u) {
            rvswd_flash_last_error_value = 0x42u;
        }
        goto cleanup;
    }
    if ((status & RVSWD_FLASH_STATR_WRPRTERR) != 0u) {
        rvswd_flash_last_error_value = 0x43u;
        goto cleanup;
    }
    success = true;

cleanup:
    if (!rvswd_memory_write32(
            RVSWD_FLASH_CTLR_ADDRESS,
            (idle_control & ~RVSWD_FLASH_CTLR_OPTION_WRITE) |
                RVSWD_FLASH_CTLR_LOCK | RVSWD_FLASH_CTLR_FAST_LOCK)) {
        if (rvswd_flash_last_error_value == 0u) {
            rvswd_flash_last_error_value = 0x44u;
        }
        success = false;
    }
    return success;
}

static bool rvswd_flash_write_option_bytes_halfword(
    const struct rvswd_target_profile *profile, const uint32_t *option_words) {
    uint32_t control;
    uint32_t idle_control = 0u;
    uint32_t status;
    bool success = false;

    if (!rvswd_flash_wait_ready(profile, &status, 0x51u, 0x52u) ||
        !rvswd_memory_read32(profile, true, RVSWD_FLASH_CTLR_ADDRESS,
                             &control)) {
        if (rvswd_flash_last_error_value == 0u) {
            rvswd_flash_last_error_value = 0x53u;
        }
        return false;
    }
    if (!rvswd_flash_unlock_option_bytes(true) ||
        !rvswd_memory_read32(profile, true, RVSWD_FLASH_CTLR_ADDRESS,
                             &control)) {
        if (rvswd_flash_last_error_value == 0u) {
            rvswd_flash_last_error_value = 0x54u;
        }
        return false;
    }
    if ((control & (RVSWD_FLASH_CTLR_LOCK | RVSWD_FLASH_CTLR_FAST_LOCK)) != 0u ||
        (control & RVSWD_FLASH_CTLR_OPTION_WRITE) == 0u) {
        rvswd_flash_last_error_value = 0x55u;
        return false;
    }

    idle_control = control & ~(RVSWD_FLASH_CTLR_OPTION_PROGRAM |
                               RVSWD_FLASH_CTLR_OPTER | RVSWD_FLASH_CTLR_STRT);

    // Option Bytes 擦除和重写必须保持完整的 16 字节镜像
    if (!rvswd_memory_write32(RVSWD_FLASH_CTLR_ADDRESS,
                              idle_control | RVSWD_FLASH_CTLR_OPTER) ||
        !rvswd_memory_write32(
            RVSWD_FLASH_CTLR_ADDRESS,
            idle_control | RVSWD_FLASH_CTLR_OPTER | RVSWD_FLASH_CTLR_STRT) ||
        !rvswd_flash_wait_ready(profile, &status, 0x56u, 0x57u)) {
        if (rvswd_flash_last_error_value == 0u) {
            rvswd_flash_last_error_value = 0x58u;
        }
        goto cleanup;
    }
    if ((status & RVSWD_FLASH_STATR_WRPRTERR) != 0u) {
        rvswd_flash_last_error_value = 0x59u;
        goto cleanup;
    }

    // Option Bytes 擦除后重新解锁主存储区、快速模式和 Option Bytes
    if (!rvswd_memory_read32(profile, true, RVSWD_FLASH_CTLR_ADDRESS,
                             &control) ||
        !rvswd_flash_unlock_option_bytes(true) ||
        !rvswd_memory_read32(profile, true, RVSWD_FLASH_CTLR_ADDRESS,
                             &control)) {
        rvswd_flash_last_error_value = 0x5au;
        goto cleanup;
    }
    if ((control & (RVSWD_FLASH_CTLR_LOCK | RVSWD_FLASH_CTLR_FAST_LOCK)) != 0u ||
        (control & RVSWD_FLASH_CTLR_OPTION_WRITE) == 0u) {
        rvswd_flash_last_error_value = 0x5bu;
        goto cleanup;
    }

    idle_control = control & ~(RVSWD_FLASH_CTLR_OPTION_PROGRAM |
                               RVSWD_FLASH_CTLR_OPTER | RVSWD_FLASH_CTLR_STRT);

    for (uint32_t index = 0u; index < RVSWD_OPTION_BYTES_WORD_COUNT * 2u; ++index) {
        uint16_t value = (uint16_t)(option_words[index / 2u] >>
                                    ((index & 1u) * 16u));

        if (!rvswd_memory_write32(
                RVSWD_FLASH_CTLR_ADDRESS,
                idle_control | RVSWD_FLASH_CTLR_OPTION_PROGRAM) ||
            !rvswd_memory_write16(profile->option_base + index * 2u, value,
                                  RVSWD_ABSTRACT_TIMEOUT_US) ||
            !rvswd_flash_wait_ready(profile, &status, 0x5cu, 0x5du)) {
            if (rvswd_flash_last_error_value == 0u) {
                rvswd_flash_last_error_value = 0x5eu;
            }
            goto cleanup;
        }
        if ((status & RVSWD_FLASH_STATR_WRPRTERR) != 0u) {
            rvswd_flash_last_error_value = 0x5fu;
            goto cleanup;
        }
    }
    success = true;

cleanup:
    if (!rvswd_memory_write32(
            RVSWD_FLASH_CTLR_ADDRESS,
            (idle_control & ~RVSWD_FLASH_CTLR_OPTION_WRITE) |
                RVSWD_FLASH_CTLR_LOCK | RVSWD_FLASH_CTLR_FAST_LOCK)) {
        if (rvswd_flash_last_error_value == 0u) {
            rvswd_flash_last_error_value = 0x60u;
        }
        success = false;
    }
    return success;
}

static bool rvswd_flash_unprotect_option_bytes(
    const struct rvswd_target_profile *profile) {
    uint32_t control;
    uint32_t idle_control = 0u;
    uint32_t status;
    bool success = false;

    if (!rvswd_flash_wait_ready(profile, &status, 0x61u, 0x62u) ||
        !rvswd_memory_read32(profile, true, RVSWD_FLASH_CTLR_ADDRESS,
                             &control)) {
        if (rvswd_flash_last_error_value == 0u) {
            rvswd_flash_last_error_value = 0x63u;
        }
        return false;
    }
    if (!rvswd_flash_unlock_option_bytes(true) ||
        !rvswd_memory_read32(profile, true, RVSWD_FLASH_CTLR_ADDRESS,
                             &control)) {
        if (rvswd_flash_last_error_value == 0u) {
            rvswd_flash_last_error_value = 0x64u;
        }
        return false;
    }
    if ((control & RVSWD_FLASH_CTLR_LOCK) != 0u ||
        (control & RVSWD_FLASH_CTLR_OPTION_WRITE) == 0u) {
        rvswd_flash_last_error_value = 0x65u;
        return false;
    }

    idle_control = control & ~(RVSWD_FLASH_CTLR_OPTION_PROGRAM |
                               RVSWD_FLASH_CTLR_OPTER | RVSWD_FLASH_CTLR_STRT);
    if (!rvswd_memory_write32(RVSWD_FLASH_CTLR_ADDRESS,
                              idle_control | RVSWD_FLASH_CTLR_OPTER) ||
        !rvswd_memory_write32(
            RVSWD_FLASH_CTLR_ADDRESS,
            idle_control | RVSWD_FLASH_CTLR_OPTER | RVSWD_FLASH_CTLR_STRT) ||
        !rvswd_flash_wait_ready(profile, &status, 0x66u, 0x67u)) {
        if (rvswd_flash_last_error_value == 0u) {
            rvswd_flash_last_error_value = 0x68u;
        }
        goto cleanup;
    }
    if ((status & RVSWD_FLASH_STATR_WRPRTERR) != 0u) {
        rvswd_flash_last_error_value = 0x69u;
        goto cleanup;
    }

    // 解除读保护只恢复 RDP，保留 Option Bytes 擦除后的默认状态
    if (!rvswd_memory_read32(profile, true, RVSWD_FLASH_CTLR_ADDRESS,
                             &control) ||
        !rvswd_flash_unlock_option_bytes(true) ||
        !rvswd_memory_read32(profile, true, RVSWD_FLASH_CTLR_ADDRESS,
                             &control)) {
        rvswd_flash_last_error_value = 0x6au;
        goto cleanup;
    }
    if ((control & RVSWD_FLASH_CTLR_LOCK) != 0u ||
        (control & RVSWD_FLASH_CTLR_OPTION_WRITE) == 0u) {
        rvswd_flash_last_error_value = 0x6bu;
        goto cleanup;
    }

    idle_control = control & ~(RVSWD_FLASH_CTLR_OPTION_PROGRAM |
                               RVSWD_FLASH_CTLR_OPTER | RVSWD_FLASH_CTLR_STRT);
    if (!rvswd_memory_write32(
            RVSWD_FLASH_CTLR_ADDRESS,
            idle_control | RVSWD_FLASH_CTLR_OPTION_PROGRAM)) {
        rvswd_flash_last_error_value = 0x71u;
        goto cleanup;
    }
    if (!rvswd_memory_write16(profile->option_base,
                              RVSWD_OPTION_RDP_UNPROTECTED,
                              RVSWD_FLASH_ERASE_TIMEOUT_US)) {
        if (rvswd_flash_last_error_value == 0u) {
            rvswd_flash_last_error_value = 0x72u;
        }
        goto cleanup;
    }
    if (!rvswd_flash_wait_ready(profile, &status, 0x6cu, 0x6du)) {
        if (rvswd_flash_last_error_value == 0u) {
            rvswd_flash_last_error_value = 0x73u;
        }
        goto cleanup;
    }
    if ((status & RVSWD_FLASH_STATR_WRPRTERR) != 0u) {
        rvswd_flash_last_error_value = 0x6fu;
        goto cleanup;
    }
    success = true;

cleanup:
    if (!rvswd_memory_write32(
            RVSWD_FLASH_CTLR_ADDRESS,
            (idle_control & ~RVSWD_FLASH_CTLR_OPTION_WRITE) |
                RVSWD_FLASH_CTLR_LOCK)) {
        if (rvswd_flash_last_error_value == 0u) {
            rvswd_flash_last_error_value = 0x70u;
        }
        success = false;
    }
    return success;
}

bool rvswd_flash_set_read_protected(bool protected) {
    const struct rvswd_target_profile *profile = rvswd_target_profile_current();
    uint32_t option_words[RVSWD_OPTION_BYTES_WORD_COUNT];
    bool current;

    if (profile == NULL || profile->ch5xx_protocol) {
        rvswd_flash_last_error_value = 0x22u;
        return false;
    }
    if (!rvswd_flash_read_protected(&current)) {
        return false;
    }
    if (current == protected) {
        return true;
    }

    if (!protected && profile->option_write == RVSWD_OPTION_WRITE_FAST_BUFFER) {
        if (!rvswd_flash_unprotect_option_bytes(profile)) {
            return false;
        }
    } else {
        for (uint32_t index = 0u; index < RVSWD_OPTION_BYTES_WORD_COUNT; ++index) {
            if (!rvswd_memory_read32(profile, true,
                                     profile->option_base + index * 4u,
                                     &option_words[index])) {
                rvswd_flash_last_error_value = 0x45u;
                return false;
            }
        }

        option_words[0] = (option_words[0] & 0xffff0000u) |
                          (protected ? RVSWD_OPTION_RDP_PROTECTED
                                     : RVSWD_OPTION_RDP_UNPROTECTED);
        switch (profile->option_write) {
            case RVSWD_OPTION_WRITE_FAST_BUFFER:
                if (!rvswd_flash_write_option_bytes_fast_buffer(profile,
                                                                option_words)) {
                    return false;
                }
                break;
            case RVSWD_OPTION_WRITE_HALFWORD:
                if (!rvswd_flash_write_option_bytes_halfword(profile,
                                                             option_words)) {
                    return false;
                }
                break;
            default:
                rvswd_flash_last_error_value = 0x5fu;
                return false;
        }
    }

    // 解除读保护时目标硬件会自动整片擦除主存储区，复位后 OBR 才加载新状态
    if (!rvswd_reset_and_halt()) {
        rvswd_flash_last_error_value = 0x46u;
        return false;
    }
    if (!rvswd_flash_read_protected(&current)) {
        return false;
    }
    if (current != protected) {
        rvswd_flash_last_error_value = 0x47u;
        return false;
    }
    return true;
}

uint32_t rvswd_flash_last_error(void) {
    return rvswd_flash_last_error_value;
}
