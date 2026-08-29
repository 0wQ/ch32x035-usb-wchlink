#pragma once

#include "drv/drv_ws2816c.h"

#include <stdbool.h>

extern const drv_ws2816c_pixel_t status_led_color_idle;
extern const drv_ws2816c_pixel_t status_led_color_success;
extern const drv_ws2816c_pixel_t status_led_color_error;
extern const drv_ws2816c_pixel_t status_led_color_mux_status;
extern const drv_ws2816c_pixel_t status_led_color_mux_status_reversed;

enum status_led_state {
    STATUS_LED_IDLE,
    STATUS_LED_ACTIVE,
    STATUS_LED_SUCCESS,
    STATUS_LED_ERROR,
};

void status_led_init(void);
void status_led_set_state(enum status_led_state state);
void status_led_show_mux_status(bool reversed);
