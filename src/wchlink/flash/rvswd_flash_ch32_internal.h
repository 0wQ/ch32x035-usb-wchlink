#pragma once

#include <stdbool.h>
#include <stdint.h>

struct rvswd_operation;
struct rvswd_target_profile;

// 该私有 seam 只共享 CH32 controller 的硬件事实和公共操作
// 主存储区擦除与 Option Bytes 各自保留完整的控制流程和阶段错误
static const uint32_t rvswd_flash_ch32_key_register = 0x40022004u;
static const uint32_t rvswd_flash_ch32_option_key_register = 0x40022008u;
static const uint32_t rvswd_flash_ch32_control_register = 0x40022010u;
static const uint32_t rvswd_flash_ch32_mode_key_register = 0x40022024u;

static const uint32_t rvswd_flash_ch32_key1 = 0x45670123u;
static const uint32_t rvswd_flash_ch32_key2 = 0xcdef89abu;

static const uint32_t rvswd_flash_ch32_status_write_protection_error = 1u << 4u;
static const uint32_t rvswd_flash_ch32_control_start = 1u << 6u;
static const uint32_t rvswd_flash_ch32_control_lock = 1u << 7u;
static const uint32_t rvswd_flash_ch32_control_fast_lock = 1u << 15u;
static const uint32_t rvswd_flash_ch32_operation_timeout_us = 6000000u;

bool rvswd_flash_ch32_wait_ready(
    struct rvswd_operation *operation,
    const struct rvswd_target_profile *profile, uint32_t *status,
    uint8_t read_error, uint8_t timeout_error);
bool rvswd_flash_ch32_unlock_main_and_fast(
    struct rvswd_operation *operation, uint32_t control);
