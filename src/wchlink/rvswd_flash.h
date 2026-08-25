#pragma once

#include <stdbool.h>
#include <stdint.h>

bool rvswd_gpio_flash_rewrite_page(uint32_t address, const uint8_t *data);
bool rvswd_gpio_flash_erase_all(void);
bool rvswd_gpio_flash_read_protected(bool *protected);
bool rvswd_gpio_flash_write_protected(bool *protected);
bool rvswd_gpio_flash_set_read_protected(bool protected);
uint32_t rvswd_gpio_flash_last_error(void);

