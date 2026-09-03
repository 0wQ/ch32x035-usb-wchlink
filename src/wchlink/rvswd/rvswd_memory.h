#pragma once

#include "wchlink/rvswd/rvswd_operation.h"
#include "wchlink/rvswd/rvswd_types.h"

#include <stdbool.h>
#include <stdint.h>

typedef bool (*rvswd_memory_read32_fn)(
    struct rvswd_operation *operation, uint32_t address, uint32_t *value);
typedef bool (*rvswd_memory_write32_fn)(
    struct rvswd_operation *operation, uint32_t address, uint32_t value);
typedef bool (*rvswd_memory_write_fn)(
    struct rvswd_operation *operation, uint32_t address, const uint8_t *data,
    uint32_t length);

struct rvswd_memory_ops {
    rvswd_memory_read32_fn read32;
    rvswd_memory_write32_fn write32;
    rvswd_memory_write_fn write;
};

// 公共内存层只提供访问算法，目标 module 在 operation 中绑定具体算法
bool rvswd_memory_read32_synchronized(struct rvswd_operation *operation,
                                      uint32_t address, uint32_t *value);
bool rvswd_memory_read32_access_memory(struct rvswd_operation *operation,
                                       uint32_t address, uint32_t *value);
bool rvswd_memory_read32(struct rvswd_operation *operation,
                         const struct rvswd_target_profile *profile,
                         bool target_identified, uint32_t address,
                         uint32_t *value);
bool rvswd_memory_write32(struct rvswd_operation *operation, uint32_t address,
                          uint32_t value);
bool rvswd_memory_write32_slow(struct rvswd_operation *operation,
                               uint32_t address, uint32_t value);
bool rvswd_memory_write32_direct(struct rvswd_operation *operation,
                                 uint32_t address, uint32_t value);
bool rvswd_memory_write_direct(struct rvswd_operation *operation,
                               uint32_t address, const uint8_t *data,
                               uint32_t length);
bool rvswd_memory_write_streaming_retry(struct rvswd_operation *operation,
                                        uint32_t address,
                                        const uint8_t *data,
                                        uint32_t length);
bool rvswd_memory_write_slow(struct rvswd_operation *operation,
                             uint32_t address, const uint8_t *data,
                             uint32_t length);
bool rvswd_memory_write(struct rvswd_operation *operation,
                        const struct rvswd_target_profile *profile,
                        uint32_t address, const uint8_t *data,
                        uint32_t length);
