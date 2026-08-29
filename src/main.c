#include "bsp/bsp_delay.h"
#include "drv/drv_button.h"
#include "drv/drv_dp_pullup.h"
#include "drv/drv_power_switch.h"
#include "drv/drv_sbu_mux.h"
#include "drv/drv_ws2816c.h"
#include "status/status_led.h"
#include "wchlink/usb/wchlink_usb.h"

#include <ch32x035_misc.h>
#include <system_ch32x035.h>

#define WS2816C_EFFECT_STEP_MS 100u
#define BUTTON_DEBOUNCE_MS     20u

static void ws2816c_show_startup_effect(void) {
    const drv_ws2816c_pixel_t colors[] = {
        status_led_color_error,
        status_led_color_success,
        status_led_color_idle,
    };

    for (size_t index = 0u; index < sizeof(colors) / sizeof(colors[0]); ++index) {
        uint64_t deadline;

        drv_ws2816c_write(&colors[index], 1u);
        deadline = bsp_time_ms() + WS2816C_EFFECT_STEP_MS;
        // 等待颜色切换期间继续处理 USB 请求
        while (bsp_time_ms() < deadline) {
            wchlink_usb_process();
        }
    }
}

static void process_button(void) {
    static bool stable_pressed;
    static bool sampled_pressed;
    static uint64_t sample_changed_at;
    bool pressed = drv_button_is_pressed();
    uint64_t now_ms = bsp_time_ms();

    if (pressed != sampled_pressed) {
        sampled_pressed = pressed;
        sample_changed_at = now_ms;
        return;
    }

    if (pressed == stable_pressed || now_ms - sample_changed_at < BUTTON_DEBOUNCE_MS) {
        return;
    }

    stable_pressed = pressed;
    // 仅在确认按下时切换，保持按住按钮期间只产生一次操作
    if (stable_pressed) {
        bool reversed = !drv_sbu_mux_is_reversed();

        drv_sbu_mux_set_reversed(reversed);
        status_led_show_mux_status(reversed);
    }
}

int main(void) {
    SystemInit();
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    drv_dp_pullup_init();
    drv_power_switch_init();
    drv_sbu_mux_init();
    drv_button_init();
    bsp_delay_init();
    bsp_delay_ms(10u);
    drv_power_switch_set_enabled(true);
    wchlink_usb_init();
    drv_ws2816c_init();
    ws2816c_show_startup_effect();
    status_led_init();
    status_led_set_state(STATUS_LED_IDLE);
    for (;;) {
        wchlink_usb_process();
        process_button();
    }
}
