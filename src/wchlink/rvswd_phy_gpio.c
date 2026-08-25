#include "rvswd_phy_gpio.h"

#include "bsp/bsp_delay.h"
#include "rvswd_frame.h"

#include <ch32x035.h>

#define RVSWD_CLOCK_PIN GPIO_Pin_2
#define RVSWD_DATA_PIN GPIO_Pin_3
#define RVSWD_DATA_PULLUP_PIN GPIO_Pin_1
#define RVSWD_CLOCK_CFG_SHIFT 8u
#define RVSWD_DATA_CFG_SHIFT 12u
#define RVSWD_DATA_PULLUP_CFG_SHIFT 4u
#define RVSWD_CLOCK_MODE_OUTPUT_50MHZ 0x03u
#define RVSWD_PINS (RVSWD_DATA_PULLUP_PIN | RVSWD_CLOCK_PIN | RVSWD_DATA_PIN)
#define RVSWD_WAKEUP_CLOCKS 100u
#define RVSWD_INTERFRAME_GUARD_US 0u

static bool rvswd_phy_fast_timing;

void rvswd_phy_gpio_set_fast_timing(bool enabled) {
    rvswd_phy_fast_timing = enabled;
}

static inline __attribute__((always_inline)) void rvswd_phy_half_period(void) {
    if (rvswd_phy_fast_timing) {
        // 快时序目标由 GPIO 写入间隔形成半周期
        __asm volatile("");
    } else {
        __asm volatile(
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n");
    }
}

static inline __attribute__((always_inline)) void rvswd_phy_clock_low(void) {
    GPIOA->BSHR = RVSWD_CLOCK_PIN << 16u;
}

static inline __attribute__((always_inline)) void rvswd_phy_clock_high(void) {
    GPIOA->BSHR = RVSWD_CLOCK_PIN;
}

static inline __attribute__((always_inline)) void rvswd_phy_data_low(void) {
    GPIOA->BSHR = RVSWD_DATA_PIN << 16u;
}

static inline __attribute__((always_inline)) void rvswd_phy_data_high(void) {
    GPIOA->BSHR = RVSWD_DATA_PIN;
}

static inline __attribute__((always_inline)) void rvswd_phy_fast_half_period(void) {
    __asm volatile("nop\n" "nop\n");
}

static inline __attribute__((always_inline)) void rvswd_phy_drive_bit_fast(
    uint8_t value) {
    GPIOA->BSHR = (RVSWD_CLOCK_PIN << 16u) |
                  (value != 0u ? RVSWD_DATA_PIN
                               : (RVSWD_DATA_PIN << 16u));
    rvswd_phy_fast_half_period();
    GPIOA->BSHR = RVSWD_CLOCK_PIN;
    rvswd_phy_fast_half_period();
}

static inline __attribute__((always_inline)) uint8_t
rvswd_phy_sample_bit_fast(void) {
    uint8_t value;

    GPIOA->BSHR = RVSWD_CLOCK_PIN << 16u;
    rvswd_phy_fast_half_period();
    GPIOA->BSHR = RVSWD_CLOCK_PIN;
    value = (GPIOA->INDR & RVSWD_DATA_PIN) != 0u ? 1u : 0u;
    rvswd_phy_fast_half_period();
    return value;
}

void rvswd_phy_gpio_config_data_output(void) {
    GPIOA->CFGLR = (GPIOA->CFGLR & ~(0xfu << RVSWD_DATA_CFG_SHIFT)) |
                   (0x01u << RVSWD_DATA_CFG_SHIFT);
}

void rvswd_phy_gpio_config_data_input(void) {
    // turnaround 开始前将锁存置高，释放 PA3 SWDIO，PA1 通过外部电阻提供上拉
    GPIOA->BSHR = RVSWD_DATA_PIN;
    GPIOA->CFGLR = (GPIOA->CFGLR & ~(0xfu << RVSWD_DATA_CFG_SHIFT)) |
                   (0x08u << RVSWD_DATA_CFG_SHIFT);
}

void rvswd_phy_gpio_start(void) {
    rvswd_phy_gpio_config_data_output();
    rvswd_phy_clock_high();
    rvswd_phy_data_high();
    rvswd_phy_half_period();
    rvswd_phy_data_low();
    rvswd_phy_half_period();
}

void rvswd_phy_gpio_stop(void) {
    // 采样结束时 SWCLK 仍为高，先接管数据线，再结束当前位
    rvswd_phy_gpio_config_data_output();
    rvswd_phy_clock_low();
    rvswd_phy_data_low();
    rvswd_phy_half_period();
    rvswd_phy_clock_high();
    rvswd_phy_half_period();
    rvswd_phy_data_high();
    rvswd_phy_half_period();
}

static inline __attribute__((always_inline)) void rvswd_phy_drive_bit(
    uint8_t value) {
    // 先拉低时钟，再更新数据，给 V30X 保留完整的数据建立窗口
    rvswd_phy_clock_low();
    if (value != 0u) {
        rvswd_phy_data_high();
    } else {
        rvswd_phy_data_low();
    }
    rvswd_phy_half_period();
    rvswd_phy_clock_high();
    rvswd_phy_half_period();
}

static inline __attribute__((always_inline)) uint8_t rvswd_phy_sample_bit(void) {
    uint8_t value;

    rvswd_phy_clock_low();
    rvswd_phy_half_period();
    rvswd_phy_clock_high();
    // 上升沿后立即读取目标驱动的 SWDIO 电平
    value = (GPIOA->INDR & RVSWD_DATA_PIN) != 0u ? 1u : 0u;
    rvswd_phy_half_period();
    return value;
}

void rvswd_phy_gpio_drive_range(const uint8_t *frame, uint8_t first_bit,
                                uint8_t bit_count) {
    if (rvswd_phy_fast_timing) {
        const uint8_t *byte = &frame[first_bit >> 3u];
        uint8_t mask = (uint8_t)(0x80u >> (first_bit & 7u));

        // 快时序路径按字节推进，保持固定采样窗口并减少 bit 定位开销
        for (uint8_t bit = 0u; bit < bit_count; ++bit) {
            rvswd_phy_drive_bit_fast((*byte & mask) != 0u ? 1u : 0u);
            mask >>= 1u;
            if (mask == 0u) {
                mask = 0x80u;
                ++byte;
            }
        }
        return;
    }
    for (uint8_t bit = 0u; bit < bit_count; ++bit) {
        rvswd_phy_drive_bit(
            rvswd_frame_get_bit(frame, (uint8_t)(first_bit + bit)));
    }
}

void rvswd_phy_gpio_sample_range(uint8_t *frame, uint8_t first_bit,
                                 uint8_t bit_count) {
    if (rvswd_phy_fast_timing) {
        uint8_t *byte = &frame[first_bit >> 3u];
        uint8_t mask = (uint8_t)(0x80u >> (first_bit & 7u));

        // 快时序路径复用字节 mask 写回采样结果，保持目标建立时间
        for (uint8_t bit = 0u; bit < bit_count; ++bit) {
            if (rvswd_phy_sample_bit_fast() != 0u) {
                *byte |= mask;
            } else {
                *byte &= (uint8_t)~mask;
            }
            mask >>= 1u;
            if (mask == 0u) {
                mask = 0x80u;
                ++byte;
            }
        }
        return;
    }
    for (uint8_t bit = 0u; bit < bit_count; ++bit) {
        rvswd_frame_set_bit(frame, (uint8_t)(first_bit + bit),
                            rvswd_phy_sample_bit());
    }
}

void rvswd_phy_gpio_drive_value(uint32_t value, uint8_t bit_count) {
    for (uint8_t bit = bit_count; bit > 0u; --bit) {
        rvswd_phy_drive_bit((uint8_t)((value >> (bit - 1u)) & 1u));
    }
}

uint32_t rvswd_phy_gpio_sample_value(uint8_t bit_count) {
    uint32_t value = 0u;

    for (uint8_t bit = 0u; bit < bit_count; ++bit) {
        value = (value << 1u) | rvswd_phy_sample_bit();
    }
    return value;
}

void rvswd_phy_gpio_wakeup(bool stop_condition) {
    __disable_irq();
    rvswd_phy_gpio_config_data_output();
    rvswd_phy_clock_high();
    rvswd_phy_data_high();
    for (uint8_t clock = 0u; clock < RVSWD_WAKEUP_CLOCKS; ++clock) {
        rvswd_phy_clock_low();
        rvswd_phy_half_period();
        rvswd_phy_clock_high();
        rvswd_phy_half_period();
    }
    if (stop_condition) {
        rvswd_phy_gpio_stop();
    }
    __enable_irq();
    bsp_delay_us(RVSWD_INTERFRAME_GUARD_US);
}

void rvswd_phy_gpio_init(void) {
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOA;
    // PA1 通过 5.1 kOhm 外部电阻给 PA3 SWDIO 提供上拉，首帧前保持高电平
    GPIOA->BSHR = RVSWD_DATA_PULLUP_PIN | RVSWD_CLOCK_PIN | RVSWD_DATA_PIN;
    GPIOA->CFGLR = (GPIOA->CFGLR & ~((0xfu << RVSWD_DATA_CFG_SHIFT) |
                                     (0xfu << RVSWD_CLOCK_CFG_SHIFT) |
                                     (0xfu << RVSWD_DATA_PULLUP_CFG_SHIFT))) |
                   (0x08u << RVSWD_DATA_CFG_SHIFT) |
                   (RVSWD_CLOCK_MODE_OUTPUT_50MHZ << RVSWD_CLOCK_CFG_SHIFT) |
                   (0x01u << RVSWD_DATA_PULLUP_CFG_SHIFT);
}

void rvswd_phy_gpio_disconnect(void) {
    GPIOA->BSHR = RVSWD_PINS;
    // 会话结束后释放 PA1 上拉控制和两根调试线，目标断电时禁止经 IO 反向供电
    GPIOA->CFGLR = (GPIOA->CFGLR & ~((0xfu << RVSWD_DATA_CFG_SHIFT) |
                                     (0xfu << RVSWD_CLOCK_CFG_SHIFT) |
                                     (0xfu << RVSWD_DATA_PULLUP_CFG_SHIFT))) |
                   (0x04u << RVSWD_DATA_CFG_SHIFT) |
                   (0x04u << RVSWD_CLOCK_CFG_SHIFT) |
                   (0x04u << RVSWD_DATA_PULLUP_CFG_SHIFT);
}
