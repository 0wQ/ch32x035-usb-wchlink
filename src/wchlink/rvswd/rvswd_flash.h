#pragma once

#include "rvswd_operation.h"
#include "rvswd_types.h"

#include <stdbool.h>
#include <stdint.h>

// Flash backend 只使用 target session 已锁定的只读 profile
bool rvswd_flash_rewrite_page(struct rvswd_operation *operation,
                              const struct rvswd_target_profile *profile,
                              uint32_t address, const uint8_t *data);
bool rvswd_flash_erase_all(struct rvswd_operation *operation,
                           const struct rvswd_target_profile *profile);
bool rvswd_flash_read_protected(struct rvswd_operation *operation,
                                const struct rvswd_target_profile *profile,
                                bool *protected);
bool rvswd_flash_write_protected(struct rvswd_operation *operation,
                                 const struct rvswd_target_profile *profile,
                                 bool *protected);
bool rvswd_flash_set_read_protected(struct rvswd_operation *operation,
                                    const struct rvswd_target_profile *profile,
                                    bool protected);
