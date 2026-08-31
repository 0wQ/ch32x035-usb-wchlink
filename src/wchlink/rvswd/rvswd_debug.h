#pragma once

#include "wchlink/rvswd/rvswd_operation.h"

#include <stdbool.h>
#include <stdint.h>

bool rvswd_debug_wait_abstract_idle_timeout(struct rvswd_operation *operation,
                                            uint32_t *abstractcs,
                                            uint32_t timeout_us);
bool rvswd_debug_wait_abstract_idle(struct rvswd_operation *operation,
                                    uint32_t *abstractcs);
bool rvswd_debug_write_register(struct rvswd_operation *operation,
                                uint16_t regno, uint32_t value);
bool rvswd_debug_read_register(struct rvswd_operation *operation,
                               uint16_t regno, uint32_t *value);
bool rvswd_debug_write_raw_gpr(struct rvswd_operation *operation,
                               uint8_t regno, uint32_t value);
bool rvswd_debug_read_raw_gpr(struct rvswd_operation *operation, uint8_t regno,
                              uint32_t *value);
bool rvswd_debug_halt(struct rvswd_operation *operation);
bool rvswd_debug_resume(struct rvswd_operation *operation,
                        uint32_t dmcontrol, uint32_t *dmstatus);
bool rvswd_debug_wait_dmstatus(struct rvswd_operation *operation,
                               uint32_t mask, bool set, uint32_t timeout_ms);
bool rvswd_debug_restore_unlock(struct rvswd_operation *operation);
