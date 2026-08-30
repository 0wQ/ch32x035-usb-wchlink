#include "wchlink/target/rvswd_target_x03x.h"

#include "wchlink/protocol/wchlink_family.h"
#include "wchlink/rvswd/rvswd_debug.h"
#include "wchlink/rvswd/rvswd_memory.h"
#include "wchlink/rvswd/rvswd_types.h"
#include "wchlink/transport/rvswd_transport.h"

#include <stddef.h>

enum rvswd_target_x03x_prepare_error {
    RVSWD_TARGET_X03X_PREPARE_ERROR_RCC_CFGR = 0xf1u,
    RVSWD_TARGET_X03X_PREPARE_ERROR_RCC_CR = 0xf3u,
    RVSWD_TARGET_X03X_PREPARE_ERROR_FLASH_MODE = 0xf2u,
    RVSWD_TARGET_X03X_PREPARE_ERROR_FLASH_CTLR = 0xf8u,
    RVSWD_TARGET_X03X_PREPARE_ERROR_FLASH_STATR = 0xf9u,
    RVSWD_TARGET_X03X_PREPARE_ERROR_FLASH_ADDR = 0xfau,
    RVSWD_TARGET_X03X_PREPARE_ERROR_APB2_EN = 0xf4u,
    RVSWD_TARGET_X03X_PREPARE_ERROR_APB1_EN = 0xf5u,
    RVSWD_TARGET_X03X_PREPARE_ERROR_AHB_EN = 0xf6u,
    RVSWD_TARGET_X03X_PREPARE_ERROR_WWDG = 0xf7u,
};

static const uint32_t rvswd_target_x03x_chip_id_mask = 0xfff00000u;
static const uint32_t rvswd_target_x03x_chip_id_value = 0x03500000u;
static const uint32_t rvswd_target_x03x_rcc_cr_address = 0x40021000u;
static const uint32_t rvswd_target_x03x_rcc_cfgr_address = 0x40021004u;
static const uint32_t rvswd_target_x03x_rcc_cfgr_value = 0x50u;
static const uint32_t rvswd_target_x03x_apb2_enable_address = 0x40021014u;
static const uint32_t rvswd_target_x03x_apb1_enable_address = 0x40021018u;
static const uint32_t rvswd_target_x03x_ahb_enable_address = 0x40021020u;
static const uint32_t rvswd_target_x03x_flash_mode_address = 0x40022000u;
static const uint32_t rvswd_target_x03x_flash_mode_value = 0x12u;
static const uint32_t rvswd_target_x03x_flash_control_address = 0x40022010u;
static const uint32_t rvswd_target_x03x_flash_control_value = 0x8080u;
static const uint32_t rvswd_target_x03x_flash_status_address = 0x4002201cu;
static const uint32_t rvswd_target_x03x_flash_address_address = 0x40022020u;
static const uint32_t rvswd_target_x03x_watchdog_address = 0xe000f000u;
static const uint32_t rvswd_target_x03x_loader_execute_timeout_ms = 5000u;

static const struct rvswd_target_identity_profile rvswd_target_x03x_identity = {
    .chip_id_address = 0x1ffff704u,
    .option_status_address = 0x4002201cu,
    .option_status_read_protected_mask = 1u << 1u,
    .esig_flash_size_address = 0x1ffff7e0u,
    .esig_uid_low_address = 0x1ffff7e8u,
    .esig_uid_high_address = 0x1ffff7ecu,
    .esig_uid_tail_address = 0x1ffff7f0u,
    .ch5xx_debug_data_address = 0u,
};

static const struct rvswd_target_option_profile rvswd_target_x03x_option = {
    .address_register = 0x40022014u,
    .status_register = 0x4002201cu,
    .write_protection_register = 0x40022020u,
};

static const struct rvswd_target_loader_profile rvswd_target_x03x_loader = {
    .kind = RVSWD_TARGET_LOADER_DEFAULT,
    .code_address = 0x20000000u,
    .data_address = 0x20001000u,
    .stack_top = 0x20002800u,
    .checksum_address = 0x20002010u,
    .length_address = 0x20002010u,
    .dpc_value = 0x20002800u,
    .download_limit = 512u,
    .download_packet_size = 256u,
    .data_page_size = 1u,
    .initialize_mode = 0x01u,
    .prepared_mode = 0x01u,
    .program_mode = 0x0cu,
    .verify_mode = 0x10u,
    .program_verify_mode = 0x1cu,
    .checksum_mode_mask = 0x10u,
    .length_mode_mask = 0x08u,
    .repeat_initialize = false,
    .partial_write_supported = false,
    .variable_length = false,
};

static const struct rvswd_target_profile rvswd_target_x03x_profile_data = {
    .wchlink_family = WCHLINK_TARGET_FAMILY_X03X,
    .ch5xx_protocol = false,
    .fast_timing = false,
    .identity = &rvswd_target_x03x_identity,
    .option = &rvswd_target_x03x_option,
    .loader = &rvswd_target_x03x_loader,
    .loader_clears_debug_unlock = true,
    .memory_write_mode = RVSWD_MEMORY_WRITE_DIRECT,
    .erase_unlock = RVSWD_FLASH_UNLOCK_MAIN_AND_FAST,
    .option_write = RVSWD_OPTION_WRITE_FAST_BUFFER,
    .option_base = 0x1ffff800u,
    .code_flash_base = 0x08000000u,
    .code_flash_size = 0xf800u,
};

// 阶段码写入 operation.memory_code，供 target ports 保留前置失败位置
static bool rvswd_target_x03x_prepare_write(struct rvswd_operation *operation,
                                            uint32_t address, uint32_t value,
                                            uint8_t error_code) {
    if (!rvswd_memory_write32(operation, address, value)) {
        operation->memory_code = error_code;
        return false;
    }
    return true;
}

static bool rvswd_target_x03x_prepare_read(
    struct rvswd_operation *operation,
    const struct rvswd_target_profile *profile, uint32_t address,
    uint8_t error_code, uint32_t *value) {
    if (!rvswd_memory_read32(operation, profile, true, address, value)) {
        operation->memory_code = error_code;
        return false;
    }
    return true;
}

// CH32X03X 的 loader 依赖固定时钟、Flash 控制器和看门狗状态，访问顺序与官方 LinkE 抓包一致
bool rvswd_target_x03x_loader_prepare(
    struct rvswd_operation *operation,
    const struct rvswd_target_profile *profile, uint32_t mode) {
    uint32_t rcc_cr_value;
    uint32_t ignored_value;

    // 编程阶段复用初始化阶段的目标环境，避免重复改写运行中的 Flash 时钟
    if ((mode & 1u) == 0u) {
        return true;
    }
    // 先保留当前 RCC_CR，再切换到 loader 所需的受控时钟配置
    if (!rvswd_target_x03x_prepare_read(
            operation, profile, rvswd_target_x03x_rcc_cr_address,
            RVSWD_TARGET_X03X_PREPARE_ERROR_RCC_CR, &rcc_cr_value) ||
        !rvswd_target_x03x_prepare_write(
            operation, rvswd_target_x03x_rcc_cr_address, rcc_cr_value,
            RVSWD_TARGET_X03X_PREPARE_ERROR_RCC_CR) ||
        !rvswd_target_x03x_prepare_write(
            operation, rvswd_target_x03x_rcc_cfgr_address,
            rvswd_target_x03x_rcc_cfgr_value,
            RVSWD_TARGET_X03X_PREPARE_ERROR_RCC_CFGR) ||
        !rvswd_target_x03x_prepare_write(
            operation, rvswd_target_x03x_apb2_enable_address, 0u,
            RVSWD_TARGET_X03X_PREPARE_ERROR_APB2_EN) ||
        !rvswd_target_x03x_prepare_write(
            operation, rvswd_target_x03x_apb1_enable_address, 0u,
            RVSWD_TARGET_X03X_PREPARE_ERROR_APB1_EN) ||
        !rvswd_target_x03x_prepare_write(
            operation, rvswd_target_x03x_ahb_enable_address, 0u,
            RVSWD_TARGET_X03X_PREPARE_ERROR_AHB_EN) ||
        !rvswd_target_x03x_prepare_write(
            operation, rvswd_target_x03x_flash_mode_address,
            rvswd_target_x03x_flash_mode_value,
            RVSWD_TARGET_X03X_PREPARE_ERROR_FLASH_MODE) ||
        !rvswd_target_x03x_prepare_write(
            operation, rvswd_target_x03x_flash_control_address,
            rvswd_target_x03x_flash_control_value,
            RVSWD_TARGET_X03X_PREPARE_ERROR_FLASH_CTLR) ||
        !rvswd_target_x03x_prepare_read(
            operation, profile, rvswd_target_x03x_flash_status_address,
            RVSWD_TARGET_X03X_PREPARE_ERROR_FLASH_STATR, &ignored_value) ||
        !rvswd_target_x03x_prepare_read(
            operation, profile, rvswd_target_x03x_flash_address_address,
            RVSWD_TARGET_X03X_PREPARE_ERROR_FLASH_ADDR, &ignored_value)) {
        return false;
    }
    return rvswd_target_x03x_prepare_write(
        operation, rvswd_target_x03x_watchdog_address, 0u,
        RVSWD_TARGET_X03X_PREPARE_ERROR_WWDG);
}

// 按 CH32X03X 的调试寄存器 ABI 启动 loader，并读取 a0 作为目标操作结果
bool rvswd_target_x03x_loader_execute(
    struct rvswd_operation *operation, uint32_t entry, uint32_t stack_top,
    uint32_t mode, uint32_t address, uint32_t length, uint32_t data_address,
    uint32_t dpc_value, uint32_t *result) {
    (void)data_address;
    (void)dpc_value;
    // 官方 LinkE 在 loader 初始化前配置 DCSR，使 RAM loader 的 ebreak 进入调试停机
    if (((mode & 1u) != 0u && !rvswd_debug_write_register(operation, 0x7b0u, 0x000090c3u)) ||
        ((mode & 1u) == 0u && !rvswd_debug_write_register(operation, 0x300u, 0u)) ||
        !rvswd_debug_write_raw_gpr(operation, 10u, mode) ||
        !rvswd_debug_write_raw_gpr(operation, 11u, address) ||
        !rvswd_debug_write_raw_gpr(operation, 12u, length) ||
        !rvswd_debug_write_raw_gpr(operation, 2u, stack_top) ||
        !rvswd_debug_write_register(operation, 0x7b1u, entry)) {
        if (result != NULL) {
            *result = 0xe101u;
        }
        return false;
    }
    if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_CONTROL, 0x40000001u).ok ||
        !rvswd_debug_wait_dmstatus(operation, 1u << 9u, true,
                                   rvswd_target_x03x_loader_execute_timeout_ms)) {
        if (result != NULL) {
            *result = 0xe102u;
        }
        return false;
    }
    // 保持 dmactive，loader 返回后的 SRAM 和 checksum abstract 访问仍沿用当前会话
    if (result != NULL && !rvswd_debug_read_raw_gpr(operation, 10u, result)) {
        *result = 0xe103u;
        return false;
    }
    return true;
}

static const struct rvswd_target_module rvswd_target_x03x = {
    .profile = &rvswd_target_x03x_profile_data,
    .loader_prepare = rvswd_target_x03x_loader_prepare,
    .loader_execute = rvswd_target_x03x_loader_execute,
};

const struct rvswd_target_module *rvswd_target_x03x_module(void) {
    return &rvswd_target_x03x;
}

const struct rvswd_target_profile *rvswd_target_x03x_profile(void) {
    return &rvswd_target_x03x_profile_data;
}

bool rvswd_target_x03x_matches_chip_id(uint32_t chip_id) {
    return (chip_id & rvswd_target_x03x_chip_id_mask) ==
           rvswd_target_x03x_chip_id_value;
}
