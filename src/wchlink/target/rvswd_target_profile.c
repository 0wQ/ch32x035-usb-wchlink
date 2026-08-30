#include "wchlink/target/rvswd_target_profile.h"

#include "wchlink/protocol/wchlink_family.h"

#include <stddef.h>

static const uint32_t rvswd_chip_family_mask = 0xfff00000u;

static const struct rvswd_target_identity_profile rvswd_identity_ch32 = {
    .chip_id_address = 0x1ffff704u,
    .option_status_address = 0x4002201cu,
    .option_status_read_protected_mask = 1u << 1u,
    .esig_flash_size_address = 0x1ffff7e0u,
    .esig_uid_low_address = 0x1ffff7e8u,
    .esig_uid_high_address = 0x1ffff7ecu,
    .esig_uid_tail_address = 0x1ffff7f0u,
    .ch5xx_debug_data_address = 0u,
};

static const struct rvswd_target_identity_profile rvswd_identity_ch5xx = {
    .chip_id_address = 0x40001041u,
    .option_status_address = 0u,
    .option_status_read_protected_mask = 0u,
    .esig_flash_size_address = 0u,
    .esig_uid_low_address = 0u,
    .esig_uid_high_address = 0u,
    .esig_uid_tail_address = 0u,
    .ch5xx_debug_data_address = 0xe0000380u,
};

const struct rvswd_target_identity_profile *rvswd_target_probe_identity_ch32(
    void) {
    return &rvswd_identity_ch32;
}

const struct rvswd_target_identity_profile *rvswd_target_probe_identity_ch5xx(
    void) {
    return &rvswd_identity_ch5xx;
}

static const struct rvswd_target_option_profile rvswd_option_ch32 = {
    .address_register = 0x40022014u,
    .status_register = 0x4002201cu,
    .write_protection_register = 0x40022020u,
};

static const struct rvswd_target_loader_profile rvswd_loader_profile_l103 = {
    .kind = RVSWD_TARGET_LOADER_L103,
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

static const struct rvswd_target_loader_profile rvswd_loader_profile_v30x = {
    .kind = RVSWD_TARGET_LOADER_DEFAULT,
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

static const struct rvswd_target_loader_profile rvswd_loader_profile_ch5xx = {
    .kind = RVSWD_TARGET_LOADER_CH5XX,
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

static const struct rvswd_target_profile rvswd_target_profile_l103 = {
    .wchlink_family = WCHLINK_TARGET_FAMILY_CH32L10X,
    .ch5xx_protocol = false,
    .fast_timing = false,
    .identity = &rvswd_identity_ch32,
    .option = &rvswd_option_ch32,
    .loader = &rvswd_loader_profile_l103,
    .loader_clears_debug_unlock = false,
    .memory_write_mode = RVSWD_MEMORY_WRITE_STREAMING,
    .erase_unlock = RVSWD_FLASH_UNLOCK_MAIN_OPTION_AND_FAST,
    .option_write = RVSWD_OPTION_WRITE_FAST_BUFFER,
    .option_base = 0x1ffff800u,
    .code_flash_base = 0x08000000u,
};

static const struct rvswd_target_profile rvswd_target_profile_v30x = {
    .wchlink_family = WCHLINK_TARGET_FAMILY_CH32V30X,
    .ch5xx_protocol = false,
    .fast_timing = false,
    .identity = &rvswd_identity_ch32,
    .option = &rvswd_option_ch32,
    .loader = &rvswd_loader_profile_v30x,
    .loader_clears_debug_unlock = false,
    .memory_write_mode = RVSWD_MEMORY_WRITE_STREAMING,
    .erase_unlock = RVSWD_FLASH_UNLOCK_MAIN_AND_FAST,
    .option_write = RVSWD_OPTION_WRITE_HALFWORD,
    .option_base = 0x1ffff800u,
    .code_flash_base = 0x08000000u,
};

static const struct rvswd_target_profile rvswd_target_profile_ch59x = {
    .wchlink_family = WCHLINK_TARGET_FAMILY_CH59X,
    .ch5xx_protocol = true,
    .fast_timing = false,
    .identity = &rvswd_identity_ch5xx,
    .option = NULL,
    .loader = &rvswd_loader_profile_ch5xx,
    .loader_clears_debug_unlock = false,
    .memory_write_mode = RVSWD_MEMORY_WRITE_WORD,
    // CH5xx 使用专用 Flash 命令和 loader，这两个字段只为保持 profile 接口完整
    .erase_unlock = RVSWD_FLASH_UNLOCK_MAIN_AND_FAST,
    .option_write = RVSWD_OPTION_WRITE_FAST_BUFFER,
    .option_base = 0u,
    .code_flash_base = 0x08000000u,
};

static const struct rvswd_target_profile rvswd_target_profile_ch58x = {
    .wchlink_family = WCHLINK_TARGET_FAMILY_CH58X,
    .ch5xx_protocol = true,
    .fast_timing = false,
    .identity = &rvswd_identity_ch5xx,
    .option = NULL,
    .loader = &rvswd_loader_profile_ch5xx,
    .loader_clears_debug_unlock = false,
    .memory_write_mode = RVSWD_MEMORY_WRITE_WORD,
    // CH5xx 使用专用 Flash 命令和 loader，这两个字段只为保持 profile 接口完整
    .erase_unlock = RVSWD_FLASH_UNLOCK_MAIN_AND_FAST,
    .option_write = RVSWD_OPTION_WRITE_FAST_BUFFER,
    .option_base = 0u,
    .code_flash_base = 0x08000000u,
};

struct rvswd_chip_profile_match {
    uint32_t family;
    const struct rvswd_target_profile *profile;
};

// ChipID 高位只负责选择 profile，目标行为继续由 profile 字段描述
static const struct rvswd_chip_profile_match rvswd_chip_profile_matches[] = {
    {0x10300000u, &rvswd_target_profile_l103},
    {0x30300000u, &rvswd_target_profile_v30x},
    {0x30500000u, &rvswd_target_profile_v30x},
    {0x30700000u, &rvswd_target_profile_v30x},
    {0x91000000u, &rvswd_target_profile_ch59x},
    {0x92000000u, &rvswd_target_profile_ch59x},
    {0x82000000u, &rvswd_target_profile_ch58x},
    {0x83000000u, &rvswd_target_profile_ch58x},
};

const struct rvswd_target_profile *rvswd_target_profile_from_chip_id(
    uint32_t chip_id) {
    uint32_t family = chip_id & rvswd_chip_family_mask;

    for (size_t index = 0u;
         index < sizeof(rvswd_chip_profile_matches) /
                     sizeof(rvswd_chip_profile_matches[0]);
         ++index) {
        if (rvswd_chip_profile_matches[index].family == family) {
            return rvswd_chip_profile_matches[index].profile;
        }
    }
    return NULL;
}

const struct rvswd_target_profile *rvswd_target_profile_from_family(
    uint8_t family) {
    switch (family) {
        case WCHLINK_TARGET_FAMILY_CH32L10X:
            return &rvswd_target_profile_l103;
        case WCHLINK_TARGET_FAMILY_CH32V30X:
            return &rvswd_target_profile_v30x;
        case WCHLINK_TARGET_FAMILY_CH59X:
            return &rvswd_target_profile_ch59x;
        case WCHLINK_TARGET_FAMILY_CH58X:
            return &rvswd_target_profile_ch58x;
        default:
            return NULL;
    }
}

const struct rvswd_target_profile *rvswd_target_profile_resolve(
    uint32_t chip_id, uint8_t family_hint, bool family_hint_active) {
    const struct rvswd_target_profile *profile =
        rvswd_target_profile_from_chip_id(chip_id);

    if (profile != NULL || !family_hint_active) {
        return profile;
    }
    return rvswd_target_profile_from_family(family_hint);
}
