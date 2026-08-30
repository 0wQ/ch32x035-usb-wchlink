#include "wchlink/rvswd/rvswd_execute_prepare.h"

#include "wchlink/rvswd/rvswd_memory.h"
#include "wchlink/rvswd/rvswd_types.h"

#include <stddef.h>

// 阶段码沿用 memory 诊断码风格，与 rvswd_debug_execute 的 0xe0 段互不覆盖
enum rvswd_execute_prepare_error {
    RVSWD_EXECUTE_PREPARE_ERROR_RCC_CFGR = 0xf1u,
    RVSWD_EXECUTE_PREPARE_ERROR_RCC_CR = 0xf3u,
    RVSWD_EXECUTE_PREPARE_ERROR_FLASH_MODE = 0xf2u,
    RVSWD_EXECUTE_PREPARE_ERROR_FLASH_CTLR = 0xf8u,
    RVSWD_EXECUTE_PREPARE_ERROR_FLASH_STATR = 0xf9u,
    RVSWD_EXECUTE_PREPARE_ERROR_FLASH_ADDR = 0xfau,
    RVSWD_EXECUTE_PREPARE_ERROR_APB2_EN = 0xf4u,
    RVSWD_EXECUTE_PREPARE_ERROR_APB1_EN = 0xf5u,
    RVSWD_EXECUTE_PREPARE_ERROR_AHB_EN = 0xf6u,
    RVSWD_EXECUTE_PREPARE_ERROR_WWDG = 0xf7u,
};

static bool rvswd_execute_prepare_write(struct rvswd_operation *operation,
                                        uint32_t address, uint32_t value,
                                        uint8_t error_code) {
    if (!rvswd_memory_write32(operation, address, value)) {
        operation->memory_code = error_code;
        return false;
    }
    return true;
}

static bool rvswd_execute_prepare_read(
    struct rvswd_operation *operation,
    const struct rvswd_target_profile *profile, uint32_t address,
    uint8_t error_code, uint32_t *value) {
    if (!rvswd_memory_read32(operation, profile, true, address, value)) {
        operation->memory_code = error_code;
        return false;
    }
    return true;
}

// 降频和 Flash 控制器配置保证 loader 的编程时序，关闭外设时钟和看门狗避免打断
static bool rvswd_execute_prepare_x03x(
    struct rvswd_operation *operation,
    const struct rvswd_target_profile *profile) {
    const struct rvswd_target_prepare_profile *prepare = profile->prepare;
    uint32_t rcc_cr_value;
    uint32_t ignored_value;

    if (prepare == NULL) {
        operation->memory_code = RVSWD_EXECUTE_PREPARE_ERROR_RCC_CR;
        return false;
    }

    // 官方 LinkE 先回读 RCC_CR，再按当前时钟源状态写回并继续配置
    if (!rvswd_execute_prepare_read(
            operation, profile, prepare->rcc_cr_address,
            RVSWD_EXECUTE_PREPARE_ERROR_RCC_CR, &rcc_cr_value) ||
        !rvswd_execute_prepare_write(
            operation, prepare->rcc_cr_address, rcc_cr_value,
            RVSWD_EXECUTE_PREPARE_ERROR_RCC_CR)) {
        return false;
    }
    return rvswd_execute_prepare_write(
               operation, prepare->rcc_cfgr_address,
               prepare->rcc_cfgr_value,
               RVSWD_EXECUTE_PREPARE_ERROR_RCC_CFGR) &&
           rvswd_execute_prepare_write(
               operation, prepare->apb2_enable_address, 0u,
               RVSWD_EXECUTE_PREPARE_ERROR_APB2_EN) &&
           rvswd_execute_prepare_write(
               operation, prepare->apb1_enable_address, 0u,
               RVSWD_EXECUTE_PREPARE_ERROR_APB1_EN) &&
           rvswd_execute_prepare_write(
               operation, prepare->ahb_enable_address, 0u,
               RVSWD_EXECUTE_PREPARE_ERROR_AHB_EN) &&
           rvswd_execute_prepare_write(
               operation, prepare->flash_mode_address,
               prepare->flash_mode_value,
               RVSWD_EXECUTE_PREPARE_ERROR_FLASH_MODE) &&
           rvswd_execute_prepare_write(
               operation, prepare->flash_control_address,
               prepare->flash_control_value,
               RVSWD_EXECUTE_PREPARE_ERROR_FLASH_CTLR) &&
           rvswd_execute_prepare_read(
               operation, profile, prepare->flash_status_address,
               RVSWD_EXECUTE_PREPARE_ERROR_FLASH_STATR, &ignored_value) &&
           rvswd_execute_prepare_read(
               operation, profile, prepare->flash_address_address,
               RVSWD_EXECUTE_PREPARE_ERROR_FLASH_ADDR, &ignored_value) &&
           rvswd_execute_prepare_write(operation, prepare->watchdog_address, 0u,
                                       RVSWD_EXECUTE_PREPARE_ERROR_WWDG);
}

bool rvswd_execute_prepare(struct rvswd_operation *operation,
                           const struct rvswd_target_profile *profile) {
    if (operation == NULL || profile == NULL ||
        profile->execute_prepare == RVSWD_EXECUTE_PREPARE_NONE) {
        return true;
    }
    switch (profile->execute_prepare) {
        case RVSWD_EXECUTE_PREPARE_X03X:
            return rvswd_execute_prepare_x03x(operation, profile);
        default:
            return true;
    }
}
