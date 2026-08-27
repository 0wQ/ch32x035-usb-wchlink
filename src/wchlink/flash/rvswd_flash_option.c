#include "wchlink/flash/rvswd_flash.h"
#include "wchlink/flash/rvswd_flash_ch32_internal.h"
#include "wchlink/rvswd/rvswd_debug.h"
#include "wchlink/rvswd/rvswd_memory.h"
#include "wchlink/rvswd/rvswd_operation.h"
#include "wchlink/rvswd/rvswd_reset.h"
#include "wchlink/rvswd/rvswd_types.h"

#include <stddef.h>

// Option Bytes backend 独占保护状态、完整镜像重写和复位后确认流程
static const uint32_t rvswd_flash_option_address_register = 0x40022014u;
static const uint32_t rvswd_flash_option_status_register = 0x4002201cu;
static const uint32_t rvswd_flash_write_protection_register = 0x40022020u;

static const uint32_t rvswd_flash_option_control_program = 1u << 4u;
static const uint32_t rvswd_flash_option_control_erase = 1u << 5u;
static const uint32_t rvswd_flash_option_control_write = 1u << 9u;
static const uint32_t rvswd_flash_option_control_fast_program = 1u << 16u;
static const uint32_t rvswd_flash_option_control_buffer_load = 1u << 18u;
static const uint32_t rvswd_flash_option_control_buffer_reset = 1u << 19u;
static const uint32_t rvswd_flash_option_status_read_protected = 1u << 1u;

static const uint16_t rvswd_flash_option_rdp_protected = 0x00ffu;
static const uint16_t rvswd_flash_option_rdp_unprotected = 0x5aa5u;
static const uint32_t rvswd_flash_option_abstract_timeout_us = 10000u;

enum {
    RVSWD_FLASH_OPTION_WORD_COUNT = 4u,
};

enum rvswd_flash_option_error {
    RVSWD_FLASH_OPTION_ERROR_READ_OUTPUT = 0x21u,
    RVSWD_FLASH_OPTION_ERROR_UNSUPPORTED_TARGET = 0x22u,
    RVSWD_FLASH_OPTION_ERROR_READ_STATUS = 0x23u,
    RVSWD_FLASH_OPTION_ERROR_WRITE_OUTPUT = 0x24u,
    RVSWD_FLASH_OPTION_ERROR_WRITE_TARGET = 0x25u,
    RVSWD_FLASH_OPTION_ERROR_READ_WRITE_PROTECTION = 0x26u,
    RVSWD_FLASH_OPTION_ERROR_FAST_INITIAL_STATUS_READ = 0x31u,
    RVSWD_FLASH_OPTION_ERROR_FAST_INITIAL_STATUS_TIMEOUT = 0x32u,
    RVSWD_FLASH_OPTION_ERROR_FAST_CONTROL_READ = 0x33u,
    RVSWD_FLASH_OPTION_ERROR_FAST_UNLOCK = 0x34u,
    RVSWD_FLASH_OPTION_ERROR_FAST_UNLOCKED_CONTROL_READ = 0x35u,
    RVSWD_FLASH_OPTION_ERROR_FAST_REMAINS_LOCKED = 0x36u,
    RVSWD_FLASH_OPTION_ERROR_FAST_ERASE_STATUS_READ = 0x36u,
    RVSWD_FLASH_OPTION_ERROR_FAST_ERASE_STATUS_TIMEOUT = 0x37u,
    RVSWD_FLASH_OPTION_ERROR_FAST_ERASE = 0x38u,
    RVSWD_FLASH_OPTION_ERROR_FAST_ERASE_PROTECTED = 0x39u,
    RVSWD_FLASH_OPTION_ERROR_FAST_REUNLOCK = 0x3au,
    RVSWD_FLASH_OPTION_ERROR_FAST_RESET_STATUS_READ = 0x3au,
    RVSWD_FLASH_OPTION_ERROR_FAST_RELOCKED = 0x3bu,
    RVSWD_FLASH_OPTION_ERROR_FAST_RESET_STATUS_TIMEOUT = 0x3bu,
    RVSWD_FLASH_OPTION_ERROR_FAST_RESET_BUFFER = 0x3cu,
    RVSWD_FLASH_OPTION_ERROR_FAST_LOAD_STATUS_READ = 0x3du,
    RVSWD_FLASH_OPTION_ERROR_FAST_LOAD_STATUS_TIMEOUT = 0x3eu,
    RVSWD_FLASH_OPTION_ERROR_FAST_LOAD_BUFFER = 0x3fu,
    RVSWD_FLASH_OPTION_ERROR_FAST_COMMIT_STATUS_READ = 0x40u,
    RVSWD_FLASH_OPTION_ERROR_FAST_COMMIT_STATUS_TIMEOUT = 0x41u,
    RVSWD_FLASH_OPTION_ERROR_FAST_COMMIT = 0x42u,
    RVSWD_FLASH_OPTION_ERROR_FAST_COMMIT_PROTECTED = 0x43u,
    RVSWD_FLASH_OPTION_ERROR_FAST_CLEANUP = 0x44u,
    RVSWD_FLASH_OPTION_ERROR_READ_IMAGE = 0x45u,
    RVSWD_FLASH_OPTION_ERROR_RESET_AND_HALT = 0x46u,
    RVSWD_FLASH_OPTION_ERROR_VERIFY = 0x47u,
    RVSWD_FLASH_OPTION_ERROR_CONFIG_INPUT = 0x48u,
    RVSWD_FLASH_OPTION_ERROR_HALFWORD_INITIAL_STATUS_READ = 0x51u,
    RVSWD_FLASH_OPTION_ERROR_HALFWORD_INITIAL_STATUS_TIMEOUT = 0x52u,
    RVSWD_FLASH_OPTION_ERROR_HALFWORD_CONTROL_READ = 0x53u,
    RVSWD_FLASH_OPTION_ERROR_HALFWORD_UNLOCK = 0x54u,
    RVSWD_FLASH_OPTION_ERROR_HALFWORD_REMAINS_LOCKED = 0x55u,
    RVSWD_FLASH_OPTION_ERROR_HALFWORD_ERASE_STATUS_READ = 0x56u,
    RVSWD_FLASH_OPTION_ERROR_HALFWORD_ERASE_STATUS_TIMEOUT = 0x57u,
    RVSWD_FLASH_OPTION_ERROR_HALFWORD_ERASE = 0x58u,
    RVSWD_FLASH_OPTION_ERROR_HALFWORD_ERASE_PROTECTED = 0x59u,
    RVSWD_FLASH_OPTION_ERROR_HALFWORD_REUNLOCK = 0x5au,
    RVSWD_FLASH_OPTION_ERROR_HALFWORD_RELOCKED = 0x5bu,
    RVSWD_FLASH_OPTION_ERROR_HALFWORD_WRITE_STATUS_READ = 0x5cu,
    RVSWD_FLASH_OPTION_ERROR_HALFWORD_WRITE_STATUS_TIMEOUT = 0x5du,
    RVSWD_FLASH_OPTION_ERROR_HALFWORD_WRITE = 0x5eu,
    RVSWD_FLASH_OPTION_ERROR_HALFWORD_WRITE_PROTECTED = 0x5fu,
    RVSWD_FLASH_OPTION_ERROR_WRITE_MODE = 0x5fu,
    RVSWD_FLASH_OPTION_ERROR_HALFWORD_CLEANUP = 0x60u,
    RVSWD_FLASH_OPTION_ERROR_UNPROTECT_INITIAL_STATUS_READ = 0x61u,
    RVSWD_FLASH_OPTION_ERROR_UNPROTECT_INITIAL_STATUS_TIMEOUT = 0x62u,
    RVSWD_FLASH_OPTION_ERROR_UNPROTECT_CONTROL_READ = 0x63u,
    RVSWD_FLASH_OPTION_ERROR_UNPROTECT_UNLOCK = 0x64u,
    RVSWD_FLASH_OPTION_ERROR_UNPROTECT_REMAINS_LOCKED = 0x65u,
    RVSWD_FLASH_OPTION_ERROR_UNPROTECT_ERASE_STATUS_READ = 0x66u,
    RVSWD_FLASH_OPTION_ERROR_UNPROTECT_ERASE_STATUS_TIMEOUT = 0x67u,
    RVSWD_FLASH_OPTION_ERROR_UNPROTECT_ERASE = 0x68u,
    RVSWD_FLASH_OPTION_ERROR_UNPROTECT_ERASE_PROTECTED = 0x69u,
    RVSWD_FLASH_OPTION_ERROR_UNPROTECT_REUNLOCK = 0x6au,
    RVSWD_FLASH_OPTION_ERROR_UNPROTECT_RELOCKED = 0x6bu,
    RVSWD_FLASH_OPTION_ERROR_UNPROTECT_FINAL_STATUS_READ = 0x6cu,
    RVSWD_FLASH_OPTION_ERROR_UNPROTECT_FINAL_STATUS_TIMEOUT = 0x6du,
    RVSWD_FLASH_OPTION_ERROR_UNPROTECT_WRITE_PROTECTED = 0x6fu,
    RVSWD_FLASH_OPTION_ERROR_UNPROTECT_CLEANUP = 0x70u,
    RVSWD_FLASH_OPTION_ERROR_UNPROTECT_ENTER_PROGRAM = 0x71u,
    RVSWD_FLASH_OPTION_ERROR_UNPROTECT_WRITE_RDP = 0x72u,
    RVSWD_FLASH_OPTION_ERROR_UNPROTECT_WAIT = 0x73u,
    RVSWD_FLASH_OPTION_ERROR_UNLOCK_KEY1 = 0xa1u,
    RVSWD_FLASH_OPTION_ERROR_UNLOCK_KEY2 = 0xa2u,
    RVSWD_FLASH_OPTION_ERROR_UNLOCK_FAST_KEY1 = 0xa3u,
    RVSWD_FLASH_OPTION_ERROR_UNLOCK_FAST_KEY2 = 0xa4u,
    RVSWD_FLASH_OPTION_ERROR_UNLOCK_OPTION_KEY1 = 0xa5u,
    RVSWD_FLASH_OPTION_ERROR_UNLOCK_OPTION_KEY2 = 0xa6u,
};

static bool rvswd_flash_option_write16(struct rvswd_operation *operation,
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

static uint16_t rvswd_flash_option_encode_byte(uint8_t value) {
    return (uint16_t)value | (uint16_t)((uint16_t)(~value) << 8u);
}

bool rvswd_flash_read_protected(struct rvswd_operation *operation,
                                const struct rvswd_target_profile *profile,
                                bool *protected) {
    uint32_t option_status;

    operation->flash_code = 0u;
    if (protected == NULL) {
        operation->flash_code = RVSWD_FLASH_OPTION_ERROR_READ_OUTPUT;
        return false;
    }
    if (profile == NULL || profile->ch5xx_protocol) {
        operation->flash_code = RVSWD_FLASH_OPTION_ERROR_UNSUPPORTED_TARGET;
        return false;
    }
    if (!rvswd_memory_read32(operation, profile, true,
                             rvswd_flash_option_status_register,
                             &option_status)) {
        operation->flash_code = RVSWD_FLASH_OPTION_ERROR_READ_STATUS;
        return false;
    }

    *protected =
        (option_status & rvswd_flash_option_status_read_protected) != 0u;
    return true;
}

bool rvswd_flash_write_protected(struct rvswd_operation *operation,
                                 const struct rvswd_target_profile *profile,
                                 bool *protected) {
    uint32_t write_protection;

    operation->flash_code = 0u;
    if (protected == NULL) {
        operation->flash_code = RVSWD_FLASH_OPTION_ERROR_WRITE_OUTPUT;
        return false;
    }
    if (profile == NULL || profile->ch5xx_protocol) {
        operation->flash_code = RVSWD_FLASH_OPTION_ERROR_WRITE_TARGET;
        return false;
    }
    if (!rvswd_memory_read32(operation, profile, true,
                             rvswd_flash_write_protection_register,
                             &write_protection)) {
        operation->flash_code =
            RVSWD_FLASH_OPTION_ERROR_READ_WRITE_PROTECTION;
        return false;
    }

    *protected = write_protection != 0xffffffffu;
    return true;
}

static bool rvswd_flash_option_unlock(struct rvswd_operation *operation,
                                      bool unlock_fast_mode) {
    if (!rvswd_memory_write32(operation, rvswd_flash_ch32_key_register,
                              rvswd_flash_ch32_key1)) {
        operation->flash_code = RVSWD_FLASH_OPTION_ERROR_UNLOCK_KEY1;
        return false;
    }
    if (!rvswd_memory_write32(operation, rvswd_flash_ch32_key_register,
                              rvswd_flash_ch32_key2)) {
        operation->flash_code = RVSWD_FLASH_OPTION_ERROR_UNLOCK_KEY2;
        return false;
    }
    if (unlock_fast_mode) {
        if (!rvswd_memory_write32(operation,
                                  rvswd_flash_ch32_mode_key_register,
                                  rvswd_flash_ch32_key1)) {
            operation->flash_code =
                RVSWD_FLASH_OPTION_ERROR_UNLOCK_FAST_KEY1;
            return false;
        }
        if (!rvswd_memory_write32(operation,
                                  rvswd_flash_ch32_mode_key_register,
                                  rvswd_flash_ch32_key2)) {
            operation->flash_code =
                RVSWD_FLASH_OPTION_ERROR_UNLOCK_FAST_KEY2;
            return false;
        }
    }
    if (!rvswd_memory_write32(operation,
                              rvswd_flash_ch32_option_key_register,
                              rvswd_flash_ch32_key1)) {
        operation->flash_code = RVSWD_FLASH_OPTION_ERROR_UNLOCK_OPTION_KEY1;
        return false;
    }
    if (!rvswd_memory_write32(operation,
                              rvswd_flash_ch32_option_key_register,
                              rvswd_flash_ch32_key2)) {
        operation->flash_code = RVSWD_FLASH_OPTION_ERROR_UNLOCK_OPTION_KEY2;
        return false;
    }
    return true;
}

static bool rvswd_flash_option_write_fast_buffer(
    struct rvswd_operation *operation,
    const struct rvswd_target_profile *profile, const uint32_t *option_words) {
    uint32_t control;
    uint32_t idle_control = 0u;
    uint32_t status;
    bool success = false;

    if (!rvswd_flash_ch32_wait_ready(
            operation, profile, &status,
            RVSWD_FLASH_OPTION_ERROR_FAST_INITIAL_STATUS_READ,
            RVSWD_FLASH_OPTION_ERROR_FAST_INITIAL_STATUS_TIMEOUT) ||
        !rvswd_memory_read32(operation, profile, true,
                             rvswd_flash_ch32_control_register, &control)) {
        if (operation->flash_code == 0u) {
            operation->flash_code =
                RVSWD_FLASH_OPTION_ERROR_FAST_CONTROL_READ;
        }
        return false;
    }
    if (!rvswd_flash_option_unlock(operation, true)) {
        if (operation->flash_code == 0u) {
            operation->flash_code = RVSWD_FLASH_OPTION_ERROR_FAST_UNLOCK;
        }
        return false;
    }
    if (!rvswd_memory_read32(operation, profile, true,
                             rvswd_flash_ch32_control_register, &control)) {
        operation->flash_code =
            RVSWD_FLASH_OPTION_ERROR_FAST_UNLOCKED_CONTROL_READ;
        return false;
    }
    if ((control & (rvswd_flash_ch32_control_lock |
                    rvswd_flash_ch32_control_fast_lock)) != 0u ||
        (control & rvswd_flash_option_control_write) == 0u) {
        operation->flash_code = RVSWD_FLASH_OPTION_ERROR_FAST_REMAINS_LOCKED;
        return false;
    }

    idle_control = control & ~(rvswd_flash_option_control_erase |
                               rvswd_flash_ch32_control_start |
                               rvswd_flash_option_control_fast_program |
                               rvswd_flash_option_control_buffer_load |
                               rvswd_flash_option_control_buffer_reset);

    // Option Bytes 擦除和重写必须保持完整的 16 字节镜像
    if (!rvswd_memory_write32(
            operation, rvswd_flash_ch32_control_register,
            idle_control | rvswd_flash_option_control_erase) ||
        !rvswd_memory_write32(
            operation, rvswd_flash_ch32_control_register,
            idle_control | rvswd_flash_option_control_erase |
                rvswd_flash_ch32_control_start) ||
        !rvswd_flash_ch32_wait_ready(
            operation, profile, &status,
            RVSWD_FLASH_OPTION_ERROR_FAST_ERASE_STATUS_READ,
            RVSWD_FLASH_OPTION_ERROR_FAST_ERASE_STATUS_TIMEOUT)) {
        if (operation->flash_code == 0u) {
            operation->flash_code = RVSWD_FLASH_OPTION_ERROR_FAST_ERASE;
        }
        goto cleanup;
    }
    if ((status & rvswd_flash_ch32_status_write_protection_error) != 0u) {
        operation->flash_code =
            RVSWD_FLASH_OPTION_ERROR_FAST_ERASE_PROTECTED;
        goto cleanup;
    }

    // 选项字擦除完成后重新解锁快速编程模式，缓冲写入不保持 OPTWRE
    if (!rvswd_memory_read32(operation, profile, true,
                             rvswd_flash_ch32_control_register, &control) ||
        !rvswd_flash_ch32_unlock_main_and_fast(operation, control) ||
        !rvswd_memory_read32(operation, profile, true,
                             rvswd_flash_ch32_control_register, &control)) {
        operation->flash_code = RVSWD_FLASH_OPTION_ERROR_FAST_REUNLOCK;
        goto cleanup;
    }
    if ((control & (rvswd_flash_ch32_control_lock |
                    rvswd_flash_ch32_control_fast_lock)) != 0u) {
        operation->flash_code = RVSWD_FLASH_OPTION_ERROR_FAST_RELOCKED;
        goto cleanup;
    }

    idle_control = control & ~(rvswd_flash_option_control_write |
                               rvswd_flash_option_control_erase |
                               rvswd_flash_ch32_control_start |
                               rvswd_flash_option_control_fast_program |
                               rvswd_flash_option_control_buffer_load |
                               rvswd_flash_option_control_buffer_reset);

    if (!rvswd_memory_write32(
            operation, rvswd_flash_ch32_control_register,
            idle_control | rvswd_flash_option_control_fast_program) ||
        !rvswd_memory_write32(
            operation, rvswd_flash_ch32_control_register,
            idle_control | rvswd_flash_option_control_fast_program |
                rvswd_flash_option_control_buffer_reset) ||
        !rvswd_flash_ch32_wait_ready(
            operation, profile, &status,
            RVSWD_FLASH_OPTION_ERROR_FAST_RESET_STATUS_READ,
            RVSWD_FLASH_OPTION_ERROR_FAST_RESET_STATUS_TIMEOUT) ||
        !rvswd_memory_write32(operation,
                              rvswd_flash_ch32_control_register,
                              idle_control)) {
        if (operation->flash_code == 0u) {
            operation->flash_code =
                RVSWD_FLASH_OPTION_ERROR_FAST_RESET_BUFFER;
        }
        goto cleanup;
    }

    for (uint32_t index = 0u; index < RVSWD_FLASH_OPTION_WORD_COUNT; ++index) {
        if (!rvswd_memory_write32(
                operation, rvswd_flash_ch32_control_register,
                idle_control | rvswd_flash_option_control_fast_program) ||
            !rvswd_memory_write32(operation,
                                  profile->option_base + index * 4u,
                                  option_words[index]) ||
            !rvswd_memory_write32(
                operation, rvswd_flash_ch32_control_register,
                idle_control | rvswd_flash_option_control_fast_program |
                    rvswd_flash_option_control_buffer_load) ||
            !rvswd_flash_ch32_wait_ready(
                operation, profile, &status,
                RVSWD_FLASH_OPTION_ERROR_FAST_LOAD_STATUS_READ,
                RVSWD_FLASH_OPTION_ERROR_FAST_LOAD_STATUS_TIMEOUT) ||
            !rvswd_memory_write32(operation,
                                  rvswd_flash_ch32_control_register,
                                  idle_control)) {
            if (operation->flash_code == 0u) {
                operation->flash_code =
                    RVSWD_FLASH_OPTION_ERROR_FAST_LOAD_BUFFER;
            }
            goto cleanup;
        }
    }

    if (!rvswd_memory_write32(
            operation, rvswd_flash_ch32_control_register,
            idle_control | rvswd_flash_option_control_fast_program) ||
        !rvswd_memory_write32(operation,
                              rvswd_flash_option_address_register,
                              profile->option_base) ||
        !rvswd_memory_write32(
            operation, rvswd_flash_ch32_control_register,
            idle_control | rvswd_flash_option_control_fast_program |
                rvswd_flash_ch32_control_start) ||
        !rvswd_flash_ch32_wait_ready(
            operation, profile, &status,
            RVSWD_FLASH_OPTION_ERROR_FAST_COMMIT_STATUS_READ,
            RVSWD_FLASH_OPTION_ERROR_FAST_COMMIT_STATUS_TIMEOUT)) {
        if (operation->flash_code == 0u) {
            operation->flash_code = RVSWD_FLASH_OPTION_ERROR_FAST_COMMIT;
        }
        goto cleanup;
    }
    if ((status & rvswd_flash_ch32_status_write_protection_error) != 0u) {
        operation->flash_code =
            RVSWD_FLASH_OPTION_ERROR_FAST_COMMIT_PROTECTED;
        goto cleanup;
    }
    success = true;

cleanup:
    if (!rvswd_memory_write32(
            operation, rvswd_flash_ch32_control_register,
            (idle_control & ~rvswd_flash_option_control_write) |
                rvswd_flash_ch32_control_lock |
                rvswd_flash_ch32_control_fast_lock)) {
        if (operation->flash_code == 0u) {
            operation->flash_code = RVSWD_FLASH_OPTION_ERROR_FAST_CLEANUP;
        }
        success = false;
    }
    return success;
}

static bool rvswd_flash_option_write_halfword(
    struct rvswd_operation *operation,
    const struct rvswd_target_profile *profile, const uint32_t *option_words) {
    uint32_t control;
    uint32_t idle_control = 0u;
    uint32_t status;
    bool success = false;

    if (!rvswd_flash_ch32_wait_ready(
            operation, profile, &status,
            RVSWD_FLASH_OPTION_ERROR_HALFWORD_INITIAL_STATUS_READ,
            RVSWD_FLASH_OPTION_ERROR_HALFWORD_INITIAL_STATUS_TIMEOUT) ||
        !rvswd_memory_read32(operation, profile, true,
                             rvswd_flash_ch32_control_register, &control)) {
        if (operation->flash_code == 0u) {
            operation->flash_code =
                RVSWD_FLASH_OPTION_ERROR_HALFWORD_CONTROL_READ;
        }
        return false;
    }
    if (!rvswd_flash_option_unlock(operation, true) ||
        !rvswd_memory_read32(operation, profile, true,
                             rvswd_flash_ch32_control_register, &control)) {
        if (operation->flash_code == 0u) {
            operation->flash_code = RVSWD_FLASH_OPTION_ERROR_HALFWORD_UNLOCK;
        }
        return false;
    }
    if ((control & (rvswd_flash_ch32_control_lock |
                    rvswd_flash_ch32_control_fast_lock)) != 0u ||
        (control & rvswd_flash_option_control_write) == 0u) {
        operation->flash_code =
            RVSWD_FLASH_OPTION_ERROR_HALFWORD_REMAINS_LOCKED;
        return false;
    }

    idle_control = control & ~(rvswd_flash_option_control_program |
                               rvswd_flash_option_control_erase |
                               rvswd_flash_ch32_control_start);

    // Option Bytes 擦除和重写必须保持完整的 16 字节镜像
    if (!rvswd_memory_write32(
            operation, rvswd_flash_ch32_control_register,
            idle_control | rvswd_flash_option_control_erase) ||
        !rvswd_memory_write32(
            operation, rvswd_flash_ch32_control_register,
            idle_control | rvswd_flash_option_control_erase |
                rvswd_flash_ch32_control_start) ||
        !rvswd_flash_ch32_wait_ready(
            operation, profile, &status,
            RVSWD_FLASH_OPTION_ERROR_HALFWORD_ERASE_STATUS_READ,
            RVSWD_FLASH_OPTION_ERROR_HALFWORD_ERASE_STATUS_TIMEOUT)) {
        if (operation->flash_code == 0u) {
            operation->flash_code = RVSWD_FLASH_OPTION_ERROR_HALFWORD_ERASE;
        }
        goto cleanup;
    }
    if ((status & rvswd_flash_ch32_status_write_protection_error) != 0u) {
        operation->flash_code =
            RVSWD_FLASH_OPTION_ERROR_HALFWORD_ERASE_PROTECTED;
        goto cleanup;
    }

    // Option Bytes 擦除后重新解锁主存储区、快速模式和 Option Bytes
    if (!rvswd_memory_read32(operation, profile, true,
                             rvswd_flash_ch32_control_register, &control) ||
        !rvswd_flash_option_unlock(operation, true) ||
        !rvswd_memory_read32(operation, profile, true,
                             rvswd_flash_ch32_control_register, &control)) {
        operation->flash_code = RVSWD_FLASH_OPTION_ERROR_HALFWORD_REUNLOCK;
        goto cleanup;
    }
    if ((control & (rvswd_flash_ch32_control_lock |
                    rvswd_flash_ch32_control_fast_lock)) != 0u ||
        (control & rvswd_flash_option_control_write) == 0u) {
        operation->flash_code = RVSWD_FLASH_OPTION_ERROR_HALFWORD_RELOCKED;
        goto cleanup;
    }

    idle_control = control & ~(rvswd_flash_option_control_program |
                               rvswd_flash_option_control_erase |
                               rvswd_flash_ch32_control_start);

    for (uint32_t index = 0u;
         index < RVSWD_FLASH_OPTION_WORD_COUNT * 2u; ++index) {
        uint16_t value = (uint16_t)(option_words[index / 2u] >>
                                    ((index & 1u) * 16u));

        if (!rvswd_memory_write32(
                operation, rvswd_flash_ch32_control_register,
                idle_control | rvswd_flash_option_control_program) ||
            !rvswd_flash_option_write16(
                operation, profile->option_base + index * 2u, value,
                rvswd_flash_option_abstract_timeout_us) ||
            !rvswd_flash_ch32_wait_ready(
                operation, profile, &status,
                RVSWD_FLASH_OPTION_ERROR_HALFWORD_WRITE_STATUS_READ,
                RVSWD_FLASH_OPTION_ERROR_HALFWORD_WRITE_STATUS_TIMEOUT)) {
            if (operation->flash_code == 0u) {
                operation->flash_code =
                    RVSWD_FLASH_OPTION_ERROR_HALFWORD_WRITE;
            }
            goto cleanup;
        }
        if ((status & rvswd_flash_ch32_status_write_protection_error) != 0u) {
            operation->flash_code =
                RVSWD_FLASH_OPTION_ERROR_HALFWORD_WRITE_PROTECTED;
            goto cleanup;
        }
    }
    success = true;

cleanup:
    if (!rvswd_memory_write32(
            operation, rvswd_flash_ch32_control_register,
            (idle_control & ~rvswd_flash_option_control_write) |
                rvswd_flash_ch32_control_lock |
                rvswd_flash_ch32_control_fast_lock)) {
        if (operation->flash_code == 0u) {
            operation->flash_code =
                RVSWD_FLASH_OPTION_ERROR_HALFWORD_CLEANUP;
        }
        success = false;
    }
    return success;
}

static bool rvswd_flash_option_unprotect(
    struct rvswd_operation *operation,
    const struct rvswd_target_profile *profile) {
    uint32_t control;
    uint32_t idle_control = 0u;
    uint32_t status;
    bool success = false;

    if (!rvswd_flash_ch32_wait_ready(
            operation, profile, &status,
            RVSWD_FLASH_OPTION_ERROR_UNPROTECT_INITIAL_STATUS_READ,
            RVSWD_FLASH_OPTION_ERROR_UNPROTECT_INITIAL_STATUS_TIMEOUT) ||
        !rvswd_memory_read32(operation, profile, true,
                             rvswd_flash_ch32_control_register, &control)) {
        if (operation->flash_code == 0u) {
            operation->flash_code =
                RVSWD_FLASH_OPTION_ERROR_UNPROTECT_CONTROL_READ;
        }
        return false;
    }
    if (!rvswd_flash_option_unlock(operation, true) ||
        !rvswd_memory_read32(operation, profile, true,
                             rvswd_flash_ch32_control_register, &control)) {
        if (operation->flash_code == 0u) {
            operation->flash_code =
                RVSWD_FLASH_OPTION_ERROR_UNPROTECT_UNLOCK;
        }
        return false;
    }
    if ((control & rvswd_flash_ch32_control_lock) != 0u ||
        (control & rvswd_flash_option_control_write) == 0u) {
        operation->flash_code =
            RVSWD_FLASH_OPTION_ERROR_UNPROTECT_REMAINS_LOCKED;
        return false;
    }

    idle_control = control & ~(rvswd_flash_option_control_program |
                               rvswd_flash_option_control_erase |
                               rvswd_flash_ch32_control_start);
    if (!rvswd_memory_write32(
            operation, rvswd_flash_ch32_control_register,
            idle_control | rvswd_flash_option_control_erase) ||
        !rvswd_memory_write32(
            operation, rvswd_flash_ch32_control_register,
            idle_control | rvswd_flash_option_control_erase |
                rvswd_flash_ch32_control_start) ||
        !rvswd_flash_ch32_wait_ready(
            operation, profile, &status,
            RVSWD_FLASH_OPTION_ERROR_UNPROTECT_ERASE_STATUS_READ,
            RVSWD_FLASH_OPTION_ERROR_UNPROTECT_ERASE_STATUS_TIMEOUT)) {
        if (operation->flash_code == 0u) {
            operation->flash_code =
                RVSWD_FLASH_OPTION_ERROR_UNPROTECT_ERASE;
        }
        goto cleanup;
    }
    if ((status & rvswd_flash_ch32_status_write_protection_error) != 0u) {
        operation->flash_code =
            RVSWD_FLASH_OPTION_ERROR_UNPROTECT_ERASE_PROTECTED;
        goto cleanup;
    }

    // 解除读保护只恢复 RDP，保留 Option Bytes 擦除后的默认状态
    if (!rvswd_memory_read32(operation, profile, true,
                             rvswd_flash_ch32_control_register, &control) ||
        !rvswd_flash_option_unlock(operation, true) ||
        !rvswd_memory_read32(operation, profile, true,
                             rvswd_flash_ch32_control_register, &control)) {
        operation->flash_code =
            RVSWD_FLASH_OPTION_ERROR_UNPROTECT_REUNLOCK;
        goto cleanup;
    }
    if ((control & rvswd_flash_ch32_control_lock) != 0u ||
        (control & rvswd_flash_option_control_write) == 0u) {
        operation->flash_code =
            RVSWD_FLASH_OPTION_ERROR_UNPROTECT_RELOCKED;
        goto cleanup;
    }

    idle_control = control & ~(rvswd_flash_option_control_program |
                               rvswd_flash_option_control_erase |
                               rvswd_flash_ch32_control_start);
    if (!rvswd_memory_write32(
            operation, rvswd_flash_ch32_control_register,
            idle_control | rvswd_flash_option_control_program)) {
        operation->flash_code =
            RVSWD_FLASH_OPTION_ERROR_UNPROTECT_ENTER_PROGRAM;
        goto cleanup;
    }
    if (!rvswd_flash_option_write16(
            operation, profile->option_base,
            rvswd_flash_option_rdp_unprotected,
            rvswd_flash_ch32_operation_timeout_us)) {
        if (operation->flash_code == 0u) {
            operation->flash_code =
                RVSWD_FLASH_OPTION_ERROR_UNPROTECT_WRITE_RDP;
        }
        goto cleanup;
    }
    if (!rvswd_flash_ch32_wait_ready(
            operation, profile, &status,
            RVSWD_FLASH_OPTION_ERROR_UNPROTECT_FINAL_STATUS_READ,
            RVSWD_FLASH_OPTION_ERROR_UNPROTECT_FINAL_STATUS_TIMEOUT)) {
        if (operation->flash_code == 0u) {
            operation->flash_code = RVSWD_FLASH_OPTION_ERROR_UNPROTECT_WAIT;
        }
        goto cleanup;
    }
    if ((status & rvswd_flash_ch32_status_write_protection_error) != 0u) {
        operation->flash_code =
            RVSWD_FLASH_OPTION_ERROR_UNPROTECT_WRITE_PROTECTED;
        goto cleanup;
    }
    success = true;

cleanup:
    if (!rvswd_memory_write32(
            operation, rvswd_flash_ch32_control_register,
            (idle_control & ~rvswd_flash_option_control_write) |
                rvswd_flash_ch32_control_lock)) {
        if (operation->flash_code == 0u) {
            operation->flash_code =
                RVSWD_FLASH_OPTION_ERROR_UNPROTECT_CLEANUP;
        }
        success = false;
    }
    return success;
}

bool rvswd_flash_set_read_protected(struct rvswd_operation *operation,
                                    const struct rvswd_target_profile *profile,
                                    bool protected) {
    uint32_t option_words[RVSWD_FLASH_OPTION_WORD_COUNT];
    bool current;

    if (profile == NULL || profile->ch5xx_protocol) {
        operation->flash_code = RVSWD_FLASH_OPTION_ERROR_UNSUPPORTED_TARGET;
        return false;
    }
    if (!rvswd_flash_read_protected(operation, profile, &current)) {
        return false;
    }
    if (current == protected) {
        return true;
    }

    if (!protected && profile->option_write == RVSWD_OPTION_WRITE_FAST_BUFFER) {
        if (!rvswd_flash_option_unprotect(operation, profile)) {
            return false;
        }
    } else {
        for (uint32_t index = 0u; index < RVSWD_FLASH_OPTION_WORD_COUNT;
             ++index) {
            if (!rvswd_memory_read32(operation, profile, true,
                                     profile->option_base + index * 4u,
                                     &option_words[index])) {
                operation->flash_code = RVSWD_FLASH_OPTION_ERROR_READ_IMAGE;
                return false;
            }
        }

        option_words[0] =
            (option_words[0] & 0xffff0000u) |
            (protected ? rvswd_flash_option_rdp_protected
                       : rvswd_flash_option_rdp_unprotected);
        switch (profile->option_write) {
            case RVSWD_OPTION_WRITE_FAST_BUFFER:
                if (!rvswd_flash_option_write_fast_buffer(
                        operation, profile, option_words)) {
                    return false;
                }
                break;
            case RVSWD_OPTION_WRITE_HALFWORD:
                if (!rvswd_flash_option_write_halfword(
                        operation, profile, option_words)) {
                    return false;
                }
                break;
            default:
                operation->flash_code = RVSWD_FLASH_OPTION_ERROR_WRITE_MODE;
                return false;
        }
    }

    // 解除读保护时目标硬件会自动整片擦除主存储区，复位后 OBR 才加载新状态
    if (!rvswd_reset_and_halt(operation)) {
        operation->flash_code = RVSWD_FLASH_OPTION_ERROR_RESET_AND_HALT;
        return false;
    }
    if (!rvswd_flash_read_protected(operation, profile, &current)) {
        return false;
    }
    if (current != protected) {
        operation->flash_code = RVSWD_FLASH_OPTION_ERROR_VERIFY;
        return false;
    }
    return true;
}

bool rvswd_flash_set_option_bytes(struct rvswd_operation *operation,
                                  const struct rvswd_target_profile *profile,
                                  const uint8_t *values, size_t count) {
    uint32_t option_words[RVSWD_FLASH_OPTION_WORD_COUNT];
    uint32_t actual;

    operation->flash_code = 0u;
    if (profile == NULL || profile->ch5xx_protocol) {
        operation->flash_code = RVSWD_FLASH_OPTION_ERROR_UNSUPPORTED_TARGET;
        return false;
    }
    if (values == NULL || count != RVSWD_OPTION_CONFIG_BYTE_COUNT) {
        operation->flash_code = RVSWD_FLASH_OPTION_ERROR_CONFIG_INPUT;
        return false;
    }

    // 扩展配置包含 USER、DATA0、DATA1 和 WRP0..3，RDP 固定保持可调试状态
    option_words[0] =
        (uint32_t)rvswd_flash_option_rdp_unprotected |
        ((uint32_t)rvswd_flash_option_encode_byte(values[0]) << 16u);
    for (uint32_t index = 1u; index < RVSWD_FLASH_OPTION_WORD_COUNT; ++index) {
        uint32_t value_index = index * 2u - 1u;

        option_words[index] =
            (uint32_t)rvswd_flash_option_encode_byte(values[value_index]) |
            ((uint32_t)rvswd_flash_option_encode_byte(
                 values[value_index + 1u])
             << 16u);
    }

    switch (profile->option_write) {
        case RVSWD_OPTION_WRITE_FAST_BUFFER:
            if (!rvswd_flash_option_write_fast_buffer(
                    operation, profile, option_words)) {
                return false;
            }
            break;
        case RVSWD_OPTION_WRITE_HALFWORD:
            if (!rvswd_flash_option_write_halfword(
                    operation, profile, option_words)) {
                return false;
            }
            break;
        default:
            operation->flash_code = RVSWD_FLASH_OPTION_ERROR_WRITE_MODE;
            return false;
    }

    // 复位使 OBR 和 WRPR 装载新值，再逐字验证完整 Option Bytes 镜像
    if (!rvswd_reset_and_halt(operation)) {
        operation->flash_code = RVSWD_FLASH_OPTION_ERROR_RESET_AND_HALT;
        return false;
    }
    for (uint32_t index = 0u; index < RVSWD_FLASH_OPTION_WORD_COUNT; ++index) {
        if (!rvswd_memory_read32(operation, profile, true,
                                 profile->option_base + index * 4u,
                                 &actual) ||
            actual != option_words[index]) {
            operation->flash_code = RVSWD_FLASH_OPTION_ERROR_VERIFY;
            return false;
        }
    }
    return true;
}
