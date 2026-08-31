#include "wchlink/target/rvswd_target_ch59x.h"

#include "wchlink/flash/rvswd_flash_ch58x_59x.h"
#include "wchlink/protocol/wchlink_family.h"
#include "wchlink/rvswd/rvswd_debug.h"
#include "wchlink/rvswd/rvswd_memory.h"
#include "wchlink/rvswd/rvswd_reset.h"
#include "wchlink/rvswd/rvswd_types.h"

#include <stddef.h>

extern const uint8_t ch58x_59x_flash_erase_stub_start[];
extern const uint8_t ch58x_59x_flash_erase_stub_end[];

static const struct rvswd_target_identity_profile rvswd_target_ch59x_identity = {
    .chip_id_address = 0x40001041u,
    .option_status_address = 0u,
    .option_status_read_protected_mask = 0u,
    .esig_flash_size_address = 0u,
    .esig_uid_low_address = 0u,
    .esig_uid_high_address = 0u,
    .esig_uid_tail_address = 0u,
};

static const uint32_t rvswd_target_ch59x_debug_data_address = 0xe0000380u;

static const struct rvswd_target_loader_profile rvswd_target_ch59x_loader = {
    .code_address = 0x20004000u,
    .data_address = 0x20005000u,
    .stack_top = 0x20007000u,
    .checksum_address = 0x20006010u,
    .length_address = 0u,
    .dpc_value = 0u,
    .download_limit = 2048u,
    .download_packet_size = 256u,
    .data_page_size = 256u,
    .initialize_mode = 0x01u,
    .prepared_mode = 0x01u,
    .program_mode = 0x08u,
    .verify_mode = 0x10u,
    .program_verify_mode = 0x08u,
    .checksum_mode_mask = 0x10u,
    .length_mode_mask = 0u,
    .repeat_initialize = true,
    .partial_write_supported = true,
    .variable_length = true,
};

static const struct rvswd_target_profile rvswd_target_ch59x_profile_data = {
    .fast_timing = false,
    .identity = &rvswd_target_ch59x_identity,
    .option = NULL,
    .loader = &rvswd_target_ch59x_loader,
    .loader_clears_debug_unlock = false,
    .erase_unlock = RVSWD_FLASH_UNLOCK_MAIN_AND_FAST,
    .option_write = RVSWD_OPTION_WRITE_FAST_BUFFER,
    .memory_type_supported = false,
    .option_base = 0u,
    .code_flash_base = 0x08000000u,
};

static const struct rvswd_target_capabilities rvswd_target_ch59x_capabilities = {
    .packet_mode = RVSWD_PACKET_SHORT,
    .chip_info_layout = RVSWD_TARGET_CHIP_INFO_LEGACY,
    .memory_streaming = false,
};

// 使用 CH59X Debug Module 的字节访问程序读取专用身份寄存器
static bool rvswd_target_ch59x_probe_read8(
    struct rvswd_operation *operation, uint32_t address, uint8_t *value) {
    uint32_t abstractcs;
    struct rvswd_transport_result read_result;

    if (value == NULL ||
        !rvswd_debug_write_raw_gpr(operation, 13u, rvswd_target_ch59x_debug_data_address) ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_ABSTRACTCS, 0x00000700u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_PROGBUF0, 0x00058483u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_PROGBUF1, 0x00968223u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_PROGBUF2, 0x00100073u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_DATA0, address).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_COMMAND, 0x0027100bu).ok ||
        !rvswd_debug_wait_abstract_idle(operation, &abstractcs) ||
        ((abstractcs >> 8u) & 0x07u) != 0u) {
        return false;
    }
    read_result = rvswd_operation_read_dmi(operation, RVSWD_DMI_DATA1);
    if (!read_result.ok) {
        return false;
    }
    *value = (uint8_t)read_result.value;
    return true;
}

// 读取 CH59X 专用 8 位 ChipID，并校验族匹配结果
static bool rvswd_target_ch59x_probe_chip_id(
    struct rvswd_operation *operation, uint32_t *chip_id) {
    uint8_t value;

    if (chip_id == NULL ||
        !rvswd_target_ch59x_probe_read8(operation, rvswd_target_ch59x_identity.chip_id_address, &value) ||
        !rvswd_target_ch59x_matches_chip_id((uint32_t)value << 24u)) {
        return false;
    }
    *chip_id = (uint32_t)value << 24u;
    return true;
}

static const struct rvswd_target_probe_ops rvswd_target_ch59x_probe = {
    .read_chip_id = rvswd_target_ch59x_probe_chip_id,
};

// TODO: 当前复制既有 CH58X/CH59X loader ABI，待 CH59X 官方抓包冻结执行顺序
bool rvswd_target_ch59x_loader_execute(
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
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_CONTROL, 0x80000001u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_CONTROL, 0x80000001u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_CONTROL, 0x00000001u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_CONTROL, 0x40000001u).ok) {
        if (result != NULL) {
            *result = 0xe501u;
        }
        return false;
    }
    if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_CONTROL, 0x00000001u).ok ||
        !rvswd_debug_wait_dmstatus(operation, 1u << 9u, true, 5000u)) {
        if (result != NULL) {
            *result = 0xe502u;
        }
        return false;
    }
    return result == NULL || rvswd_debug_read_raw_gpr(operation, 10u, result);
}

static const struct rvswd_memory_ops rvswd_target_ch59x_memory = {
    .read32 = rvswd_memory_read32_synchronized,
    .write32 = rvswd_memory_write32_slow,
    .write = rvswd_memory_write_slow,
};

static const struct rvswd_target_loader_ops rvswd_target_ch59x_loader_ops = {
    .prepare = NULL,
    .execute = rvswd_target_ch59x_loader_execute,
};

static const struct rvswd_target_flash_ops rvswd_target_ch59x_flash = {
    .erase_all = rvswd_target_ch59x_flash_erase_all,
    .rewrite_page = rvswd_flash_rewrite_page,
    .read_protected = NULL,
    .write_protected = NULL,
    .set_read_protected = NULL,
    .set_option_bytes = NULL,
    .read_memory_type = NULL,
    .set_memory_type = NULL,
};

static const struct rvswd_target_control_ops rvswd_target_ch59x_control = {
    .reset_and_halt = rvswd_reset_and_halt,
    .soft_reset_and_run = rvswd_soft_reset_and_run,
    .reset_and_run = rvswd_reset_and_run,
};

static const struct rvswd_target_module rvswd_target_ch59x = {
    .family = WCHLINK_TARGET_FAMILY_CH59X,
    .matches_chip_id = rvswd_target_ch59x_matches_chip_id,
    .profile = &rvswd_target_ch59x_profile_data,
    .capabilities = &rvswd_target_ch59x_capabilities,
    .probe = &rvswd_target_ch59x_probe,
    .memory = &rvswd_target_ch59x_memory,
    .loader = &rvswd_target_ch59x_loader_ops,
    .flash = &rvswd_target_ch59x_flash,
    .control = &rvswd_target_ch59x_control,
};

// 返回 CH59X 族的完整 module 入口
const struct rvswd_target_module *rvswd_target_ch59x_module(void) {
    return &rvswd_target_ch59x;
}

// 返回 CH59X 族唯一的静态目标描述
const struct rvswd_target_profile *rvswd_target_ch59x_profile(void) {
    return &rvswd_target_ch59x_profile_data;
}

// 使用 CH59X 独立 stub 启动整片擦除流程
bool rvswd_target_ch59x_flash_erase_all(
    struct rvswd_operation *operation,
    const struct rvswd_target_profile *profile) {
    return rvswd_flash_ch58x_59x_erase_all(
        operation, profile, ch58x_59x_flash_erase_stub_start,
        ch58x_59x_flash_erase_stub_end,
        rvswd_target_ch59x_loader_execute);
}

bool rvswd_target_ch59x_matches_chip_id(uint32_t chip_id) {
    uint32_t family = chip_id & 0xff000000u;

    return family == 0x91000000u || family == 0x92000000u;
}
