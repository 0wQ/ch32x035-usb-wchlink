#include "rvswd_target_profile.h"

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
