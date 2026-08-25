#pragma once

#include <stdbool.h>
#include <stdint.h>

void rvswd_gpio_init(void);
void rvswd_gpio_disconnect(void);
bool rvswd_gpio_connect(void);
uint8_t rvswd_gpio_connect_last_error(void);
uint32_t rvswd_gpio_target_chip_id(void);
void rvswd_gpio_set_target_wchlink_family_hint(uint8_t family);
uint8_t rvswd_gpio_target_wchlink_family(void);
bool rvswd_gpio_target_supports_memory_streaming(void);
bool rvswd_gpio_read_dmi(uint8_t address, uint32_t *value);
bool rvswd_gpio_write_dmi(uint8_t address, uint32_t value);
bool rvswd_gpio_dmi_failure_retryable(void);
bool rvswd_gpio_read_memory32(uint32_t address, uint32_t *value);
bool rvswd_gpio_write_memory32(uint32_t address, uint32_t value);
bool rvswd_gpio_write_memory(uint32_t address, const uint8_t *data, uint32_t length);
uint8_t rvswd_gpio_memory_last_error(void);
uint8_t rvswd_gpio_memory_failure_dmi_status(void);
uint32_t rvswd_gpio_memory_failure_address(void);
uint32_t rvswd_gpio_memory_failure_abstractcs(void);
bool rvswd_gpio_write_register(uint16_t regno, uint32_t value);
bool rvswd_gpio_read_register(uint16_t regno, uint32_t *value);
bool rvswd_gpio_halt(void);
bool rvswd_gpio_execute(uint32_t entry, uint32_t stack_top, uint32_t mode,
                        uint32_t address, uint32_t length, uint32_t data_address,
                        uint32_t *result);
bool rvswd_gpio_flash_erase_all(void);
bool rvswd_gpio_flash_rewrite_page(uint32_t address, const uint8_t *data);
bool rvswd_gpio_flash_read_protected(bool *protected);
bool rvswd_gpio_flash_write_protected(bool *protected);
bool rvswd_gpio_flash_set_read_protected(bool protected);
uint32_t rvswd_gpio_flash_last_error(void);
bool rvswd_gpio_reset_and_halt(void);
bool rvswd_gpio_soft_reset_and_run(void);
bool rvswd_gpio_reset_and_run(void);
