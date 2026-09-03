#include "wchlink/target/rvswd_target_x03x.h"

#include "wchlink/flash/rvswd_flash_ch32.h"
#include "wchlink/flash/rvswd_flash_option.h"
#include "wchlink/protocol/wchlink_family.h"
#include "wchlink/rvswd/rvswd_debug.h"
#include "wchlink/rvswd/rvswd_memory.h"
#include "wchlink/rvswd/rvswd_reset.h"
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
};

static const struct rvswd_target_option_profile rvswd_target_x03x_option = {
    .address_register = 0x40022014u,
    .status_register = 0x4002201cu,
    .write_protection_register = 0x40022020u,
};

static const struct rvswd_target_loader_profile rvswd_target_x03x_loader = {
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
    .fast_timing = false,
    .identity = &rvswd_target_x03x_identity,
    .option = &rvswd_target_x03x_option,
    .loader = &rvswd_target_x03x_loader,
    .loader_clears_debug_unlock = true,
    .erase_unlock = RVSWD_FLASH_UNLOCK_MAIN_AND_FAST,
    .option_write = RVSWD_OPTION_WRITE_FAST_BUFFER,
    .memory_type_supported = false,
    .option_base = 0x1ffff800u,
};

static const struct rvswd_target_capabilities rvswd_target_x03x_capabilities = {
    .packet_mode = RVSWD_PACKET_SHORT,
    .chip_info_layout = RVSWD_TARGET_CHIP_INFO_ESIG,
    .memory_streaming = false,
};

// 读取 CH32X03X 的内存 ChipID，失败时由连接流程尝试其他 module
static bool rvswd_target_x03x_probe_chip_id(
    struct rvswd_operation *operation, uint32_t *chip_id) {
    return chip_id != NULL &&
           rvswd_memory_read32_access_memory(operation,
                                    rvswd_target_x03x_identity.chip_id_address,
                                    chip_id);
}

// 提供 CH32X03X 的身份探测入口
static const struct rvswd_target_probe_ops rvswd_target_x03x_probe = {
    .read_chip_id = rvswd_target_x03x_probe_chip_id,
};

// 绑定 CH32X03X 的读写访问宽度和失败恢复策略
static const struct rvswd_memory_ops rvswd_target_x03x_memory = {
    .read32 = rvswd_memory_read32_access_memory,
    .write32 = rvswd_memory_write32_direct,
    .write = rvswd_memory_write_direct,
};

static const struct rvswd_target_loader_ops rvswd_target_x03x_loader_ops = {
    .prepare = rvswd_target_x03x_loader_prepare,
    .execute = rvswd_target_x03x_loader_execute,
};

// 绑定 CH32X03X 的 Flash、Option Bytes 和保护操作
static const struct rvswd_target_flash_ops rvswd_target_x03x_flash = {
    .erase_all = rvswd_flash_ch32_erase_all,
    .rewrite_page = NULL,
    .read_protected = rvswd_flash_read_protected,
    .write_protected = rvswd_flash_write_protected,
    .set_read_protected = rvswd_flash_set_read_protected,
    .set_option_bytes = rvswd_flash_set_option_bytes,
    .read_memory_type = rvswd_flash_read_memory_type,
    .set_memory_type = rvswd_flash_set_memory_type,
};

static const struct rvswd_target_control_ops rvswd_target_x03x_control = {
    .reset_and_halt = rvswd_reset_and_halt,
    .soft_reset_and_run = rvswd_soft_reset_and_run,
    .reset_and_run = rvswd_reset_and_run,
};

// 阶段码写入 operation.memory_code，供 target ports 保留前置失败位置
static bool rvswd_target_x03x_prepare_write(struct rvswd_operation *operation,
                                            uint32_t address, uint32_t value,
                                            uint8_t error_code) {
    if (!rvswd_memory_write32_slow(operation, address, value)) {
        operation->memory_code = error_code;
        return false;
    }
    return true;
}

static bool rvswd_target_x03x_prepare_read(
    struct rvswd_operation *operation, uint32_t address, uint8_t error_code,
    uint32_t *value) {
    if (!rvswd_memory_read32_access_memory(operation, address, value)) {
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

    (void)profile;
    // 编程阶段复用初始化阶段的目标环境，避免重复改写运行中的 Flash 时钟
    if ((mode & 1u) == 0u) {
        return true;
    }
    // 先保留当前 RCC_CR，再切换到 loader 所需的受控时钟配置
    if (!rvswd_target_x03x_prepare_read(
            operation, rvswd_target_x03x_rcc_cr_address,
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
            operation, rvswd_target_x03x_flash_status_address,
            RVSWD_TARGET_X03X_PREPARE_ERROR_FLASH_STATR, &ignored_value) ||
        !rvswd_target_x03x_prepare_read(
            operation, rvswd_target_x03x_flash_address_address,
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
    .family = WCHLINK_TARGET_FAMILY_X03X,
    .matches_chip_id = rvswd_target_x03x_matches_chip_id,
    .profile = &rvswd_target_x03x_profile_data,
    .capabilities = &rvswd_target_x03x_capabilities,
    .probe = &rvswd_target_x03x_probe,
    .memory = &rvswd_target_x03x_memory,
    .loader = &rvswd_target_x03x_loader_ops,
    .flash = &rvswd_target_x03x_flash,
    .control = &rvswd_target_x03x_control,
};

// 返回 CH32X03X 族的完整 module 入口
const struct rvswd_target_module *rvswd_target_x03x_module(void) {
    return &rvswd_target_x03x;
}

// 返回 CH32X03X 族唯一的静态目标描述
const struct rvswd_target_profile *rvswd_target_x03x_profile(void) {
    return &rvswd_target_x03x_profile_data;
}

// 判断 ChipID 是否属于 CH32X03X 族
bool rvswd_target_x03x_matches_chip_id(uint32_t chip_id) {
    return (chip_id & rvswd_target_x03x_chip_id_mask) ==
           rvswd_target_x03x_chip_id_value;
}
