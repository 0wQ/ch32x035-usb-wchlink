#include "status/status_led.h"

#include "bsp/bsp_delay.h"
#include "drv/drv_ws2816c.h"

#include <stdbool.h>
#include <stdint.h>

#include <ch32x035.h>
#include <ch32x035_misc.h>
#include <ch32x035_rcc.h>
#include <ch32x035_tim.h>

#define STATUS_LED_BLINK_INTERVAL_MS  50u
#define STATUS_LED_SUCCESS_HOLD_MS    300u
#define STATUS_LED_ERROR_HOLD_MS      500u
#define STATUS_LED_MUX_STATUS_HOLD_MS 500u
#define STATUS_LED_TIMER_TICK_HZ      1000000u
#define STATUS_LED_TIMER_RATE_HZ      1000u

static volatile bool status_led_initialized;
static volatile enum status_led_state status_led_state;
static volatile bool status_led_display_valid;
static volatile bool status_led_active_on;
static volatile bool status_led_mux_status_display;
static volatile bool status_led_mux_status_reversed;
static volatile uint32_t status_led_mux_status_deadline_ms;
static volatile uint32_t status_led_result_deadline_ms;
static volatile uint32_t status_led_next_frame_ms;

const drv_ws2816c_pixel_t status_led_color_idle = {.red = 0u, .green = 0u, .blue = 0x0500u};
const drv_ws2816c_pixel_t status_led_color_success = {.red = 0u, .green = 0x0300u, .blue = 0u};
const drv_ws2816c_pixel_t status_led_color_error = {.red = 0x0400u, .green = 0u, .blue = 0u};
const drv_ws2816c_pixel_t status_led_color_mux_status = {.red = 0u, .green = 0x0300u, .blue = 0x0500u};
const drv_ws2816c_pixel_t status_led_color_mux_status_reversed = {.red = 0x0400u, .green = 0u, .blue = 0x0500u};

static drv_ws2816c_pixel_t status_led_static_pixel(void) {
    switch (status_led_state) {
        case STATUS_LED_SUCCESS:
            return status_led_color_success;
        case STATUS_LED_ERROR:
            return status_led_color_error;
        default:
            return status_led_color_idle;
    }
}

void status_led_init(void) {
    status_led_initialized = true;
    status_led_state = STATUS_LED_IDLE;
    status_led_display_valid = false;
    status_led_active_on = true;
    status_led_mux_status_display = false;
    status_led_mux_status_reversed = false;
    status_led_mux_status_deadline_ms = 0u;
    status_led_result_deadline_ms = 0u;
    status_led_next_frame_ms = 0u;

    // TIM3 只负责低优先级唤醒状态灯调度，像素发送仍由 WS2816C DMA 完成
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    TIM_DeInit(TIM3);
    TIM_TimeBaseInitTypeDef timer = {0};
    timer.TIM_Prescaler = (uint16_t)(SystemCoreClock / STATUS_LED_TIMER_TICK_HZ - 1u);
    timer.TIM_Period = (uint16_t)(STATUS_LED_TIMER_TICK_HZ / STATUS_LED_TIMER_RATE_HZ - 1u);
    timer.TIM_ClockDivision = TIM_CKD_DIV1;
    timer.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM3, &timer);
    TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
    TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);

    NVIC_InitTypeDef nvic = {0};
    nvic.NVIC_IRQChannel = TIM3_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1u;
    nvic.NVIC_IRQChannelSubPriority = 3u;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);
    TIM_Cmd(TIM3, ENABLE);
}

void status_led_set_state(enum status_led_state state) {
    uint32_t now_ms;

    if (state > STATUS_LED_ERROR) {
        return;
    }
    now_ms = (uint32_t)bsp_time_ms();
    __disable_irq();
    if (!status_led_initialized) {
        __enable_irq();
        return;
    }
    if (state == status_led_state) {
        if (state == STATUS_LED_SUCCESS) {
            status_led_result_deadline_ms = now_ms + STATUS_LED_SUCCESS_HOLD_MS;
        } else if (state == STATUS_LED_ERROR) {
            status_led_result_deadline_ms = now_ms + STATUS_LED_ERROR_HOLD_MS;
        }
        __enable_irq();
        return;
    }

    status_led_state = state;
    status_led_display_valid = false;
    status_led_next_frame_ms = 0u;
    if (state == STATUS_LED_ACTIVE) {
        status_led_active_on = true;
        status_led_result_deadline_ms = 0u;
    } else if (state == STATUS_LED_SUCCESS) {
        status_led_result_deadline_ms = now_ms + STATUS_LED_SUCCESS_HOLD_MS;
    } else if (state == STATUS_LED_ERROR) {
        status_led_result_deadline_ms = now_ms + STATUS_LED_ERROR_HOLD_MS;
    } else {
        status_led_result_deadline_ms = 0u;
    }
    __enable_irq();
}

void status_led_show_mux_status(bool reversed) {
    uint32_t now_ms = (uint32_t)bsp_time_ms();

    __disable_irq();
    status_led_mux_status_display = true;
    status_led_mux_status_reversed = reversed;
    status_led_mux_status_deadline_ms = now_ms + STATUS_LED_MUX_STATUS_HOLD_MS;
    status_led_display_valid = false;
    __enable_irq();
}

static void status_led_timer_process(void) {
    drv_ws2816c_pixel_t pixel;
    uint32_t now_ms;

    if (!status_led_initialized) {
        return;
    }
    now_ms = (uint32_t)bsp_time_ms();
    if (status_led_mux_status_display &&
        (int32_t)(now_ms - status_led_mux_status_deadline_ms) >= 0) {
        status_led_mux_status_display = false;
        status_led_mux_status_reversed = false;
        status_led_mux_status_deadline_ms = 0u;
        status_led_display_valid = false;
        status_led_next_frame_ms = 0u;
    }
    if (status_led_mux_status_display) {
        if (status_led_display_valid) {
            return;
        }
        pixel = status_led_mux_status_reversed
                    ? status_led_color_mux_status_reversed
                    : status_led_color_mux_status;
        if (!drv_ws2816c_write_async(&pixel, 1u)) {
            return;
        }
        status_led_display_valid = true;
        return;
    }
    if (status_led_state != STATUS_LED_ACTIVE &&
        status_led_result_deadline_ms != 0u &&
        (int32_t)(now_ms - status_led_result_deadline_ms) >= 0) {
        status_led_state = STATUS_LED_IDLE;
        status_led_result_deadline_ms = 0u;
        status_led_display_valid = false;
        status_led_next_frame_ms = 0u;
    }
    if (status_led_state == STATUS_LED_ACTIVE) {
        if ((int32_t)(now_ms - status_led_next_frame_ms) < 0) {
            return;
        }
        pixel = status_led_active_on ? status_led_color_idle : (drv_ws2816c_pixel_t){0};
    } else {
        if (status_led_display_valid) {
            return;
        }
        pixel = status_led_static_pixel();
    }

    if (!drv_ws2816c_write_async(&pixel, 1u)) {
        return;
    }
    status_led_display_valid = true;
    if (status_led_state == STATUS_LED_ACTIVE) {
        status_led_active_on = !status_led_active_on;
        status_led_next_frame_ms = now_ms + STATUS_LED_BLINK_INTERVAL_MS;
    }
}

void TIM3_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

void TIM3_IRQHandler(void) {
    if (TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET) {
        TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
        // RVSWD 临界 bit-bang 区间会屏蔽该中断，退出后继续推进状态灯
        status_led_timer_process();
    }
}
