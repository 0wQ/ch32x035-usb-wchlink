#pragma once

#include <stdbool.h>
#include <stdint.h>

void rvswd_phy_gpio_init(void);
void rvswd_phy_gpio_disconnect(void);
void rvswd_phy_gpio_config_data_output(void);
void rvswd_phy_gpio_config_data_input(void);
void rvswd_phy_gpio_start(bool fast_timing);
void rvswd_phy_gpio_stop(bool fast_timing);
void rvswd_phy_gpio_drive_range(bool fast_timing, const uint8_t *frame,
                                uint8_t first_bit, uint8_t bit_count);
void rvswd_phy_gpio_sample_range(bool fast_timing, uint8_t *frame,
                                 uint8_t first_bit, uint8_t bit_count);
void rvswd_phy_gpio_drive_value(bool fast_timing, uint32_t value,
                                uint8_t bit_count);
uint32_t rvswd_phy_gpio_sample_value(bool fast_timing, uint8_t bit_count);
void rvswd_phy_gpio_wakeup(bool fast_timing, bool stop_condition);
