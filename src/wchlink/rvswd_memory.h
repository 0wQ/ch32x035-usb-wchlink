#pragma once

#include <stdbool.h>
#include <stdint.h>

bool rvswd_memory_read32(uint32_t address, uint32_t *value);
bool rvswd_memory_write32(uint32_t address, uint32_t value);
bool rvswd_memory_write(uint32_t address, const uint8_t *data, uint32_t length);
uint8_t rvswd_memory_last_error(void);
uint8_t rvswd_memory_failure_dmi_status(void);
uint32_t rvswd_memory_failure_address(void);
uint32_t rvswd_memory_failure_abstractcs(void);

