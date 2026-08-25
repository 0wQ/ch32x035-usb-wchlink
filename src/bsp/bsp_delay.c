#include "bsp/bsp_delay.h"

#include <stdbool.h>

#include <ch32x035.h>

#define SYSTICK_CTLR (*(volatile uint32_t *)0xE000F000UL)
#define SYSTICK_SR   (*(volatile uint32_t *)0xE000F004UL)
#define SYSTICK_CNTL (*(volatile uint32_t *)0xE000F008UL)
#define SYSTICK_CNTH (*(volatile uint32_t *)0xE000F00CUL)
#define SYSTICK_CMPL (*(volatile uint32_t *)0xE000F010UL)
#define SYSTICK_CMPH (*(volatile uint32_t *)0xE000F014UL)

#define SYSTICK_CTLR_STCLK_HCLK (1UL << 2U)
#define SYSTICK_CTLR_ENABLE     (1UL << 0U)

static bool delay_initialized;
static uint32_t systick_ticks_per_us;
static uint32_t systick_ticks_per_ms;

static uint64_t systick_read_counter(void) {
    uint32_t high_before;
    uint32_t high_after;
    uint32_t low;

    // RV32 分两次读取 64 位计数器，高位跨越时重新读取
    do {
        high_before = SYSTICK_CNTH;
        low = SYSTICK_CNTL;
        high_after = SYSTICK_CNTH;
    } while (high_before != high_after);

    return ((uint64_t)high_after << 32U) | low;
}

void bsp_delay_init(void) {
    if (delay_initialized) {
        return;
    }

    SYSTICK_CTLR = 0;
    SYSTICK_SR = 0;
    SYSTICK_CNTL = 0;
    SYSTICK_CNTH = 0;
    SYSTICK_CMPL = UINT32_MAX;
    SYSTICK_CMPH = UINT32_MAX;

    // SDK 支持的系统时钟均为整数 MHz，直接使用整数 tick 换算
    systick_ticks_per_us = SystemCoreClock / 1000000U;
    systick_ticks_per_ms = SystemCoreClock / 1000U;
    SYSTICK_CTLR = SYSTICK_CTLR_ENABLE | SYSTICK_CTLR_STCLK_HCLK;
    delay_initialized = true;
}

void bsp_delay_us(uint32_t us) {
    uint64_t ticks;

    if (us == 0U) {
        return;
    }

    uint32_t start = SYSTICK_CNTL;
    ticks = (uint64_t)us * systick_ticks_per_us;
    if (ticks <= UINT32_MAX) {
        while ((uint32_t)(SYSTICK_CNTL - start) < (uint32_t)ticks) {
        }
        return;
    }

    uint64_t start64 = systick_read_counter();
    while ((systick_read_counter() - start64) < ticks) {
    }
}

void bsp_delay_ms(uint32_t ms) {
    uint64_t ticks;

    if (ms == 0U) {
        return;
    }

    uint32_t start = SYSTICK_CNTL;
    ticks = (uint64_t)ms * systick_ticks_per_ms;
    if (ticks <= UINT32_MAX) {
        while ((uint32_t)(SYSTICK_CNTL - start) < (uint32_t)ticks) {
        }
        return;
    }

    uint64_t start64 = systick_read_counter();
    while ((systick_read_counter() - start64) < ticks) {
    }
}

uint64_t bsp_time_us(void) {
    return systick_read_counter() / systick_ticks_per_us;
}

uint64_t bsp_time_ms(void) {
    return systick_read_counter() / systick_ticks_per_ms;
}
