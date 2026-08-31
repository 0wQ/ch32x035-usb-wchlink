#pragma once

#include <stdbool.h>
#include <stdint.h>

struct rvswd_operation;
struct rvswd_target_profile;

typedef bool (*rvswd_flash_ch5xx_execute_fn)(
    struct rvswd_operation *operation, uint32_t entry, uint32_t stack_top,
    uint32_t mode, uint32_t address, uint32_t length, uint32_t data_address,
    uint32_t dpc_value, uint32_t *result);

// backend 复用命令口实现，具体 stub 和执行 ABI 由芯片族 module 传入
bool rvswd_flash_ch5xx_erase_all(
    struct rvswd_operation *operation,
    const struct rvswd_target_profile *profile, const uint8_t *stub_start,
    const uint8_t *stub_end, rvswd_flash_ch5xx_execute_fn execute);
bool rvswd_flash_rewrite_page(struct rvswd_operation *operation,
                              const struct rvswd_target_profile *profile,
                              uint32_t address, const uint8_t *data);
