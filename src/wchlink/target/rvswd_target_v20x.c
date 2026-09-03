#include "wchlink/target/rvswd_target_v20x.h"

#include "bsp/bsp_delay.h"
#include "wchlink/flash/rvswd_flash_option.h"
#include "wchlink/protocol/wchlink_family.h"
#include "wchlink/rvswd/rvswd_debug.h"
#include "wchlink/rvswd/rvswd_memory.h"
#include "wchlink/rvswd/rvswd_reset.h"
#include "wchlink/rvswd/rvswd_types.h"

#include <stddef.h>

static const uint32_t rvswd_target_v20x_chip_id_mask = 0xfff00000u;
static const uint32_t rvswd_target_v20x_chip_id_value = 0x20300000u;
static const uint32_t rvswd_target_v20x_chip_id_address = 0x1ffff704u;
static const uint32_t rvswd_target_v20x_loader_timeout_ms = 5000u;
static const uint32_t rvswd_target_v20x_flash_key_register = 0x40022004u;
static const uint32_t rvswd_target_v20x_flash_status_register = 0x4002200cu;
static const uint32_t rvswd_target_v20x_flash_control_register = 0x40022010u;
static const uint32_t rvswd_target_v20x_flash_mode_key_register = 0x40022024u;
static const uint32_t rvswd_target_v20x_flash_key1 = 0x45670123u;
static const uint32_t rvswd_target_v20x_flash_key2 = 0xcdef89abu;
static const uint32_t rvswd_target_v20x_flash_mass_erase = 1u << 2u;
static const uint32_t rvswd_target_v20x_flash_start = 1u << 6u;
static const uint32_t rvswd_target_v20x_flash_busy = 1u << 0u;
static const uint32_t rvswd_target_v20x_flash_write_protection_error = 1u << 4u;
static const uint32_t rvswd_target_v20x_flash_timeout_us = 6000000u;

enum rvswd_target_v20x_flash_error {
    RVSWD_TARGET_V20X_FLASH_ERROR_UNLOCK = 0x14u,
    RVSWD_TARGET_V20X_FLASH_ERROR_CONTROL_READ = 0x15u,
    RVSWD_TARGET_V20X_FLASH_ERROR_SELECT_MASS_ERASE = 0x19u,
    RVSWD_TARGET_V20X_FLASH_ERROR_START_MASS_ERASE = 0x1au,
    RVSWD_TARGET_V20X_FLASH_ERROR_STATUS_READ = 0x1bu,
    RVSWD_TARGET_V20X_FLASH_ERROR_TIMEOUT = 0x1cu,
    RVSWD_TARGET_V20X_FLASH_ERROR_WRITE_PROTECTED = 0x1du,
    RVSWD_TARGET_V20X_FLASH_ERROR_CLEANUP = 0x1eu,
    RVSWD_TARGET_V20X_FLASH_ERROR_RECOVERY = 0x1fu,
};

static const struct rvswd_target_identity_profile rvswd_target_v20x_identity = {
    .chip_id_address = 0x1ffff704u,
    .option_status_address = 0x4002201cu,
    .option_status_read_protected_mask = 1u << 1u,
    .esig_flash_size_address = 0x1ffff7e0u,
    .esig_uid_low_address = 0x1ffff7e8u,
    .esig_uid_high_address = 0x1ffff7ecu,
    .esig_uid_tail_address = 0x1ffff7f0u,
};

static const struct rvswd_target_option_profile rvswd_target_v20x_option = {
    .address_register = 0x40022014u,
    .status_register = 0x4002201cu,
    .write_protection_register = 0x40022020u,
};

static const struct rvswd_target_loader_profile rvswd_target_v20x_loader = {
    .code_address = 0x20000000u,
    .data_address = 0x20001000u,
    .stack_top = 0x20002800u,
    .checksum_address = 0x20002010u,
    .length_address = 0u,
    .dpc_value = 0u,
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

static const struct rvswd_target_profile rvswd_target_v20x_profile_data = {
    .fast_timing = false,
    .identity = &rvswd_target_v20x_identity,
    .option = &rvswd_target_v20x_option,
    .loader = &rvswd_target_v20x_loader,
    .loader_clears_debug_unlock = false,
    .erase_unlock = RVSWD_FLASH_UNLOCK_MAIN_AND_FAST,
    .option_write = RVSWD_OPTION_WRITE_HALFWORD,
    .memory_type_supported = true,
    .option_base = 0x1ffff800u,
};

static const struct rvswd_target_capabilities rvswd_target_v20x_capabilities = {
    .packet_mode = RVSWD_PACKET_SHORT,
    .chip_info_layout = RVSWD_TARGET_CHIP_INFO_ESIG,
    .memory_streaming = false,
};

// 官方 WCH-LinkE 在 DMI ChipID 后通过 Data1、Access Memory 和 Data0 复核固定身份地址
static bool rvswd_target_v20x_probe_chip_id(
    struct rvswd_operation *operation, uint32_t *chip_id) {
    uint32_t value;

    if (chip_id == NULL ||
        !rvswd_memory_read32_access_memory(
            operation, rvswd_target_v20x_chip_id_address, &value) ||
        !rvswd_target_v20x_matches_chip_id(value)) {
        return false;
    }
    *chip_id = value;
    return true;
}

static const struct rvswd_target_probe_ops rvswd_target_v20x_probe = {
    .read_chip_id = rvswd_target_v20x_probe_chip_id,
};

// CH32V203 的 Access Memory 写由三条 DMI transaction 组成，逐字 ABSTRACTCS 轮询会阻塞下载
// 后续 loader execute 和主机校验负责确认整块数据已经被目标正确处理
static bool rvswd_target_v20x_write32_access_memory(
    struct rvswd_operation *operation, uint32_t address, uint32_t value) {
    if (operation == NULL) {
        return false;
    }
    operation->memory_code = 0u;
    operation->address = address;
    if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_DATA1, address).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_DATA0, value).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_COMMAND,
                                   0x02210000u)
             .ok) {
        operation->memory_code = 0xc1u;
        return false;
    }
    return true;
}

// loader 的 code 和 data 区都以 32 位逐字写入，loader execute 负责 Flash 操作结果
static bool rvswd_target_v20x_write_access_memory(
    struct rvswd_operation *operation, uint32_t address, const uint8_t *data,
    uint32_t length) {
    if (operation == NULL || data == NULL || length == 0u ||
        (address & 3u) != 0u || (length & 3u) != 0u) {
        if (operation != NULL) {
            operation->memory_code = 0xefu;
            operation->address = address;
        }
        return false;
    }
    for (uint32_t offset = 0u; offset < length; offset += 4u) {
        uint32_t value = (uint32_t)data[offset] |
                         ((uint32_t)data[offset + 1u] << 8u) |
                         ((uint32_t)data[offset + 2u] << 16u) |
                         ((uint32_t)data[offset + 3u] << 24u);

        if (!rvswd_target_v20x_write32_access_memory(
                operation, address + offset, value)) {
            return false;
        }
    }
    return true;
}

// 官方 CH32V203 loader 下载和数据写入均使用 Access Memory 的逐字直接路径
static const struct rvswd_memory_ops rvswd_target_v20x_memory = {
    .read32 = rvswd_memory_read32_access_memory,
    .write32 = rvswd_target_v20x_write32_access_memory,
    .write = rvswd_target_v20x_write_access_memory,
};

// MRS 的旧、新 ROM/RAM 命令都将 USER[7:6] 解释为索引，写入时再扩展为 USER[7:5]
static bool rvswd_target_v20x_read_memory_type(
    struct rvswd_operation *operation,
    const struct rvswd_target_profile *profile, bool extended,
    uint8_t *memory_type) {
    uint32_t option_word;

    (void)extended;
    if (operation == NULL || memory_type == NULL ||
        profile == NULL || profile->option_base == 0u ||
        !rvswd_memory_read32(operation, profile, true, profile->option_base,
                             &option_word)) {
        return false;
    }
    *memory_type = (uint8_t)((option_word >> 22u) & 0x03u);
    return true;
}

// MRS 的旧、新写入命令均传入逻辑索引，写入前转换为 USER[7:5] 的奇数编码
static bool rvswd_target_v20x_set_memory_type(
    struct rvswd_operation *operation,
    const struct rvswd_target_profile *profile, bool extended,
    uint8_t memory_type) {
    (void)extended;
    if (memory_type > 3u) {
        return false;
    }
    return rvswd_flash_set_memory_type(
        operation, profile, true, (uint8_t)(memory_type * 2u + 1u));
}

// 记录 CH32V20X loader 前置写入的阶段错误，失败时返回给主机的错误端点
static bool rvswd_target_v20x_prepare_write(
    struct rvswd_operation *operation, uint32_t address, uint32_t value,
    uint8_t error_code) {
    if (!rvswd_target_v20x_write32_access_memory(operation, address, value)) {
        operation->memory_code = error_code;
        return false;
    }
    return true;
}

// CH32V203 首次运行 loader 前清理四个 RCC 使能寄存器，顺序来自官方 MRS 抓包
static bool rvswd_target_v20x_loader_prepare(
    struct rvswd_operation *operation,
    const struct rvswd_target_profile *profile, uint32_t mode) {
    (void)profile;
    if ((mode & 1u) == 0u) {
        return true;
    }
    return rvswd_debug_write_register(operation, 0x7b0u, 0x000090c3u) &&
           rvswd_target_v20x_prepare_write(operation, 0x40021014u, 0u,
                                           0xf1u) &&
           rvswd_target_v20x_prepare_write(operation, 0x40021018u, 0u,
                                           0xf2u) &&
           rvswd_target_v20x_prepare_write(operation, 0x4002101cu, 0u,
                                           0xf3u) &&
           rvswd_target_v20x_prepare_write(operation, 0x40021020u, 0u,
                                           0xf4u);
}

// CH32V203 loader 通过 a0-a2 传递 mode、Flash 地址和长度，DPC 固定为零
static bool rvswd_target_v20x_loader_execute(
    struct rvswd_operation *operation, uint32_t entry, uint32_t stack_top,
    uint32_t mode, uint32_t address, uint32_t length, uint32_t data_address,
    uint32_t dpc_value, uint32_t *result) {
    (void)data_address;
    (void)dpc_value;
    if (!rvswd_debug_write_raw_gpr(operation, 10u, mode) ||
        !rvswd_debug_write_raw_gpr(operation, 11u, address) ||
        !rvswd_debug_write_raw_gpr(operation, 12u, length) ||
        !rvswd_debug_write_register(operation, 0x300u, 0u) ||
        !rvswd_debug_write_register(operation, 0x1002u, stack_top) ||
        !rvswd_debug_write_register(operation, 0x7b1u, entry)) {
        if (result != NULL) {
            *result = 0xe401u;
        }
        return false;
    }
    if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_CONTROL,
                                   0x40000001u)
             .ok ||
        !rvswd_debug_wait_dmstatus(operation, 1u << 9u, true,
                                   rvswd_target_v20x_loader_timeout_ms)) {
        if (result != NULL) {
            *result = 0xe402u;
        }
        return false;
    }
    if (result != NULL && !rvswd_debug_read_raw_gpr(operation, 10u, result)) {
        *result = 0xe403u;
        return false;
    }
    return true;
}

static const struct rvswd_target_loader_ops rvswd_target_v20x_loader_ops = {
    .prepare = rvswd_target_v20x_loader_prepare,
    .execute = rvswd_target_v20x_loader_execute,
};

// CH32V203 的 STATR.BSY 在擦除启动前保持置位，官方流程先发起擦除再等待其清零
static bool rvswd_target_v20x_erase_all(
    struct rvswd_operation *operation,
    const struct rvswd_target_profile *profile) {
    uint32_t control;
    uint32_t status;
    uint32_t idle_control;
    uint64_t start;
    bool erase_started = false;
    bool success = false;

    operation->flash_code = 0u;
    if (!rvswd_memory_write32(operation, rvswd_target_v20x_flash_key_register,
                              rvswd_target_v20x_flash_key1) ||
        !rvswd_memory_write32(operation, rvswd_target_v20x_flash_key_register,
                              rvswd_target_v20x_flash_key2) ||
        !rvswd_memory_write32(operation,
                              rvswd_target_v20x_flash_mode_key_register,
                              rvswd_target_v20x_flash_key1) ||
        !rvswd_memory_write32(operation,
                              rvswd_target_v20x_flash_mode_key_register,
                              rvswd_target_v20x_flash_key2)) {
        operation->flash_code = RVSWD_TARGET_V20X_FLASH_ERROR_UNLOCK;
        return false;
    }
    if (!rvswd_memory_read32(operation, profile, true,
                             rvswd_target_v20x_flash_control_register,
                             &control)) {
        operation->flash_code = RVSWD_TARGET_V20X_FLASH_ERROR_CONTROL_READ;
        return false;
    }
    idle_control = control & ~(rvswd_target_v20x_flash_mass_erase |
                               rvswd_target_v20x_flash_start);
    if (!rvswd_memory_write32(operation, rvswd_target_v20x_flash_control_register,
                              idle_control | rvswd_target_v20x_flash_mass_erase)) {
        operation->flash_code =
            RVSWD_TARGET_V20X_FLASH_ERROR_SELECT_MASS_ERASE;
        return false;
    }
    if (!rvswd_memory_read32(operation, profile, true,
                             rvswd_target_v20x_flash_control_register,
                             &control)) {
        operation->flash_code = RVSWD_TARGET_V20X_FLASH_ERROR_CONTROL_READ;
        goto cleanup;
    }
    if (!rvswd_memory_write32(operation, rvswd_target_v20x_flash_control_register,
                              control | rvswd_target_v20x_flash_start)) {
        operation->flash_code = RVSWD_TARGET_V20X_FLASH_ERROR_START_MASS_ERASE;
        goto cleanup;
    }
    erase_started = true;
    start = bsp_time_us();
    do {
        if (!rvswd_memory_read32(operation, profile, true,
                                 rvswd_target_v20x_flash_status_register,
                                 &status)) {
            operation->flash_code = RVSWD_TARGET_V20X_FLASH_ERROR_STATUS_READ;
            goto cleanup;
        }
        if ((status & rvswd_target_v20x_flash_busy) == 0u) {
            break;
        }
        bsp_delay_us(100u);
    } while ((bsp_time_us() - start) < rvswd_target_v20x_flash_timeout_us);
    if ((status & rvswd_target_v20x_flash_busy) != 0u) {
        operation->flash_code = RVSWD_TARGET_V20X_FLASH_ERROR_TIMEOUT;
        goto cleanup;
    }
    if ((status & rvswd_target_v20x_flash_write_protection_error) != 0u) {
        operation->flash_code =
            RVSWD_TARGET_V20X_FLASH_ERROR_WRITE_PROTECTED;
        goto cleanup;
    }
    success = true;

cleanup:
    // 擦除已启动后始终清除 MER 和 STRT，避免失败状态泄漏到下一条主机命令
    if (erase_started &&
        !rvswd_memory_write32(operation, rvswd_target_v20x_flash_control_register,
                              idle_control)) {
        if (operation->flash_code == 0u) {
            operation->flash_code = RVSWD_TARGET_V20X_FLASH_ERROR_CLEANUP;
        }
        success = false;
    }
    // 全擦后 CH32V203 不接受下一次解锁序列，必须经 ndmreset 回到可重新连接的 halted 状态
    if (success && !rvswd_reset_and_halt(operation)) {
        operation->flash_code = RVSWD_TARGET_V20X_FLASH_ERROR_RECOVERY;
        success = false;
    }
    return success;
}

// CH32V203 的读保护写入复用已验证的半字 Option Bytes 流程，任意配置写入仍未确认
static const struct rvswd_target_flash_ops rvswd_target_v20x_flash = {
    .erase_all = rvswd_target_v20x_erase_all,
    .rewrite_page = NULL,
    .read_protected = rvswd_flash_read_protected,
    .write_protected = rvswd_flash_write_protected,
    .set_read_protected = rvswd_flash_set_read_protected,
    .set_option_bytes = NULL,
    .read_memory_type = rvswd_target_v20x_read_memory_type,
    .set_memory_type = rvswd_target_v20x_set_memory_type,
};

static const struct rvswd_target_control_ops rvswd_target_v20x_control = {
    .reset_and_halt = rvswd_reset_and_halt,
    .soft_reset_and_run = rvswd_soft_reset_and_run,
    .reset_and_run = rvswd_reset_and_run,
};

static const struct rvswd_target_module rvswd_target_v20x = {
    .family = WCHLINK_TARGET_FAMILY_CH32V20X,
    .matches_chip_id = rvswd_target_v20x_matches_chip_id,
    .profile = &rvswd_target_v20x_profile_data,
    .capabilities = &rvswd_target_v20x_capabilities,
    .probe = &rvswd_target_v20x_probe,
    .memory = &rvswd_target_v20x_memory,
    .loader = &rvswd_target_v20x_loader_ops,
    .flash = &rvswd_target_v20x_flash,
    .control = &rvswd_target_v20x_control,
};

// 返回 CH32V20X 的已验证身份、loader、Flash 和基础 reset 模块
const struct rvswd_target_module *rvswd_target_v20x_module(void) {
    return &rvswd_target_v20x;
}

// 官方 CH32V203 SDK 列出的各封装 ChipID 都以 0x203 为高 12 位
bool rvswd_target_v20x_matches_chip_id(uint32_t chip_id) {
    return (chip_id & rvswd_target_v20x_chip_id_mask) ==
           rvswd_target_v20x_chip_id_value;
}
