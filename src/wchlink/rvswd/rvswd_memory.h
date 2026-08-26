#pragma once

#include "rvswd_operation.h"
#include "rvswd_types.h"

#include <stdbool.h>
#include <stdint.h>

// profile 来自本次 target session，target_identified 控制未知目标的兼容读路径
bool rvswd_memory_read32(struct rvswd_operation *operation,
                         const struct rvswd_target_profile *profile,
                         bool target_identified, uint32_t address,
                         uint32_t *value);
bool rvswd_memory_write32(struct rvswd_operation *operation, uint32_t address,
                          uint32_t value);
bool rvswd_memory_write(struct rvswd_operation *operation,
                        const struct rvswd_target_profile *profile,
                        uint32_t address, const uint8_t *data,
                        uint32_t length);
