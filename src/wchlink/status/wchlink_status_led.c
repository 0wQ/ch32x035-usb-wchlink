#include "wchlink/status/wchlink_status_led.h"

#include "bsp/bsp_delay.h"
#include "drv/drv_ws2816c.h"

#include <stdbool.h>
#include <stdint.h>

#include <ch32x035.h>
#include <ch32x035_misc.h>
#include <ch32x035_rcc.h>
#include <ch32x035_tim.h>

#define WCHLINK_STATUS_LED_BLINK_INTERVAL_MS 50u
#define WCHLINK_STATUS_LED_SUCCESS_HOLD_MS   300u
#define WCHLINK_STATUS_LED_ERROR_HOLD_MS     500u
#define WCHLINK_STATUS_LED_TIMER_TICK_HZ     1000000u
#define WCHLINK_STATUS_LED_TIMER_RATE_HZ     1000u

static volatile bool wchlink_status_led_initialized;
static volatile enum wchlink_status_led_state wchlink_status_led_state;
static volatile bool wchlink_status_led_display_valid;
static volatile bool wchlink_status_led_active_on;
static volatile uint32_t wchlink_status_led_result_deadline_ms;
static volatile uint32_t wchlink_status_led_next_frame_ms;

static drv_ws2816c_pixel_t wchlink_status_led_static_pixel(void) {
    switch (wchlink_status_led_state) {
        case WCHLINK_STATUS_LED_SUCCESS:
            return (drv_ws2816c_pixel_t){
                .red = 0u, .green = UINT16_MAX, .blue = 0u};
        case WCHLINK_STATUS_LED_ERROR:
            return (drv_ws2816c_pixel_t){
                .red = UINT16_MAX, .green = 0u, .blue = 0u};
        default:
            return (drv_ws2816c_pixel_t){
                .red = 0u, .green = 0u, .blue = UINT16_MAX};
    }
}

void wchlink_status_led_init(void) {
    wchlink_status_led_initialized = true;
    wchlink_status_led_state = WCHLINK_STATUS_LED_IDLE;
    wchlink_status_led_display_valid = false;
    wchlink_status_led_active_on = true;
    wchlink_status_led_result_deadline_ms = 0u;
    wchlink_status_led_next_frame_ms = 0u;

    // TIM3 只负责低优先级唤醒状态灯调度，像素发送仍由 WS2816C DMA 完成
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    TIM_DeInit(TIM3);
    TIM_TimeBaseInitTypeDef timer = {0};
    timer.TIM_Prescaler = (uint16_t)(SystemCoreClock / WCHLINK_STATUS_LED_TIMER_TICK_HZ - 1u);
    timer.TIM_Period = (uint16_t)(WCHLINK_STATUS_LED_TIMER_TICK_HZ / WCHLINK_STATUS_LED_TIMER_RATE_HZ - 1u);
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

void wchlink_status_led_set_state(enum wchlink_status_led_state state) {
    uint32_t now_ms;

    if (state > WCHLINK_STATUS_LED_ERROR) {
        return;
    }
    now_ms = (uint32_t)bsp_time_ms();
    __disable_irq();
    if (!wchlink_status_led_initialized) {
        __enable_irq();
        return;
    }
    if (state == wchlink_status_led_state) {
        if (state == WCHLINK_STATUS_LED_SUCCESS) {
            wchlink_status_led_result_deadline_ms =
                now_ms + WCHLINK_STATUS_LED_SUCCESS_HOLD_MS;
        } else if (state == WCHLINK_STATUS_LED_ERROR) {
            wchlink_status_led_result_deadline_ms =
                now_ms + WCHLINK_STATUS_LED_ERROR_HOLD_MS;
        }
        __enable_irq();
        return;
    }

    wchlink_status_led_state = state;
    wchlink_status_led_display_valid = false;
    wchlink_status_led_next_frame_ms = 0u;
    if (state == WCHLINK_STATUS_LED_ACTIVE) {
        wchlink_status_led_active_on = true;
        wchlink_status_led_result_deadline_ms = 0u;
    } else if (state == WCHLINK_STATUS_LED_SUCCESS) {
        wchlink_status_led_result_deadline_ms =
            now_ms + WCHLINK_STATUS_LED_SUCCESS_HOLD_MS;
    } else if (state == WCHLINK_STATUS_LED_ERROR) {
        wchlink_status_led_result_deadline_ms =
            now_ms + WCHLINK_STATUS_LED_ERROR_HOLD_MS;
    } else {
        wchlink_status_led_result_deadline_ms = 0u;
    }
    __enable_irq();
}

static void wchlink_status_led_timer_process(void) {
    drv_ws2816c_pixel_t pixel;
    uint32_t now_ms;

    if (!wchlink_status_led_initialized) {
        return;
    }
    now_ms = (uint32_t)bsp_time_ms();
    if (wchlink_status_led_state != WCHLINK_STATUS_LED_ACTIVE &&
        wchlink_status_led_result_deadline_ms != 0u &&
        (int32_t)(now_ms - wchlink_status_led_result_deadline_ms) >= 0) {
        wchlink_status_led_state = WCHLINK_STATUS_LED_IDLE;
        wchlink_status_led_result_deadline_ms = 0u;
        wchlink_status_led_display_valid = false;
        wchlink_status_led_next_frame_ms = 0u;
    }
    if (wchlink_status_led_state == WCHLINK_STATUS_LED_ACTIVE) {
        if ((int32_t)(now_ms - wchlink_status_led_next_frame_ms) < 0) {
            return;
        }
        pixel = wchlink_status_led_active_on
                    ? (drv_ws2816c_pixel_t){
                          .red = 0u, .green = 0u, .blue = UINT16_MAX}
                    : (drv_ws2816c_pixel_t){0};
    } else {
        if (wchlink_status_led_display_valid) {
            return;
        }
        pixel = wchlink_status_led_static_pixel();
    }

    if (!drv_ws2816c_write_async(&pixel, 1u)) {
        return;
    }
    wchlink_status_led_display_valid = true;
    if (wchlink_status_led_state == WCHLINK_STATUS_LED_ACTIVE) {
        wchlink_status_led_active_on = !wchlink_status_led_active_on;
        wchlink_status_led_next_frame_ms =
            now_ms + WCHLINK_STATUS_LED_BLINK_INTERVAL_MS;
    }
}

void TIM3_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

void TIM3_IRQHandler(void) {
    if (TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET) {
        TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
        // RVSWD 临界 bit-bang 区间会屏蔽该中断，退出后继续推进状态灯
        wchlink_status_led_timer_process();
    }
}
