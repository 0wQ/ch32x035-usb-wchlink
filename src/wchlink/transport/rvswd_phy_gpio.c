#include "rvswd_phy_gpio.h"

#include "rvswd_frame.h"

#include <ch32x035.h>

// PA2 驱动 SWCLK，PA3 双向承载 SWDIO，PA1 通过 5.1 kOhm 外部电阻控制上拉
static const uint32_t rvswd_phy_clock_pin = GPIO_Pin_2;
static const uint32_t rvswd_phy_data_pin = GPIO_Pin_3;
static const uint32_t rvswd_phy_data_pullup_pin = GPIO_Pin_1;
static const uint8_t rvswd_phy_clock_cfg_shift = 8u;
static const uint8_t rvswd_phy_data_cfg_shift = 12u;
static const uint8_t rvswd_phy_data_pullup_cfg_shift = 4u;
static const uint32_t rvswd_phy_clock_mode_output_50mhz = 0x03u;
static const uint32_t rvswd_phy_pins = GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3;
static const uint8_t rvswd_phy_wakeup_clocks = 100u;

static inline __attribute__((always_inline)) void rvswd_phy_half_period(
    bool fast_timing) {
    if (fast_timing) {
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
    GPIOA->BSHR = rvswd_phy_clock_pin << 16u;
}

static inline __attribute__((always_inline)) void rvswd_phy_clock_high(void) {
    GPIOA->BSHR = rvswd_phy_clock_pin;
}

static inline __attribute__((always_inline)) void rvswd_phy_data_low(void) {
    GPIOA->BSHR = rvswd_phy_data_pin << 16u;
}

static inline __attribute__((always_inline)) void rvswd_phy_data_high(void) {
    GPIOA->BSHR = rvswd_phy_data_pin;
}

static inline __attribute__((always_inline)) void rvswd_phy_fast_half_period(void) {
    __asm volatile("nop\n"
                   "nop\n");
}

static inline __attribute__((always_inline)) void rvswd_phy_drive_bit_fast(
    uint8_t value) {
    GPIOA->BSHR = (rvswd_phy_clock_pin << 16u) |
                  (value != 0u ? rvswd_phy_data_pin
                               : (rvswd_phy_data_pin << 16u));
    rvswd_phy_fast_half_period();
    GPIOA->BSHR = rvswd_phy_clock_pin;
    rvswd_phy_fast_half_period();
}

static inline __attribute__((always_inline)) uint8_t
rvswd_phy_sample_bit_fast(void) {
    uint8_t value;

    GPIOA->BSHR = rvswd_phy_clock_pin << 16u;
    rvswd_phy_fast_half_period();
    GPIOA->BSHR = rvswd_phy_clock_pin;
    value = (GPIOA->INDR & rvswd_phy_data_pin) != 0u ? 1u : 0u;
    rvswd_phy_fast_half_period();
    return value;
}

void rvswd_phy_gpio_config_data_output(void) {
    GPIOA->CFGLR = (GPIOA->CFGLR & ~(0xfu << rvswd_phy_data_cfg_shift)) |
                   (0x01u << rvswd_phy_data_cfg_shift);
}

void rvswd_phy_gpio_config_data_input(void) {
    // turnaround 开始前将锁存置高，释放 PA3 SWDIO，PA1 通过外部电阻提供上拉
    GPIOA->BSHR = rvswd_phy_data_pin;
    GPIOA->CFGLR = (GPIOA->CFGLR & ~(0xfu << rvswd_phy_data_cfg_shift)) |
                   (0x08u << rvswd_phy_data_cfg_shift);
}

void rvswd_phy_gpio_start(bool fast_timing) {
    rvswd_phy_gpio_config_data_output();
    rvswd_phy_clock_high();
    rvswd_phy_data_high();
    rvswd_phy_half_period(fast_timing);
    rvswd_phy_data_low();
    rvswd_phy_half_period(fast_timing);
}

void rvswd_phy_gpio_stop(bool fast_timing) {
    // 采样结束时 SWCLK 仍为高，先接管数据线，再结束当前位
    rvswd_phy_gpio_config_data_output();
    rvswd_phy_clock_low();
    rvswd_phy_data_low();
    rvswd_phy_half_period(fast_timing);
    rvswd_phy_clock_high();
    rvswd_phy_half_period(fast_timing);
    rvswd_phy_data_high();
    rvswd_phy_half_period(fast_timing);
}

static inline __attribute__((always_inline)) void rvswd_phy_drive_bit(
    bool fast_timing, uint8_t value) {
    // 先拉低时钟，再更新数据，给 V30X 保留完整的数据建立窗口
    rvswd_phy_clock_low();
    if (value != 0u) {
        rvswd_phy_data_high();
    } else {
        rvswd_phy_data_low();
    }
    rvswd_phy_half_period(fast_timing);
    rvswd_phy_clock_high();
    rvswd_phy_half_period(fast_timing);
}

static inline __attribute__((always_inline)) uint8_t rvswd_phy_sample_bit(
    bool fast_timing) {
    uint8_t value;

    rvswd_phy_clock_low();
    rvswd_phy_half_period(fast_timing);
    rvswd_phy_clock_high();
    // 上升沿后立即读取目标驱动的 SWDIO 电平
    value = (GPIOA->INDR & rvswd_phy_data_pin) != 0u ? 1u : 0u;
    rvswd_phy_half_period(fast_timing);
    return value;
}

void rvswd_phy_gpio_drive_range(bool fast_timing, const uint8_t *frame,
                                uint8_t first_bit, uint8_t bit_count) {
    if (fast_timing) {
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
            fast_timing,
            rvswd_frame_get_bit(frame, (uint8_t)(first_bit + bit)));
    }
}

void rvswd_phy_gpio_sample_range(bool fast_timing, uint8_t *frame,
                                 uint8_t first_bit, uint8_t bit_count) {
    if (fast_timing) {
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
                            rvswd_phy_sample_bit(fast_timing));
    }
}

void rvswd_phy_gpio_drive_value(bool fast_timing, uint32_t value,
                                uint8_t bit_count) {
    for (uint8_t bit = bit_count; bit > 0u; --bit) {
        rvswd_phy_drive_bit(fast_timing,
                            (uint8_t)((value >> (bit - 1u)) & 1u));
    }
}

uint32_t rvswd_phy_gpio_sample_value(bool fast_timing, uint8_t bit_count) {
    uint32_t value = 0u;

    for (uint8_t bit = 0u; bit < bit_count; ++bit) {
        value = (value << 1u) | rvswd_phy_sample_bit(fast_timing);
    }
    return value;
}

void rvswd_phy_gpio_wakeup(bool fast_timing, bool stop_condition) {
    __disable_irq();
    rvswd_phy_gpio_config_data_output();
    rvswd_phy_clock_high();
    rvswd_phy_data_high();
    for (uint8_t clock = 0u; clock < rvswd_phy_wakeup_clocks; ++clock) {
        rvswd_phy_clock_low();
        rvswd_phy_half_period(fast_timing);
        rvswd_phy_clock_high();
        rvswd_phy_half_period(fast_timing);
    }
    if (stop_condition) {
        rvswd_phy_gpio_stop(fast_timing);
    }
    __enable_irq();
}

void rvswd_phy_gpio_init(void) {
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOA;
    // PA1 通过 5.1 kOhm 外部电阻给 PA3 SWDIO 提供上拉，首帧前保持高电平
    GPIOA->BSHR = rvswd_phy_data_pullup_pin | rvswd_phy_clock_pin | rvswd_phy_data_pin;
    GPIOA->CFGLR = (GPIOA->CFGLR & ~((0xfu << rvswd_phy_data_cfg_shift) |
                                     (0xfu << rvswd_phy_clock_cfg_shift) |
                                     (0xfu << rvswd_phy_data_pullup_cfg_shift))) |
                   (0x08u << rvswd_phy_data_cfg_shift) |
                   (rvswd_phy_clock_mode_output_50mhz << rvswd_phy_clock_cfg_shift) |
                   (0x01u << rvswd_phy_data_pullup_cfg_shift);
}

void rvswd_phy_gpio_disconnect(void) {
    GPIOA->BSHR = rvswd_phy_pins;
    // 会话结束后释放 PA1 上拉控制和两根调试线，目标断电时禁止经 IO 反向供电
    GPIOA->CFGLR = (GPIOA->CFGLR & ~((0xfu << rvswd_phy_data_cfg_shift) |
                                     (0xfu << rvswd_phy_clock_cfg_shift) |
                                     (0xfu << rvswd_phy_data_pullup_cfg_shift))) |
                   (0x04u << rvswd_phy_data_cfg_shift) |
                   (0x04u << rvswd_phy_clock_cfg_shift) |
                   (0x04u << rvswd_phy_data_pullup_cfg_shift);
}
