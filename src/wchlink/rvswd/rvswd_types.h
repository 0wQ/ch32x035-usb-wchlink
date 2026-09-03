#pragma once

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

enum rvswd_target_chip_info_layout {
    RVSWD_TARGET_CHIP_INFO_UNSUPPORTED,
    RVSWD_TARGET_CHIP_INFO_ESIG,
    RVSWD_TARGET_CHIP_INFO_LEGACY,
};

// 目标身份读取所需的地址和状态位，连接流程不再持有芯片地址常量
struct rvswd_target_identity_profile {
    uint32_t chip_id_address;
    uint32_t option_status_address;
    uint32_t option_status_read_protected_mask;
    uint32_t esig_flash_size_address;
    uint32_t esig_uid_low_address;
    uint32_t esig_uid_high_address;
    uint32_t esig_uid_tail_address;
};

// Option Bytes 的控制地址由目标 profile 提供，Flash 流程不重复定义
struct rvswd_target_option_profile {
    uint32_t address_register;
    uint32_t status_register;
    uint32_t write_protection_register;
};

// Loader profile 集中目标 RAM 布局和下载策略，session 不直接持有目标地址
struct rvswd_target_loader_profile {
    uint32_t code_address;
    uint32_t data_address;
    uint32_t stack_top;
    uint32_t checksum_address;
    uint32_t length_address;
    uint32_t dpc_value;
    uint32_t download_limit;
    uint32_t download_packet_size;
    uint32_t data_page_size;
    uint32_t initialize_mode;
    uint32_t prepared_mode;
    uint32_t program_mode;
    uint32_t verify_mode;
    uint32_t program_verify_mode;
    uint32_t checksum_mode_mask;
    uint32_t length_mode_mask;
    bool repeat_initialize;
    bool partial_write_supported;
    bool variable_length;
};

// 目标 profile 只描述目标差异，不承载 RVSWD 操作和 Flash 流程
struct rvswd_target_profile {
    bool fast_timing;
    const struct rvswd_target_identity_profile *identity;
    const struct rvswd_target_option_profile *option;
    const struct rvswd_target_loader_profile *loader;
    bool loader_clears_debug_unlock;
    enum rvswd_flash_unlock_mode erase_unlock;
    enum rvswd_option_write_mode option_write;
    bool memory_type_supported;
    uint32_t option_base;
    // 不在 target profile 保存 Code Flash 起始地址和容量，烧录地址由主机请求传入
    // 当前官方流程不由 Link 下位在 loader 执行前预校验容量，实际容量由 chip info 返回
};
