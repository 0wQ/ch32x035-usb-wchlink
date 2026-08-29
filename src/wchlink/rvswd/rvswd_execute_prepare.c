#include "wchlink/rvswd/rvswd_execute_prepare.h"

#include "wchlink/rvswd/rvswd_memory.h"
#include "wchlink/rvswd/rvswd_types.h"

#include <stddef.h>

// X03X 族 loader 执行前的受控环境参数，来自官方 LinkE 对 X035 的 RVSWD 抓包
static const uint32_t rvswd_execute_prepare_rcc_cfgr = 0x40021004u;
static const uint32_t rvswd_execute_prepare_rcc_cfgr_value = 0x50u;
static const uint32_t rvswd_execute_prepare_flash_mode = 0x40022000u;
static const uint32_t rvswd_execute_prepare_flash_mode_value = 0x12u;
static const uint32_t rvswd_execute_prepare_apb2_en = 0x40021014u;
static const uint32_t rvswd_execute_prepare_apb1_en = 0x40021018u;
static const uint32_t rvswd_execute_prepare_ahb_en = 0x40021020u;
static const uint32_t rvswd_execute_prepare_wwdg = 0xe000f000u;

// 阶段码沿用 memory 诊断码风格，与 rvswd_debug_execute 的 0xe0 段互不覆盖
enum rvswd_execute_prepare_error {
    RVSWD_EXECUTE_PREPARE_ERROR_RCC_CFGR = 0xf1u,
    RVSWD_EXECUTE_PREPARE_ERROR_FLASH_MODE = 0xf2u,
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

// 降频和 Flash 控制器配置保证 loader 的编程时序，关闭外设时钟和看门狗避免打断
static bool rvswd_execute_prepare_x03x(struct rvswd_operation *operation) {
    return rvswd_execute_prepare_write(
               operation, rvswd_execute_prepare_rcc_cfgr,
               rvswd_execute_prepare_rcc_cfgr_value,
               RVSWD_EXECUTE_PREPARE_ERROR_RCC_CFGR) &&
           rvswd_execute_prepare_write(
               operation, rvswd_execute_prepare_flash_mode,
               rvswd_execute_prepare_flash_mode_value,
               RVSWD_EXECUTE_PREPARE_ERROR_FLASH_MODE) &&
           rvswd_execute_prepare_write(
               operation, rvswd_execute_prepare_apb2_en, 0u,
               RVSWD_EXECUTE_PREPARE_ERROR_APB2_EN) &&
           rvswd_execute_prepare_write(
               operation, rvswd_execute_prepare_apb1_en, 0u,
               RVSWD_EXECUTE_PREPARE_ERROR_APB1_EN) &&
           rvswd_execute_prepare_write(
               operation, rvswd_execute_prepare_ahb_en, 0u,
               RVSWD_EXECUTE_PREPARE_ERROR_AHB_EN) &&
           rvswd_execute_prepare_write(operation, rvswd_execute_prepare_wwdg, 0u,
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
            return rvswd_execute_prepare_x03x(operation);
        default:
            return true;
    }
}
