#pragma once

#include <stdbool.h>
#include <stdint.h>

bool rvswd_flash_rewrite_page(uint32_t address, const uint8_t *data);
bool rvswd_flash_erase_all(void);
bool rvswd_flash_read_protected(bool *protected);
bool rvswd_flash_write_protected(bool *protected);
bool rvswd_flash_set_read_protected(bool protected);
uint32_t rvswd_flash_last_error(void);
