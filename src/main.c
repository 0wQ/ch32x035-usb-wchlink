#include "bsp/bsp_delay.h"
#include "drv/drv_dp_pullup.h"
#include "drv/drv_power_switch.h"
#include "drv/drv_uart_mux.h"
#include "drv/drv_ws2816c.h"
#include "wchlink/wchlink_usb.h"

#include <system_ch32x035.h>

#define WS2816C_DIM_LEVEL 0x0100u
#define WS2816C_EFFECT_STEP_MS 100u

static void ws2816c_show_startup_effect(void) {
    static const drv_ws2816c_pixel_t colors[] = {
        {.red = WS2816C_DIM_LEVEL, .green = 0u, .blue = 0u},
        {.red = 0u, .green = WS2816C_DIM_LEVEL, .blue = 0u},
        {.red = 0u, .green = 0u, .blue = WS2816C_DIM_LEVEL},
        {.red = WS2816C_DIM_LEVEL, .green = 0u, .blue = WS2816C_DIM_LEVEL},
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

int main(void) {
    SystemInit();
    drv_dp_pullup_init();
    drv_power_switch_init();
    drv_uart_mux_init();
    bsp_delay_init();
    bsp_delay_ms(10u);
    drv_power_switch_set_enabled(true);
    wchlink_usb_init();
    drv_ws2816c_init();
    ws2816c_show_startup_effect();
    for (;;) {
        wchlink_usb_process();
    }
}
