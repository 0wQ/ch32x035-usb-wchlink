#pragma once

#include "wchlink/protocol/wchlink_family.h"

#include <stdbool.h>
#include <stdint.h>

enum rvswd_flash_unlock_mode {
    RVSWD_FLASH_UNLOCK_MAIN_AND_FAST,
    RVSWD_FLASH_UNLOCK_MAIN_OPTION_AND_FAST,
};

enum rvswd_option_write_mode {
    RVSWD_OPTION_WRITE_FAST_BUFFER,
    RVSWD_OPTION_WRITE_HALFWORD,
};

enum rvswd_packet_mode {
    RVSWD_PACKET_SHORT,
    RVSWD_PACKET_LONG,
};

enum rvswd_memory_write_mode {
    RVSWD_MEMORY_WRITE_WORD,
    RVSWD_MEMORY_WRITE_STREAMING,
};

enum rvswd_target_loader {
    RVSWD_TARGET_LOADER_DEFAULT,
    RVSWD_TARGET_LOADER_L103,
    RVSWD_TARGET_LOADER_CH5XX,
};

// 目标 profile 只描述目标差异，不承载 RVSWD 操作和 Flash 流程
struct rvswd_target_profile {
    uint8_t wchlink_family;
    bool ch5xx_protocol;
    bool fast_timing;
    uint8_t loader;
    enum rvswd_memory_write_mode memory_write_mode;
    enum rvswd_flash_unlock_mode erase_unlock;
    enum rvswd_option_write_mode option_write;
    uint32_t option_base;
};
