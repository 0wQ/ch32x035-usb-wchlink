#include "wchlink/flash/rvswd_flash_ch32.h"

#include "bsp/bsp_delay.h"
#include "wchlink/flash/rvswd_flash_ch32_internal.h"
#include "wchlink/rvswd/rvswd_memory.h"
#include "wchlink/rvswd/rvswd_operation.h"
#include "wchlink/rvswd/rvswd_types.h"

// CH32 backend 只实现通用主存储区整片擦除，目标选择由 Flash facade 完成
static const uint32_t rvswd_flash_ch32_status_register = 0x4002200cu;
static const uint32_t rvswd_flash_ch32_status_busy = 1u << 0u;
static const uint32_t rvswd_flash_ch32_status_end = 1u << 5u;
static const uint32_t rvswd_flash_ch32_control_mass_erase = 1u << 2u;

enum rvswd_flash_ch32_error {
    RVSWD_FLASH_CH32_ERROR_UNLOCK_MODE = 0x0fu,
    RVSWD_FLASH_CH32_ERROR_INITIAL_STATUS_READ = 0x11u,
    RVSWD_FLASH_CH32_ERROR_INITIAL_STATUS_TIMEOUT = 0x12u,
    RVSWD_FLASH_CH32_ERROR_CONTROL_READ = 0x13u,
    RVSWD_FLASH_CH32_ERROR_UNLOCK = 0x14u,
    RVSWD_FLASH_CH32_ERROR_UNLOCKED_CONTROL_READ = 0x15u,
    RVSWD_FLASH_CH32_ERROR_REMAINS_LOCKED = 0x16u,
    RVSWD_FLASH_CH32_ERROR_CLEAR_STATUS = 0x17u,
    RVSWD_FLASH_CH32_ERROR_ENTER_IDLE = 0x18u,
    RVSWD_FLASH_CH32_ERROR_SELECT_MASS_ERASE = 0x19u,
    RVSWD_FLASH_CH32_ERROR_START_MASS_ERASE = 0x1au,
    RVSWD_FLASH_CH32_ERROR_FINAL_STATUS_READ = 0x1bu,
    RVSWD_FLASH_CH32_ERROR_FINAL_STATUS_TIMEOUT = 0x1cu,
    RVSWD_FLASH_CH32_ERROR_WRITE_PROTECTED = 0x1du,
    RVSWD_FLASH_CH32_ERROR_CLEANUP = 0x1eu,
};

bool rvswd_flash_ch32_wait_ready(
    struct rvswd_operation *operation,
    const struct rvswd_target_profile *profile, uint32_t *status,
    uint8_t read_error, uint8_t timeout_error) {
    uint64_t start = bsp_time_us();

    do {
        if (!rvswd_memory_read32(operation, profile, true,
                                 rvswd_flash_ch32_status_register, status)) {
            operation->flash_code = read_error;
            return false;
        }
        if ((*status & rvswd_flash_ch32_status_busy) == 0u) {
            return true;
        }
        bsp_delay_us(100u);
    } while ((bsp_time_us() - start) < rvswd_flash_ch32_operation_timeout_us);

    operation->flash_code = timeout_error;
    return false;
}

static bool rvswd_flash_ch32_unlock_main_option_and_fast(
    struct rvswd_operation *operation, uint32_t control) {
    if ((control & (rvswd_flash_ch32_control_lock |
                    rvswd_flash_ch32_control_fast_lock)) == 0u) {
        return true;
    }

    // L103 需要同时解锁主存储区、用户字和快速编程模式
    return rvswd_memory_write32(operation, rvswd_flash_ch32_key_register,
                                rvswd_flash_ch32_key1) &&
           rvswd_memory_write32(operation, rvswd_flash_ch32_key_register,
                                rvswd_flash_ch32_key2) &&
           rvswd_memory_write32(operation,
                                rvswd_flash_ch32_option_key_register,
                                rvswd_flash_ch32_key1) &&
           rvswd_memory_write32(operation,
                                rvswd_flash_ch32_option_key_register,
                                rvswd_flash_ch32_key2) &&
           rvswd_memory_write32(operation,
                                rvswd_flash_ch32_mode_key_register,
                                rvswd_flash_ch32_key1) &&
           rvswd_memory_write32(operation,
                                rvswd_flash_ch32_mode_key_register,
                                rvswd_flash_ch32_key2);
}

bool rvswd_flash_ch32_unlock_main_and_fast(
    struct rvswd_operation *operation, uint32_t control) {
    if ((control & rvswd_flash_ch32_control_lock) != 0u &&
        (!rvswd_memory_write32(operation, rvswd_flash_ch32_key_register,
                               rvswd_flash_ch32_key1) ||
         !rvswd_memory_write32(operation, rvswd_flash_ch32_key_register,
                               rvswd_flash_ch32_key2))) {
        return false;
    }
    if ((control & rvswd_flash_ch32_control_fast_lock) != 0u &&
        (!rvswd_memory_write32(operation,
                               rvswd_flash_ch32_mode_key_register,
                               rvswd_flash_ch32_key1) ||
         !rvswd_memory_write32(operation,
                               rvswd_flash_ch32_mode_key_register,
                               rvswd_flash_ch32_key2))) {
        return false;
    }
    return true;
}

bool rvswd_flash_ch32_erase_all(
    struct rvswd_operation *operation,
    const struct rvswd_target_profile *profile) {
    uint32_t control;
    uint32_t idle_control;
    uint32_t status;
    bool unlocked;
    bool success = false;

    if (!rvswd_flash_ch32_wait_ready(
            operation, profile, &status,
            RVSWD_FLASH_CH32_ERROR_INITIAL_STATUS_READ,
            RVSWD_FLASH_CH32_ERROR_INITIAL_STATUS_TIMEOUT)) {
        return false;
    }

    if (!rvswd_memory_read32(operation, profile, true,
                             rvswd_flash_ch32_control_register, &control)) {
        operation->flash_code = RVSWD_FLASH_CH32_ERROR_CONTROL_READ;
        return false;
    }

    switch (profile->erase_unlock) {
        case RVSWD_FLASH_UNLOCK_MAIN_OPTION_AND_FAST:
            unlocked = rvswd_flash_ch32_unlock_main_option_and_fast(
                operation, control);
            break;
        case RVSWD_FLASH_UNLOCK_MAIN_AND_FAST:
            unlocked =
                rvswd_flash_ch32_unlock_main_and_fast(operation, control);
            break;
        default:
            operation->flash_code = RVSWD_FLASH_CH32_ERROR_UNLOCK_MODE;
            return false;
    }
    if (!unlocked) {
        operation->flash_code = RVSWD_FLASH_CH32_ERROR_UNLOCK;
        return false;
    }

    if (!rvswd_memory_read32(operation, profile, true,
                             rvswd_flash_ch32_control_register, &control)) {
        operation->flash_code = RVSWD_FLASH_CH32_ERROR_UNLOCKED_CONTROL_READ;
        return false;
    }
    if ((control & (rvswd_flash_ch32_control_lock |
                    rvswd_flash_ch32_control_fast_lock)) != 0u) {
        operation->flash_code = RVSWD_FLASH_CH32_ERROR_REMAINS_LOCKED;
        return false;
    }

    idle_control = control & ~(rvswd_flash_ch32_control_mass_erase |
                               rvswd_flash_ch32_control_start);

    // 清除上一次操作遗留的完成和写保护状态，避免误判本次擦除
    if ((status & (rvswd_flash_ch32_status_end |
                   rvswd_flash_ch32_status_write_protection_error)) != 0u &&
        !rvswd_memory_write32(
            operation, rvswd_flash_ch32_status_register,
            status & (rvswd_flash_ch32_status_end |
                      rvswd_flash_ch32_status_write_protection_error))) {
        operation->flash_code = RVSWD_FLASH_CH32_ERROR_CLEAR_STATUS;
        goto cleanup;
    }

    if (!rvswd_memory_write32(operation,
                              rvswd_flash_ch32_control_register,
                              idle_control)) {
        operation->flash_code = RVSWD_FLASH_CH32_ERROR_ENTER_IDLE;
        goto cleanup;
    }
    if (!rvswd_memory_write32(
            operation, rvswd_flash_ch32_control_register,
            idle_control | rvswd_flash_ch32_control_mass_erase)) {
        operation->flash_code = RVSWD_FLASH_CH32_ERROR_SELECT_MASS_ERASE;
        goto cleanup;
    }
    if (!rvswd_memory_write32(
            operation, rvswd_flash_ch32_control_register,
            idle_control | rvswd_flash_ch32_control_mass_erase |
                rvswd_flash_ch32_control_start)) {
        operation->flash_code = RVSWD_FLASH_CH32_ERROR_START_MASS_ERASE;
        goto cleanup;
    }
    if (!rvswd_flash_ch32_wait_ready(
            operation, profile, &status,
            RVSWD_FLASH_CH32_ERROR_FINAL_STATUS_READ,
            RVSWD_FLASH_CH32_ERROR_FINAL_STATUS_TIMEOUT)) {
        goto cleanup;
    }
    if ((status & rvswd_flash_ch32_status_write_protection_error) != 0u) {
        operation->flash_code = RVSWD_FLASH_CH32_ERROR_WRITE_PROTECTED;
        goto cleanup;
    }
    success = true;

cleanup:
    if (!rvswd_memory_write32(operation,
                              rvswd_flash_ch32_control_register,
                              idle_control)) {
        if (operation->flash_code == 0u) {
            operation->flash_code = RVSWD_FLASH_CH32_ERROR_CLEANUP;
        }
        success = false;
    }
    return success;
}
