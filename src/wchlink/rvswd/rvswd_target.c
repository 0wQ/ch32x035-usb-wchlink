#include "rvswd_target.h"

#include "wchlink_family.h"

#include <stddef.h>

#define RVSWD_CHIP_FAMILY_MASK  0xfff00000u
#define RVSWD_CHIP_FAMILY_X035  0x03500000u
#define RVSWD_CHIP_FAMILY_L103  0x10300000u
#define RVSWD_CHIP_FAMILY_V303  0x30300000u
#define RVSWD_CHIP_FAMILY_V305  0x30500000u
#define RVSWD_CHIP_FAMILY_V307  0x30700000u
#define RVSWD_CHIP_FAMILY_CH591 0x91000000u
#define RVSWD_CHIP_FAMILY_CH592 0x92000000u
#define RVSWD_CHIP_FAMILY_CH582 0x82000000u
#define RVSWD_CHIP_FAMILY_CH583 0x83000000u

static const struct rvswd_target_profile rvswd_target_profile_x035 = {
    .wchlink_family = WCHLINK_TARGET_FAMILY_X035,
    .ch5xx_protocol = false,
    .fast_timing = false,
    .memory_write_mode = RVSWD_MEMORY_WRITE_STREAMING,
    .erase_unlock = RVSWD_FLASH_UNLOCK_MAIN_AND_FAST,
    .option_write = RVSWD_OPTION_WRITE_FAST_BUFFER,
    .option_base = 0x1ffff800u,
};

static const struct rvswd_target_profile rvswd_target_profile_l103 = {
    .wchlink_family = WCHLINK_TARGET_FAMILY_L103,
    .ch5xx_protocol = false,
    .fast_timing = false,
    .memory_write_mode = RVSWD_MEMORY_WRITE_STREAMING,
    .erase_unlock = RVSWD_FLASH_UNLOCK_MAIN_OPTION_AND_FAST,
    .option_write = RVSWD_OPTION_WRITE_FAST_BUFFER,
    .option_base = 0x1ffff800u,
};

static const struct rvswd_target_profile rvswd_target_profile_v30x = {
    .wchlink_family = WCHLINK_TARGET_FAMILY_V30X,
    .ch5xx_protocol = false,
    .fast_timing = false,
    .memory_write_mode = RVSWD_MEMORY_WRITE_STREAMING,
    .erase_unlock = RVSWD_FLASH_UNLOCK_MAIN_AND_FAST,
    .option_write = RVSWD_OPTION_WRITE_HALFWORD,
    .option_base = 0x1ffff800u,
};

static const struct rvswd_target_profile rvswd_target_profile_ch59x = {
    .wchlink_family = WCHLINK_TARGET_FAMILY_CH59X,
    .ch5xx_protocol = true,
    .fast_timing = false,
    .memory_write_mode = RVSWD_MEMORY_WRITE_WORD,
    // CH5xx 使用专用 Flash 命令和 loader，这两个字段只为保持 profile 接口完整
    .erase_unlock = RVSWD_FLASH_UNLOCK_MAIN_AND_FAST,
    .option_write = RVSWD_OPTION_WRITE_FAST_BUFFER,
    .option_base = 0u,
};

static const struct rvswd_target_profile rvswd_target_profile_ch58x = {
    .wchlink_family = WCHLINK_TARGET_FAMILY_CH58X,
    .ch5xx_protocol = true,
    .fast_timing = false,
    .memory_write_mode = RVSWD_MEMORY_WRITE_WORD,
    // CH5xx 使用专用 Flash 命令和 loader，这两个字段只为保持 profile 接口完整
    .erase_unlock = RVSWD_FLASH_UNLOCK_MAIN_AND_FAST,
    .option_write = RVSWD_OPTION_WRITE_FAST_BUFFER,
    .option_base = 0u,
};

static uint32_t rvswd_target_current_chip_id;
static uint8_t rvswd_target_expected_family;
static bool rvswd_target_uses_family_hint;

const struct rvswd_target_profile *rvswd_target_profile_from_chip_id(
    uint32_t chip_id) {
    switch (chip_id & RVSWD_CHIP_FAMILY_MASK) {
        case RVSWD_CHIP_FAMILY_X035:
            return &rvswd_target_profile_x035;
        case RVSWD_CHIP_FAMILY_L103:
            return &rvswd_target_profile_l103;
        case RVSWD_CHIP_FAMILY_V303:
        case RVSWD_CHIP_FAMILY_V305:
        case RVSWD_CHIP_FAMILY_V307:
            return &rvswd_target_profile_v30x;
        case RVSWD_CHIP_FAMILY_CH591:
        case RVSWD_CHIP_FAMILY_CH592:
            return &rvswd_target_profile_ch59x;
        case RVSWD_CHIP_FAMILY_CH582:
        case RVSWD_CHIP_FAMILY_CH583:
            return &rvswd_target_profile_ch58x;
        default:
            return NULL;
    }
}

const struct rvswd_target_profile *rvswd_target_profile_from_family(
    uint8_t family) {
    switch (family) {
        case WCHLINK_TARGET_FAMILY_X035:
            return &rvswd_target_profile_x035;
        case WCHLINK_TARGET_FAMILY_L103:
            return &rvswd_target_profile_l103;
        case WCHLINK_TARGET_FAMILY_V30X:
            return &rvswd_target_profile_v30x;
        case WCHLINK_TARGET_FAMILY_CH59X:
            return &rvswd_target_profile_ch59x;
        case WCHLINK_TARGET_FAMILY_CH58X:
            return &rvswd_target_profile_ch58x;
        default:
            return NULL;
    }
}

const struct rvswd_target_profile *rvswd_target_profile_current(void) {
    const struct rvswd_target_profile *profile =
        rvswd_target_profile_from_chip_id(rvswd_target_current_chip_id);

    if (profile != NULL || !rvswd_target_uses_family_hint) {
        return profile;
    }
    return rvswd_target_profile_from_family(rvswd_target_expected_family);
}

void rvswd_target_reset(void) {
    rvswd_target_current_chip_id = 0u;
    rvswd_target_uses_family_hint = false;
}

void rvswd_target_set_chip_id(uint32_t chip_id) {
    rvswd_target_current_chip_id = chip_id;
}

uint32_t rvswd_target_chip_id(void) {
    return rvswd_target_current_chip_id;
}

void rvswd_target_set_family_hint(uint8_t family) {
    rvswd_target_expected_family = family;
}

uint8_t rvswd_target_family_hint(void) {
    return rvswd_target_expected_family;
}

bool rvswd_target_family_hint_active(void) {
    return rvswd_target_uses_family_hint;
}

void rvswd_target_set_family_hint_active(bool active) {
    rvswd_target_uses_family_hint = active;
}

uint8_t rvswd_target_family(void) {
    const struct rvswd_target_profile *profile =
        rvswd_target_profile_current();

    if (profile != NULL) {
        return profile->wchlink_family;
    }
    profile = rvswd_target_profile_from_family(rvswd_target_expected_family);
    return profile == NULL ? 0u : profile->wchlink_family;
}

bool rvswd_target_supports_memory_streaming(void) {
    const struct rvswd_target_profile *profile =
        rvswd_target_profile_current();

    return profile != NULL &&
           profile->memory_write_mode == RVSWD_MEMORY_WRITE_STREAMING;
}
