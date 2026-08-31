#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct rvswd_operation;
struct rvswd_target_profile;

bool rvswd_flash_read_protected(struct rvswd_operation *operation,
                                const struct rvswd_target_profile *profile,
                                bool *protected);
bool rvswd_flash_write_protected(struct rvswd_operation *operation,
                                 const struct rvswd_target_profile *profile,
                                 bool *protected);
bool rvswd_flash_set_read_protected(struct rvswd_operation *operation,
                                    const struct rvswd_target_profile *profile,
                                    bool protected);
bool rvswd_flash_set_option_bytes(struct rvswd_operation *operation,
                                  const struct rvswd_target_profile *profile,
                                  const uint8_t *values, size_t count);
bool rvswd_flash_read_memory_type(struct rvswd_operation *operation,
                                  const struct rvswd_target_profile *profile,
                                  bool extended, uint8_t *memory_type);
bool rvswd_flash_set_memory_type(struct rvswd_operation *operation,
                                 const struct rvswd_target_profile *profile,
                                 bool extended, uint8_t memory_type);
