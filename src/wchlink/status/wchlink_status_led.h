#pragma once

enum wchlink_status_led_state {
    WCHLINK_STATUS_LED_IDLE,
    WCHLINK_STATUS_LED_ACTIVE,
    WCHLINK_STATUS_LED_SUCCESS,
    WCHLINK_STATUS_LED_ERROR,
};

void wchlink_status_led_init(void);
void wchlink_status_led_set_state(enum wchlink_status_led_state state);
