#include "bsp/bsp_delay.h"
#include <stdbool.h>
#include <ch32x035.h>

#define CTLR (*(volatile uint32_t *)0xE000F000UL)
#define SR   (*(volatile uint32_t *)0xE000F004UL)
#define CNTL (*(volatile uint32_t *)0xE000F008UL)
#define CNTH (*(volatile uint32_t *)0xE000F00CUL)
#define CMPL (*(volatile uint32_t *)0xE000F010UL)
#define CMPH (*(volatile uint32_t *)0xE000F014UL)
#define ENABLE (1UL << 0U)
#define HCLK   (1UL << 2U)

static bool initialized;
static uint32_t ticks_per_us;
static uint32_t ticks_per_ms;

static void read_counter(uint32_t *high, uint32_t *low) {
    uint32_t before;
    uint32_t after;
    do {
        before = CNTH;
        *low = CNTL;
        after = CNTH;
    } while (before != after);
    *high = after;
}

static uint32_t counter_to_units(uint32_t high, uint32_t low, uint32_t divisor) {
    uint32_t remainder = high % divisor;
    uint32_t result = 0u;
    for (uint32_t mask = 0x80000000u; mask != 0u; mask >>= 1u) {
        uint32_t next = (remainder << 1u) | ((low & mask) != 0u ? 1u : 0u);
        if (next >= divisor) {
            next -= divisor;
            result |= mask;
        }
        remainder = next;
    }
    return result;
}

static void wait_ticks(uint32_t ticks) {
    uint32_t start = CNTL;
    while ((uint32_t)(CNTL - start) < ticks) {
    }
}

static void delay_units(uint32_t units, uint32_t ticks) {
    if (!initialized || units == 0u || ticks == 0u) {
        return;
    }
    const uint32_t max_units = UINT32_MAX / ticks;
    while (units != 0u) {
        const uint32_t chunk = units > max_units ? max_units : units;
        wait_ticks(chunk * ticks);
        units -= chunk;
    }
}

void bsp_delay_init(void) {
    if (initialized) {
        return;
    }
    SystemCoreClockUpdate();
    CTLR = 0u;
    SR = 0u;
    CNTL = 0u;
    CNTH = 0u;
    CMPL = UINT32_MAX;
    CMPH = UINT32_MAX;
    ticks_per_us = SystemCoreClock / 1000000u;
    ticks_per_ms = SystemCoreClock / 1000u;
    if (ticks_per_us == 0u || ticks_per_ms == 0u) {
        return;
    }
    CTLR = ENABLE | HCLK;
    initialized = true;
}

void bsp_delay_us(uint32_t us) { delay_units(us, ticks_per_us); }
void bsp_delay_ms(uint32_t ms) { delay_units(ms, ticks_per_ms); }

uint32_t bsp_time_us(void) {
    uint32_t high, low;
    if (!initialized) return 0u;
    read_counter(&high, &low);
    return counter_to_units(high, low, ticks_per_us);
}

uint32_t bsp_time_ms(void) {
    uint32_t high, low;
    if (!initialized) return 0u;
    read_counter(&high, &low);
    return counter_to_units(high, low, ticks_per_ms);
}
