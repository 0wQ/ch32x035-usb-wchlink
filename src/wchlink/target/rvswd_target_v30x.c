#include "wchlink/target/rvswd_target_v30x.h"

#include "bsp/bsp_delay.h"
#include "wchlink/flash/rvswd_flash_ch32.h"
#include "wchlink/flash/rvswd_flash_option.h"
#include "wchlink/protocol/wchlink_family.h"
#include "wchlink/rvswd/rvswd_debug.h"
#include "wchlink/rvswd/rvswd_memory.h"
#include "wchlink/rvswd/rvswd_reset.h"
#include "wchlink/rvswd/rvswd_types.h"

#include <stddef.h>

static const uint32_t rvswd_target_v30x_loader_timeout_ms = 5000u;

static const struct rvswd_target_identity_profile rvswd_target_v30x_identity = {
    .chip_id_address = 0x1ffff704u,
    .option_status_address = 0x4002201cu,
    .option_status_read_protected_mask = 1u << 1u,
    .esig_flash_size_address = 0x1ffff7e0u,
    .esig_uid_low_address = 0x1ffff7e8u,
    .esig_uid_high_address = 0x1ffff7ecu,
    .esig_uid_tail_address = 0x1ffff7f0u,
};

static const struct rvswd_target_option_profile rvswd_target_v30x_option = {
    .address_register = 0x40022014u,
    .status_register = 0x4002201cu,
    .write_protection_register = 0x40022020u,
};

static const struct rvswd_target_loader_profile rvswd_target_v30x_loader = {
    .code_address = 0x20000000u,
    .data_address = 0x20001000u,
    .stack_top = 0x20005000u,
    .checksum_address = 0x20002010u,
    .length_address = 0u,
    .dpc_value = 0u,
    .download_limit = 512u,
    .download_packet_size = 256u,
    .data_page_size = 1u,
    .initialize_mode = 0x01u,
    .prepared_mode = 0x03u,
    .program_mode = 0x08u,
    .verify_mode = 0x10u,
    .program_verify_mode = 0x18u,
    .checksum_mode_mask = 0x10u,
    .length_mode_mask = 0u,
    .repeat_initialize = true,
    .partial_write_supported = false,
    .variable_length = false,
};

static const struct rvswd_target_profile rvswd_target_v30x_profile_data = {
    .fast_timing = false,
    .identity = &rvswd_target_v30x_identity,
    .option = &rvswd_target_v30x_option,
    .loader = &rvswd_target_v30x_loader,
    .loader_clears_debug_unlock = false,
    .erase_unlock = RVSWD_FLASH_UNLOCK_MAIN_AND_FAST,
    .option_write = RVSWD_OPTION_WRITE_HALFWORD,
    .memory_type_supported = true,
    .option_base = 0x1ffff800u,
};

static const struct rvswd_target_capabilities rvswd_target_v30x_capabilities = {
    .packet_mode = RVSWD_PACKET_SHORT,
    .chip_info_layout = RVSWD_TARGET_CHIP_INFO_ESIG,
    .memory_streaming = true,
};

// 先读取 WCH ChipID，失败后按现有内存路径兼容探测
static bool rvswd_target_v30x_probe_chip_id(
    struct rvswd_operation *operation, uint32_t *chip_id) {
    struct rvswd_transport_result read_result =
        rvswd_operation_read_dmi(operation, RVSWD_DMI_WCH_CHIP_ID);

    if (read_result.ok && chip_id != NULL &&
        rvswd_target_v30x_matches_chip_id(read_result.value)) {
        *chip_id = read_result.value;
        return true;
    }
    return chip_id != NULL &&
           rvswd_memory_read32_access_memory(operation,
                                    rvswd_target_v30x_identity.chip_id_address,
                                    chip_id);
}

static const struct rvswd_target_probe_ops rvswd_target_v30x_probe = {
    .read_chip_id = rvswd_target_v30x_probe_chip_id,
};

// TODO: 此函数暂按当前 V30X 代码路径占位，尚未用 L103/X035 之外的 MRS 抓包冻结
static bool rvswd_target_v30x_loader_execute(
    struct rvswd_operation *operation, uint32_t entry, uint32_t stack_top,
    uint32_t mode, uint32_t address, uint32_t length, uint32_t data_address,
    uint32_t dpc_value, uint32_t *result) {
    if (!rvswd_debug_write_raw_gpr(operation, 10u, mode) ||
        !rvswd_debug_write_raw_gpr(operation, 11u, address) ||
        !rvswd_debug_write_raw_gpr(operation, 12u, length) ||
        !rvswd_debug_write_raw_gpr(operation, 13u, data_address) ||
        !rvswd_debug_write_register(operation, 0x1002u, stack_top) ||
        !rvswd_debug_write_register(operation, 0x7b0u, 0x000090c3u) ||
        !rvswd_debug_write_register(operation, 0x300u, dpc_value) ||
        !rvswd_debug_write_register(operation, 0x7b1u, entry) ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_CONTROL,
                                   0x80000001u)
             .ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_CONTROL,
                                   0x80000001u)
             .ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_CONTROL,
                                   0x00000001u)
             .ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_CONTROL,
                                   0x40000001u)
             .ok) {
        if (result != NULL) {
            *result = 0xe301u;
        }
        return false;
    }
    // TODO: 1 ms 是当前 V30X 路径的已知处理窗口，等待新的官方抓包复核
    bsp_delay_us(1000u);
    if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_CONTROL,
                                   0x00000001u)
             .ok ||
        !rvswd_debug_wait_dmstatus(operation, 1u << 9u, true,
                                   rvswd_target_v30x_loader_timeout_ms)) {
        if (result != NULL) {
            *result = 0xe302u;
        }
        return false;
    }
    if (result != NULL && !rvswd_debug_read_raw_gpr(operation, 10u, result)) {
        *result = 0xe303u;
        return false;
    }
    return true;
}

// 绑定 CH32V30X 的访问算法和流式写入恢复路径
static const struct rvswd_memory_ops rvswd_target_v30x_memory = {
    .read32 = rvswd_memory_read32_access_memory,
    .write32 = rvswd_memory_write32_slow,
    .write = rvswd_memory_write_streaming_retry,
};

static const struct rvswd_target_loader_ops rvswd_target_v30x_loader_ops = {
    .prepare = NULL,
    .execute = rvswd_target_v30x_loader_execute,
};

static const struct rvswd_target_flash_ops rvswd_target_v30x_flash = {
    .erase_all = rvswd_flash_ch32_erase_all,
    .rewrite_page = NULL,
    .read_protected = rvswd_flash_read_protected,
    .write_protected = rvswd_flash_write_protected,
    .set_read_protected = rvswd_flash_set_read_protected,
    .set_option_bytes = rvswd_flash_set_option_bytes,
    .read_memory_type = rvswd_flash_read_memory_type,
    .set_memory_type = rvswd_flash_set_memory_type,
};

static const struct rvswd_target_control_ops rvswd_target_v30x_control = {
    .reset_and_halt = rvswd_reset_and_halt,
    .soft_reset_and_run = rvswd_soft_reset_and_run,
    .reset_and_run = rvswd_reset_and_run,
};

static const struct rvswd_target_module rvswd_target_v30x = {
    .family = WCHLINK_TARGET_FAMILY_CH32V30X,
    .matches_chip_id = rvswd_target_v30x_matches_chip_id,
    .profile = &rvswd_target_v30x_profile_data,
    .capabilities = &rvswd_target_v30x_capabilities,
    .probe = &rvswd_target_v30x_probe,
    .memory = &rvswd_target_v30x_memory,
    .loader = &rvswd_target_v30x_loader_ops,
    .flash = &rvswd_target_v30x_flash,
    .control = &rvswd_target_v30x_control,
};

// 返回 CH32V30X 族的完整 module 入口
const struct rvswd_target_module *rvswd_target_v30x_module(void) {
    return &rvswd_target_v30x;
}

// 返回 CH32V30X 族唯一的静态目标描述
const struct rvswd_target_profile *rvswd_target_v30x_profile(void) {
    return &rvswd_target_v30x_profile_data;
}

// 判断 ChipID 是否属于 CH32V30X 族
bool rvswd_target_v30x_matches_chip_id(uint32_t chip_id) {
    uint32_t family = chip_id & 0xfff00000u;

    return family == 0x30300000u || family == 0x30500000u ||
           family == 0x30700000u;
}
