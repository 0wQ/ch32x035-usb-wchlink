#pragma once

#include "wchlink/protocol/wchlink_family.h"

#include <stdbool.h>
#include <stdint.h>

enum {
    RVSWD_OPTION_CONFIG_BYTE_COUNT = 7u,
};

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

// Loader profile 集中目标 RAM 布局和下载策略，session 不直接持有目标地址
struct rvswd_target_loader_profile {
    enum rvswd_target_loader kind;
    uint32_t code_address;
    uint32_t data_address;
    uint32_t stack_top;
    uint32_t checksum_address;
    uint32_t download_limit;
    uint32_t download_packet_size;
    uint32_t data_page_size;
    bool variable_length;
};

// 目标 profile 只描述目标差异，不承载 RVSWD 操作和 Flash 流程
struct rvswd_target_profile {
    uint8_t wchlink_family;
    bool ch5xx_protocol;
    bool fast_timing;
    const struct rvswd_target_loader_profile *loader;
    enum rvswd_memory_write_mode memory_write_mode;
    enum rvswd_flash_unlock_mode erase_unlock;
    enum rvswd_option_write_mode option_write;
    uint32_t option_base;
};
