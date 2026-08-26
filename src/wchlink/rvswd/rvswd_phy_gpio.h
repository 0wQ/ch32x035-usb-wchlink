#pragma once

#include <stdbool.h>
#include <stdint.h>

void rvswd_phy_gpio_set_fast_timing(bool enabled);
void rvswd_phy_gpio_init(void);
void rvswd_phy_gpio_disconnect(void);
void rvswd_phy_gpio_config_data_output(void);
void rvswd_phy_gpio_config_data_input(void);
void rvswd_phy_gpio_start(void);
void rvswd_phy_gpio_stop(void);
void rvswd_phy_gpio_drive_range(const uint8_t *frame, uint8_t first_bit,
                                uint8_t bit_count);
void rvswd_phy_gpio_sample_range(uint8_t *frame, uint8_t first_bit,
                                 uint8_t bit_count);
void rvswd_phy_gpio_drive_value(uint32_t value, uint8_t bit_count);
uint32_t rvswd_phy_gpio_sample_value(uint8_t bit_count);
void rvswd_phy_gpio_wakeup(bool stop_condition);
