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
    RVSWD_MEMORY_WRITE_DIRECT,
    RVSWD_MEMORY_WRITE_WORD,
    RVSWD_MEMORY_WRITE_STREAMING,
};

enum rvswd_execute_prepare_mode {
    RVSWD_EXECUTE_PREPARE_NONE,
    // X03X 族在 loader 执行前把目标切换到受控环境，与官方 LinkE 行为一致
    RVSWD_EXECUTE_PREPARE_X03X,
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
    uint32_t ch5xx_debug_data_address;
};

// loader 执行前的寄存器准备属于目标适配数据，执行模块只负责按布局访问
struct rvswd_target_prepare_profile {
    uint32_t rcc_cr_address;
    uint32_t rcc_cfgr_address;
    uint32_t rcc_cfgr_value;
    uint32_t apb2_enable_address;
    uint32_t apb1_enable_address;
    uint32_t ahb_enable_address;
    uint32_t flash_mode_address;
    uint32_t flash_mode_value;
    uint32_t flash_control_address;
    uint32_t flash_control_value;
    uint32_t flash_status_address;
    uint32_t flash_address_address;
    uint32_t watchdog_address;
};

// Option Bytes 的控制地址由目标 profile 提供，Flash 流程不重复定义
struct rvswd_target_option_profile {
    uint32_t address_register;
    uint32_t status_register;
    uint32_t write_protection_register;
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
    uint8_t wchlink_family;
    bool ch5xx_protocol;
    bool fast_timing;
    const struct rvswd_target_identity_profile *identity;
    const struct rvswd_target_prepare_profile *prepare;
    const struct rvswd_target_option_profile *option;
    const struct rvswd_target_loader_profile *loader;
    bool loader_clears_debug_unlock;
    enum rvswd_memory_write_mode memory_write_mode;
    enum rvswd_flash_unlock_mode erase_unlock;
    enum rvswd_execute_prepare_mode execute_prepare;
    enum rvswd_option_write_mode option_write;
    uint32_t option_base;
    // Code Flash 起始地址和可用容量，loader 编程块越界时前置拒绝
    uint32_t code_flash_base;
    uint32_t code_flash_size;
};
