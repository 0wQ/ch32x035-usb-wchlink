#pragma once

#include "rvswd_transport.h"
#include "rvswd_types.h"

#include <stdbool.h>
#include <stdint.h>

// profile 来自本次 target session，target_identified 控制未知目标的兼容读路径
bool rvswd_memory_read32(struct rvswd_transport *transport,
                         const struct rvswd_target_profile *profile,
                         bool target_identified, uint32_t address,
                         uint32_t *value);
bool rvswd_memory_write32(struct rvswd_transport *transport, uint32_t address,
                          uint32_t value);
bool rvswd_memory_write(struct rvswd_transport *transport,
                        const struct rvswd_target_profile *profile,
                        uint32_t address, const uint8_t *data,
                        uint32_t length);
uint8_t rvswd_memory_last_error(void);
uint8_t rvswd_memory_failure_dmi_status(void);
uint32_t rvswd_memory_failure_address(void);
uint32_t rvswd_memory_failure_abstractcs(void);
