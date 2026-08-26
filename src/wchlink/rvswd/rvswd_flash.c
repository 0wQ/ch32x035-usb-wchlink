#include "rvswd_flash.h"

#include "bsp/bsp_delay.h"
#include "rvswd_debug.h"
#include "rvswd_memory.h"
#include "rvswd_reset.h"
#include "rvswd_types.h"
#include "wchlink/flash/rvswd_flash_ch5xx.h"

#include <stddef.h>

#include <ch32x035.h>

#define RVSWD_FLASH_KEYR_ADDRESS     0x40022004u
#define RVSWD_FLASH_OBKEYR_ADDRESS   0x40022008u
#define RVSWD_FLASH_STATR_ADDRESS    0x4002200cu
#define RVSWD_FLASH_CTLR_ADDRESS     0x40022010u
#define RVSWD_FLASH_ADDR_ADDRESS     0x40022014u
#define RVSWD_FLASH_OBR_ADDRESS      0x4002201cu
#define RVSWD_FLASH_WPR_ADDRESS      0x40022020u
#define RVSWD_FLASH_MODEKEYR_ADDRESS 0x40022024u

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

static const uint32_t rvswd_flash_abstract_timeout_us = 10000u;

static bool rvswd_memory_write16(struct rvswd_operation *operation,
                                 uint32_t address, uint16_t value,
                                 uint32_t timeout_us) {
    uint32_t abstractcs;

    // 使用 x8 保存数据，x9 保存目标地址，Program Buffer 执行 sh
    if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_ABSTRACTAUTO, 0u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_ABSTRACTCS, 0x00000700u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_PROGBUF0, 0x00849023u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_PROGBUF1, 0x00100073u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_DATA0, address).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_COMMAND, 0x00231009u).ok ||
        !rvswd_debug_wait_abstract_idle(operation, &abstractcs) ||
        ((abstractcs >> 8u) & 0x07u) != 0u ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_DATA0, value).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_COMMAND, 0x00271008u).ok ||
        !rvswd_debug_wait_abstract_idle_timeout(operation, &abstractcs, timeout_us)) {
        return false;
    }

    return ((abstractcs >> 8u) & 0x07u) == 0u;
}

static bool rvswd_flash_wait_ready(struct rvswd_operation *operation,
                                   const struct rvswd_target_profile *profile,
                                   uint32_t *status, uint8_t read_error,
                                   uint8_t timeout_error) {
    uint64_t start = bsp_time_us();

    do {
        if (!rvswd_memory_read32(operation, profile, true, RVSWD_FLASH_STATR_ADDRESS,
                                 status)) {
            operation->flash_code = read_error;
            return false;
        }
        if ((*status & RVSWD_FLASH_STATR_BUSY) == 0u) {
            return true;
        }
        bsp_delay_us(100u);
    } while ((bsp_time_us() - start) < RVSWD_FLASH_ERASE_TIMEOUT_US);

    operation->flash_code = timeout_error;
    return false;
}

static bool rvswd_flash_unlock_main_option_and_fast(
    struct rvswd_operation *operation, uint32_t control) {
    if ((control & (RVSWD_FLASH_CTLR_LOCK | RVSWD_FLASH_CTLR_FAST_LOCK)) == 0u) {
        return true;
    }

    // L103 需要同时解锁主存储区、用户字和快速编程模式
    return rvswd_memory_write32(operation, RVSWD_FLASH_KEYR_ADDRESS, RVSWD_FLASH_KEY1) &&
           rvswd_memory_write32(operation, RVSWD_FLASH_KEYR_ADDRESS, RVSWD_FLASH_KEY2) &&
           rvswd_memory_write32(operation, RVSWD_FLASH_OBKEYR_ADDRESS, RVSWD_FLASH_KEY1) &&
           rvswd_memory_write32(operation, RVSWD_FLASH_OBKEYR_ADDRESS, RVSWD_FLASH_KEY2) &&
           rvswd_memory_write32(operation, RVSWD_FLASH_MODEKEYR_ADDRESS, RVSWD_FLASH_KEY1) &&
           rvswd_memory_write32(operation, RVSWD_FLASH_MODEKEYR_ADDRESS, RVSWD_FLASH_KEY2);
}

static bool rvswd_flash_unlock_main_and_fast(struct rvswd_operation *operation,
                                             uint32_t control) {
    if ((control & RVSWD_FLASH_CTLR_LOCK) != 0u &&
        (!rvswd_memory_write32(operation, RVSWD_FLASH_KEYR_ADDRESS, RVSWD_FLASH_KEY1) ||
         !rvswd_memory_write32(operation, RVSWD_FLASH_KEYR_ADDRESS, RVSWD_FLASH_KEY2))) {
        return false;
    }
    if ((control & RVSWD_FLASH_CTLR_FAST_LOCK) != 0u &&
        (!rvswd_memory_write32(operation, RVSWD_FLASH_MODEKEYR_ADDRESS, RVSWD_FLASH_KEY1) ||
         !rvswd_memory_write32(operation, RVSWD_FLASH_MODEKEYR_ADDRESS, RVSWD_FLASH_KEY2))) {
        return false;
    }
    return true;
}

bool rvswd_flash_erase_all(struct rvswd_operation *operation,
                           const struct rvswd_target_profile *profile) {
    uint32_t control;
    uint32_t idle_control;
    uint32_t status;
    bool unlocked;
    bool success = false;

    operation->flash_code = 0u;
    if (profile == NULL) {
        operation->flash_code = 0x0fu;
        return false;
    }
    if (profile->ch5xx_protocol) {
        return rvswd_flash_ch5xx_erase_all(operation, profile);
    }

    if (!rvswd_flash_wait_ready(operation, profile, &status, 0x11u, 0x12u)) {
        return false;
    }

    if (!rvswd_memory_read32(operation, profile, true, RVSWD_FLASH_CTLR_ADDRESS,
                             &control)) {
        operation->flash_code = 0x13u;
        return false;
    }

    switch (profile->erase_unlock) {
        case RVSWD_FLASH_UNLOCK_MAIN_OPTION_AND_FAST:
            unlocked =
                rvswd_flash_unlock_main_option_and_fast(operation, control);
            break;
        case RVSWD_FLASH_UNLOCK_MAIN_AND_FAST:
            unlocked = rvswd_flash_unlock_main_and_fast(operation, control);
            break;
        default:
            operation->flash_code = 0x0fu;
            return false;
    }
    if (!unlocked) {
        operation->flash_code = 0x14u;
        return false;
    }

    if (!rvswd_memory_read32(operation, profile, true, RVSWD_FLASH_CTLR_ADDRESS,
                             &control)) {
        operation->flash_code = 0x15u;
        return false;
    }
    if ((control & (RVSWD_FLASH_CTLR_LOCK | RVSWD_FLASH_CTLR_FAST_LOCK)) != 0u) {
        operation->flash_code = 0x16u;
        return false;
    }

    idle_control = control & ~(RVSWD_FLASH_CTLR_MER | RVSWD_FLASH_CTLR_STRT);

    // 清除上一次操作遗留的完成和写保护状态，避免误判本次擦除
    if ((status & (RVSWD_FLASH_STATR_EOP | RVSWD_FLASH_STATR_WRPRTERR)) != 0u &&
        !rvswd_memory_write32(operation,
                              RVSWD_FLASH_STATR_ADDRESS,
                              status & (RVSWD_FLASH_STATR_EOP | RVSWD_FLASH_STATR_WRPRTERR))) {
        operation->flash_code = 0x17u;
        goto cleanup;
    }

    if (!rvswd_memory_write32(operation, RVSWD_FLASH_CTLR_ADDRESS, idle_control)) {
        operation->flash_code = 0x18u;
        goto cleanup;
    }
    if (!rvswd_memory_write32(operation, RVSWD_FLASH_CTLR_ADDRESS,
                              idle_control | RVSWD_FLASH_CTLR_MER)) {
        operation->flash_code = 0x19u;
        goto cleanup;
    }
    if (!rvswd_memory_write32(operation,
                              RVSWD_FLASH_CTLR_ADDRESS,
                              idle_control | RVSWD_FLASH_CTLR_MER | RVSWD_FLASH_CTLR_STRT)) {
        operation->flash_code = 0x1au;
        goto cleanup;
    }
    if (!rvswd_flash_wait_ready(operation, profile, &status, 0x1bu, 0x1cu)) {
        goto cleanup;
    }
    if ((status & RVSWD_FLASH_STATR_WRPRTERR) != 0u) {
        operation->flash_code = 0x1du;
        goto cleanup;
    }
    success = true;

cleanup:
    if (!rvswd_memory_write32(operation, RVSWD_FLASH_CTLR_ADDRESS, idle_control)) {
        if (operation->flash_code == 0u) {
            operation->flash_code = 0x1eu;
        }
        success = false;
    }
    return success;
}

bool rvswd_flash_read_protected(struct rvswd_operation *operation,
                                const struct rvswd_target_profile *profile,
                                bool *protected) {
    uint32_t option_status;

    operation->flash_code = 0u;
    if (protected == NULL) {
        operation->flash_code = 0x21u;
        return false;
    }
    if (profile == NULL || profile->ch5xx_protocol) {
        operation->flash_code = 0x22u;
        return false;
    }
    if (!rvswd_memory_read32(operation, profile, true, RVSWD_FLASH_OBR_ADDRESS,
                             &option_status)) {
        operation->flash_code = 0x23u;
        return false;
    }

    *protected = (option_status & RVSWD_FLASH_OBR_READ_PROTECTED) != 0u;
    return true;
}

bool rvswd_flash_write_protected(struct rvswd_operation *operation,
                                 const struct rvswd_target_profile *profile,
                                 bool *protected) {
    uint32_t write_protection;

    operation->flash_code = 0u;
    if (protected == NULL) {
        operation->flash_code = 0x24u;
        return false;
    }
    if (profile == NULL || profile->ch5xx_protocol) {
        operation->flash_code = 0x25u;
        return false;
    }
    if (!rvswd_memory_read32(operation, profile, true, RVSWD_FLASH_WPR_ADDRESS,
                             &write_protection)) {
        operation->flash_code = 0x26u;
        return false;
    }

    *protected = write_protection != 0xffffffffu;
    return true;
}

static bool rvswd_flash_unlock_option_bytes(struct rvswd_operation *operation,
                                            bool unlock_fast_mode) {
    if (!rvswd_memory_write32(operation, RVSWD_FLASH_KEYR_ADDRESS, RVSWD_FLASH_KEY1)) {
        operation->flash_code = 0xa1u;
        return false;
    }
    if (!rvswd_memory_write32(operation, RVSWD_FLASH_KEYR_ADDRESS, RVSWD_FLASH_KEY2)) {
        operation->flash_code = 0xa2u;
        return false;
    }
    if (unlock_fast_mode) {
        if (!rvswd_memory_write32(operation, RVSWD_FLASH_MODEKEYR_ADDRESS,
                                  RVSWD_FLASH_KEY1)) {
            operation->flash_code = 0xa3u;
            return false;
        }
        if (!rvswd_memory_write32(operation, RVSWD_FLASH_MODEKEYR_ADDRESS,
                                  RVSWD_FLASH_KEY2)) {
            operation->flash_code = 0xa4u;
            return false;
        }
    }
    if (!rvswd_memory_write32(operation, RVSWD_FLASH_OBKEYR_ADDRESS,
                              RVSWD_FLASH_KEY1)) {
        operation->flash_code = 0xa5u;
        return false;
    }
    if (!rvswd_memory_write32(operation, RVSWD_FLASH_OBKEYR_ADDRESS,
                              RVSWD_FLASH_KEY2)) {
        operation->flash_code = 0xa6u;
        return false;
    }
    return true;
}

static bool rvswd_flash_write_option_bytes_fast_buffer(
    struct rvswd_operation *operation,
    const struct rvswd_target_profile *profile, const uint32_t *option_words) {
    uint32_t control;
    uint32_t idle_control = 0u;
    uint32_t status;
    bool success = false;

    if (!rvswd_flash_wait_ready(operation, profile, &status, 0x31u, 0x32u) ||
        !rvswd_memory_read32(operation, profile, true, RVSWD_FLASH_CTLR_ADDRESS,
                             &control)) {
        if (operation->flash_code == 0u) {
            operation->flash_code = 0x33u;
        }
        return false;
    }
    if (!rvswd_flash_unlock_option_bytes(operation, true)) {
        if (operation->flash_code == 0u) {
            operation->flash_code = 0x34u;
        }
        return false;
    }
    if (!rvswd_memory_read32(operation, profile, true, RVSWD_FLASH_CTLR_ADDRESS,
                             &control)) {
        operation->flash_code = 0x35u;
        return false;
    }
    if ((control & (RVSWD_FLASH_CTLR_LOCK | RVSWD_FLASH_CTLR_FAST_LOCK)) != 0u ||
        (control & RVSWD_FLASH_CTLR_OPTION_WRITE) == 0u) {
        operation->flash_code = 0x36u;
        return false;
    }

    idle_control = control & ~(RVSWD_FLASH_CTLR_OPTER | RVSWD_FLASH_CTLR_STRT |
                               RVSWD_FLASH_CTLR_FAST_PROGRAM |
                               RVSWD_FLASH_CTLR_BUFFER_LOAD |
                               RVSWD_FLASH_CTLR_BUFFER_RESET);

    // Option Bytes 擦除和重写必须保持完整的 16 字节镜像
    if (!rvswd_memory_write32(operation, RVSWD_FLASH_CTLR_ADDRESS,
                              idle_control | RVSWD_FLASH_CTLR_OPTER) ||
        !rvswd_memory_write32(operation,
                              RVSWD_FLASH_CTLR_ADDRESS,
                              idle_control | RVSWD_FLASH_CTLR_OPTER | RVSWD_FLASH_CTLR_STRT) ||
        !rvswd_flash_wait_ready(operation, profile, &status, 0x36u, 0x37u)) {
        if (operation->flash_code == 0u) {
            operation->flash_code = 0x38u;
        }
        goto cleanup;
    }
    if ((status & RVSWD_FLASH_STATR_WRPRTERR) != 0u) {
        operation->flash_code = 0x39u;
        goto cleanup;
    }

    // 选项字擦除完成后重新解锁快速编程模式，缓冲写入不保持 OPTWRE
    if (!rvswd_memory_read32(operation, profile, true, RVSWD_FLASH_CTLR_ADDRESS,
                             &control) ||
        !rvswd_flash_unlock_main_and_fast(operation, control) ||
        !rvswd_memory_read32(operation, profile, true, RVSWD_FLASH_CTLR_ADDRESS,
                             &control)) {
        operation->flash_code = 0x3au;
        goto cleanup;
    }
    if ((control & (RVSWD_FLASH_CTLR_LOCK | RVSWD_FLASH_CTLR_FAST_LOCK)) != 0u) {
        operation->flash_code = 0x3bu;
        goto cleanup;
    }

    idle_control = control & ~(RVSWD_FLASH_CTLR_OPTION_WRITE |
                               RVSWD_FLASH_CTLR_OPTER | RVSWD_FLASH_CTLR_STRT |
                               RVSWD_FLASH_CTLR_FAST_PROGRAM |
                               RVSWD_FLASH_CTLR_BUFFER_LOAD |
                               RVSWD_FLASH_CTLR_BUFFER_RESET);

    if (!rvswd_memory_write32(operation, RVSWD_FLASH_CTLR_ADDRESS,
                              idle_control | RVSWD_FLASH_CTLR_FAST_PROGRAM) ||
        !rvswd_memory_write32(operation,
                              RVSWD_FLASH_CTLR_ADDRESS,
                              idle_control | RVSWD_FLASH_CTLR_FAST_PROGRAM |
                                  RVSWD_FLASH_CTLR_BUFFER_RESET) ||
        !rvswd_flash_wait_ready(operation, profile, &status, 0x3au, 0x3bu) ||
        !rvswd_memory_write32(operation, RVSWD_FLASH_CTLR_ADDRESS, idle_control)) {
        if (operation->flash_code == 0u) {
            operation->flash_code = 0x3cu;
        }
        goto cleanup;
    }

    for (uint32_t index = 0u; index < RVSWD_OPTION_BYTES_WORD_COUNT; ++index) {
        if (!rvswd_memory_write32(operation, RVSWD_FLASH_CTLR_ADDRESS,
                                  idle_control | RVSWD_FLASH_CTLR_FAST_PROGRAM) ||
            !rvswd_memory_write32(operation, profile->option_base + index * 4u,
                                  option_words[index]) ||
            !rvswd_memory_write32(operation,
                                  RVSWD_FLASH_CTLR_ADDRESS,
                                  idle_control | RVSWD_FLASH_CTLR_FAST_PROGRAM |
                                      RVSWD_FLASH_CTLR_BUFFER_LOAD) ||
            !rvswd_flash_wait_ready(operation, profile, &status, 0x3du,
                                    0x3eu) ||
            !rvswd_memory_write32(operation, RVSWD_FLASH_CTLR_ADDRESS, idle_control)) {
            if (operation->flash_code == 0u) {
                operation->flash_code = 0x3fu;
            }
            goto cleanup;
        }
    }

    if (!rvswd_memory_write32(operation, RVSWD_FLASH_CTLR_ADDRESS,
                              idle_control | RVSWD_FLASH_CTLR_FAST_PROGRAM) ||
        !rvswd_memory_write32(operation, RVSWD_FLASH_ADDR_ADDRESS, profile->option_base) ||
        !rvswd_memory_write32(operation,
                              RVSWD_FLASH_CTLR_ADDRESS,
                              idle_control | RVSWD_FLASH_CTLR_FAST_PROGRAM | RVSWD_FLASH_CTLR_STRT) ||
        !rvswd_flash_wait_ready(operation, profile, &status, 0x40u, 0x41u)) {
        if (operation->flash_code == 0u) {
            operation->flash_code = 0x42u;
        }
        goto cleanup;
    }
    if ((status & RVSWD_FLASH_STATR_WRPRTERR) != 0u) {
        operation->flash_code = 0x43u;
        goto cleanup;
    }
    success = true;

cleanup:
    if (!rvswd_memory_write32(operation,
                              RVSWD_FLASH_CTLR_ADDRESS,
                              (idle_control & ~RVSWD_FLASH_CTLR_OPTION_WRITE) |
                                  RVSWD_FLASH_CTLR_LOCK | RVSWD_FLASH_CTLR_FAST_LOCK)) {
        if (operation->flash_code == 0u) {
            operation->flash_code = 0x44u;
        }
        success = false;
    }
    return success;
}

static bool rvswd_flash_write_option_bytes_halfword(
    struct rvswd_operation *operation,
    const struct rvswd_target_profile *profile, const uint32_t *option_words) {
    uint32_t control;
    uint32_t idle_control = 0u;
    uint32_t status;
    bool success = false;

    if (!rvswd_flash_wait_ready(operation, profile, &status, 0x51u, 0x52u) ||
        !rvswd_memory_read32(operation, profile, true, RVSWD_FLASH_CTLR_ADDRESS,
                             &control)) {
        if (operation->flash_code == 0u) {
            operation->flash_code = 0x53u;
        }
        return false;
    }
    if (!rvswd_flash_unlock_option_bytes(operation, true) ||
        !rvswd_memory_read32(operation, profile, true, RVSWD_FLASH_CTLR_ADDRESS,
                             &control)) {
        if (operation->flash_code == 0u) {
            operation->flash_code = 0x54u;
        }
        return false;
    }
    if ((control & (RVSWD_FLASH_CTLR_LOCK | RVSWD_FLASH_CTLR_FAST_LOCK)) != 0u ||
        (control & RVSWD_FLASH_CTLR_OPTION_WRITE) == 0u) {
        operation->flash_code = 0x55u;
        return false;
    }

    idle_control = control & ~(RVSWD_FLASH_CTLR_OPTION_PROGRAM |
                               RVSWD_FLASH_CTLR_OPTER | RVSWD_FLASH_CTLR_STRT);

    // Option Bytes 擦除和重写必须保持完整的 16 字节镜像
    if (!rvswd_memory_write32(operation, RVSWD_FLASH_CTLR_ADDRESS,
                              idle_control | RVSWD_FLASH_CTLR_OPTER) ||
        !rvswd_memory_write32(operation,
                              RVSWD_FLASH_CTLR_ADDRESS,
                              idle_control | RVSWD_FLASH_CTLR_OPTER | RVSWD_FLASH_CTLR_STRT) ||
        !rvswd_flash_wait_ready(operation, profile, &status, 0x56u, 0x57u)) {
        if (operation->flash_code == 0u) {
            operation->flash_code = 0x58u;
        }
        goto cleanup;
    }
    if ((status & RVSWD_FLASH_STATR_WRPRTERR) != 0u) {
        operation->flash_code = 0x59u;
        goto cleanup;
    }

    // Option Bytes 擦除后重新解锁主存储区、快速模式和 Option Bytes
    if (!rvswd_memory_read32(operation, profile, true, RVSWD_FLASH_CTLR_ADDRESS,
                             &control) ||
        !rvswd_flash_unlock_option_bytes(operation, true) ||
        !rvswd_memory_read32(operation, profile, true, RVSWD_FLASH_CTLR_ADDRESS,
                             &control)) {
        operation->flash_code = 0x5au;
        goto cleanup;
    }
    if ((control & (RVSWD_FLASH_CTLR_LOCK | RVSWD_FLASH_CTLR_FAST_LOCK)) != 0u ||
        (control & RVSWD_FLASH_CTLR_OPTION_WRITE) == 0u) {
        operation->flash_code = 0x5bu;
        goto cleanup;
    }

    idle_control = control & ~(RVSWD_FLASH_CTLR_OPTION_PROGRAM |
                               RVSWD_FLASH_CTLR_OPTER | RVSWD_FLASH_CTLR_STRT);

    for (uint32_t index = 0u; index < RVSWD_OPTION_BYTES_WORD_COUNT * 2u; ++index) {
        uint16_t value = (uint16_t)(option_words[index / 2u] >>
                                    ((index & 1u) * 16u));

        if (!rvswd_memory_write32(operation,
                                  RVSWD_FLASH_CTLR_ADDRESS,
                                  idle_control | RVSWD_FLASH_CTLR_OPTION_PROGRAM) ||
            !rvswd_memory_write16(operation,
                                  profile->option_base + index * 2u, value,
                                  rvswd_flash_abstract_timeout_us) ||
            !rvswd_flash_wait_ready(operation, profile, &status, 0x5cu,
                                    0x5du)) {
            if (operation->flash_code == 0u) {
                operation->flash_code = 0x5eu;
            }
            goto cleanup;
        }
        if ((status & RVSWD_FLASH_STATR_WRPRTERR) != 0u) {
            operation->flash_code = 0x5fu;
            goto cleanup;
        }
    }
    success = true;

cleanup:
    if (!rvswd_memory_write32(operation,
                              RVSWD_FLASH_CTLR_ADDRESS,
                              (idle_control & ~RVSWD_FLASH_CTLR_OPTION_WRITE) |
                                  RVSWD_FLASH_CTLR_LOCK | RVSWD_FLASH_CTLR_FAST_LOCK)) {
        if (operation->flash_code == 0u) {
            operation->flash_code = 0x60u;
        }
        success = false;
    }
    return success;
}

static bool rvswd_flash_unprotect_option_bytes(
    struct rvswd_operation *operation,
    const struct rvswd_target_profile *profile) {
    uint32_t control;
    uint32_t idle_control = 0u;
    uint32_t status;
    bool success = false;

    if (!rvswd_flash_wait_ready(operation, profile, &status, 0x61u, 0x62u) ||
        !rvswd_memory_read32(operation, profile, true, RVSWD_FLASH_CTLR_ADDRESS,
                             &control)) {
        if (operation->flash_code == 0u) {
            operation->flash_code = 0x63u;
        }
        return false;
    }
    if (!rvswd_flash_unlock_option_bytes(operation, true) ||
        !rvswd_memory_read32(operation, profile, true, RVSWD_FLASH_CTLR_ADDRESS,
                             &control)) {
        if (operation->flash_code == 0u) {
            operation->flash_code = 0x64u;
        }
        return false;
    }
    if ((control & RVSWD_FLASH_CTLR_LOCK) != 0u ||
        (control & RVSWD_FLASH_CTLR_OPTION_WRITE) == 0u) {
        operation->flash_code = 0x65u;
        return false;
    }

    idle_control = control & ~(RVSWD_FLASH_CTLR_OPTION_PROGRAM |
                               RVSWD_FLASH_CTLR_OPTER | RVSWD_FLASH_CTLR_STRT);
    if (!rvswd_memory_write32(operation, RVSWD_FLASH_CTLR_ADDRESS,
                              idle_control | RVSWD_FLASH_CTLR_OPTER) ||
        !rvswd_memory_write32(operation,
                              RVSWD_FLASH_CTLR_ADDRESS,
                              idle_control | RVSWD_FLASH_CTLR_OPTER | RVSWD_FLASH_CTLR_STRT) ||
        !rvswd_flash_wait_ready(operation, profile, &status, 0x66u, 0x67u)) {
        if (operation->flash_code == 0u) {
            operation->flash_code = 0x68u;
        }
        goto cleanup;
    }
    if ((status & RVSWD_FLASH_STATR_WRPRTERR) != 0u) {
        operation->flash_code = 0x69u;
        goto cleanup;
    }

    // 解除读保护只恢复 RDP，保留 Option Bytes 擦除后的默认状态
    if (!rvswd_memory_read32(operation, profile, true, RVSWD_FLASH_CTLR_ADDRESS,
                             &control) ||
        !rvswd_flash_unlock_option_bytes(operation, true) ||
        !rvswd_memory_read32(operation, profile, true, RVSWD_FLASH_CTLR_ADDRESS,
                             &control)) {
        operation->flash_code = 0x6au;
        goto cleanup;
    }
    if ((control & RVSWD_FLASH_CTLR_LOCK) != 0u ||
        (control & RVSWD_FLASH_CTLR_OPTION_WRITE) == 0u) {
        operation->flash_code = 0x6bu;
        goto cleanup;
    }

    idle_control = control & ~(RVSWD_FLASH_CTLR_OPTION_PROGRAM |
                               RVSWD_FLASH_CTLR_OPTER | RVSWD_FLASH_CTLR_STRT);
    if (!rvswd_memory_write32(operation,
                              RVSWD_FLASH_CTLR_ADDRESS,
                              idle_control | RVSWD_FLASH_CTLR_OPTION_PROGRAM)) {
        operation->flash_code = 0x71u;
        goto cleanup;
    }
    if (!rvswd_memory_write16(operation, profile->option_base,
                              RVSWD_OPTION_RDP_UNPROTECTED,
                              RVSWD_FLASH_ERASE_TIMEOUT_US)) {
        if (operation->flash_code == 0u) {
            operation->flash_code = 0x72u;
        }
        goto cleanup;
    }
    if (!rvswd_flash_wait_ready(operation, profile, &status, 0x6cu, 0x6du)) {
        if (operation->flash_code == 0u) {
            operation->flash_code = 0x73u;
        }
        goto cleanup;
    }
    if ((status & RVSWD_FLASH_STATR_WRPRTERR) != 0u) {
        operation->flash_code = 0x6fu;
        goto cleanup;
    }
    success = true;

cleanup:
    if (!rvswd_memory_write32(operation,
                              RVSWD_FLASH_CTLR_ADDRESS,
                              (idle_control & ~RVSWD_FLASH_CTLR_OPTION_WRITE) |
                                  RVSWD_FLASH_CTLR_LOCK)) {
        if (operation->flash_code == 0u) {
            operation->flash_code = 0x70u;
        }
        success = false;
    }
    return success;
}

bool rvswd_flash_set_read_protected(struct rvswd_operation *operation,
                                    const struct rvswd_target_profile *profile,
                                    bool protected) {
    uint32_t option_words[RVSWD_OPTION_BYTES_WORD_COUNT];
    bool current;

    if (profile == NULL || profile->ch5xx_protocol) {
        operation->flash_code = 0x22u;
        return false;
    }
    if (!rvswd_flash_read_protected(operation, profile, &current)) {
        return false;
    }
    if (current == protected) {
        return true;
    }

    if (!protected && profile->option_write == RVSWD_OPTION_WRITE_FAST_BUFFER) {
        if (!rvswd_flash_unprotect_option_bytes(operation, profile)) {
            return false;
        }
    } else {
        for (uint32_t index = 0u; index < RVSWD_OPTION_BYTES_WORD_COUNT; ++index) {
            if (!rvswd_memory_read32(operation, profile, true,
                                     profile->option_base + index * 4u,
                                     &option_words[index])) {
                operation->flash_code = 0x45u;
                return false;
            }
        }

        option_words[0] = (option_words[0] & 0xffff0000u) |
                          (protected ? RVSWD_OPTION_RDP_PROTECTED
                                     : RVSWD_OPTION_RDP_UNPROTECTED);
        switch (profile->option_write) {
            case RVSWD_OPTION_WRITE_FAST_BUFFER:
                if (!rvswd_flash_write_option_bytes_fast_buffer(
                        operation, profile, option_words)) {
                    return false;
                }
                break;
            case RVSWD_OPTION_WRITE_HALFWORD:
                if (!rvswd_flash_write_option_bytes_halfword(
                        operation, profile, option_words)) {
                    return false;
                }
                break;
            default:
                operation->flash_code = 0x5fu;
                return false;
        }
    }

    // 解除读保护时目标硬件会自动整片擦除主存储区，复位后 OBR 才加载新状态
    if (!rvswd_reset_and_halt(operation)) {
        operation->flash_code = 0x46u;
        return false;
    }
    if (!rvswd_flash_read_protected(operation, profile, &current)) {
        return false;
    }
    if (current != protected) {
        operation->flash_code = 0x47u;
        return false;
    }
    return true;
}
