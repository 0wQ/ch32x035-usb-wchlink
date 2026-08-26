#pragma once

#include "rvswd_transport.h"

#include <stdbool.h>
#include <stdint.h>

bool rvswd_debug_wait_abstract_idle_timeout(struct rvswd_transport *transport,
                                            uint32_t *abstractcs,
                                            uint32_t timeout_us);
bool rvswd_debug_wait_abstract_idle(struct rvswd_transport *transport,
                                    uint32_t *abstractcs);
bool rvswd_debug_write_register(struct rvswd_transport *transport,
                                uint16_t regno, uint32_t value);
bool rvswd_debug_read_register(struct rvswd_transport *transport,
                               uint16_t regno, uint32_t *value);
bool rvswd_debug_write_raw_gpr(struct rvswd_transport *transport,
                               uint8_t regno, uint32_t value);
bool rvswd_debug_read_raw_gpr(struct rvswd_transport *transport, uint8_t regno,
                              uint32_t *value);
bool rvswd_debug_halt(struct rvswd_transport *transport);
bool rvswd_debug_wait_dmstatus(struct rvswd_transport *transport,
                               uint32_t mask, bool set, uint32_t timeout_ms);
bool rvswd_debug_execute(struct rvswd_transport *transport, uint32_t entry,
                         uint32_t stack_top, uint32_t mode, uint32_t address,
                         uint32_t length, uint32_t data_address,
                         uint32_t *result);
