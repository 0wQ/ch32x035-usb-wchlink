#include "wchlink/transport/rvswd_phy_gpio.h"

#include "wchlink/transport/rvswd_frame.h"

#include <ch32x035.h>

// PA2 驱动 SWCLK，PA3 双向承载 SWDIO
static const uint32_t rvswd_phy_clock_pin = GPIO_Pin_2;
static const uint32_t rvswd_phy_data_pin = GPIO_Pin_3;
static const uint8_t rvswd_phy_clock_cfg_shift = 8u;
static const uint8_t rvswd_phy_data_cfg_shift = 12u;
static const uint32_t rvswd_phy_output_mode_50mhz = 0x03u;
static const uint32_t rvswd_phy_pins = GPIO_Pin_2 | GPIO_Pin_3;
static const uint8_t rvswd_phy_wakeup_clocks = 100u;

// RVSWD 位时序相关函数统一放入 RAM，避免执行期间受到 Flash 等待周期影响
__attribute__((section(".highcode"))) void rvswd_phy_gpio_config_data_output(void);
__attribute__((section(".highcode"))) void rvswd_phy_gpio_config_data_input(void);
__attribute__((section(".highcode"))) void rvswd_phy_gpio_start(bool fast_timing);
__attribute__((section(".highcode"))) void rvswd_phy_gpio_stop(bool fast_timing);
__attribute__((section(".highcode"))) void rvswd_phy_gpio_drive_range(bool fast_timing, const uint8_t *frame, uint8_t first_bit, uint8_t bit_count);
__attribute__((section(".highcode"))) void rvswd_phy_gpio_sample_range(bool fast_timing, uint8_t *frame, uint8_t first_bit, uint8_t bit_count);
__attribute__((section(".highcode"))) void rvswd_phy_gpio_drive_value(bool fast_timing, uint32_t value, uint8_t bit_count);
__attribute__((section(".highcode"))) uint32_t rvswd_phy_gpio_sample_value(bool fast_timing, uint8_t bit_count);
__attribute__((section(".highcode"))) void rvswd_phy_gpio_wakeup(bool fast_timing, bool stop_condition);

static inline __attribute__((always_inline)) void rvswd_phy_fast_low_period(void) {
    __asm volatile(".rept 4\n"
                   "nop\n"
                   ".endr\n");
}

static inline __attribute__((always_inline)) void rvswd_phy_fast_high_period(void) {
    __asm volatile(".rept 2\n"
                   "nop\n"
                   ".endr\n");
}

static inline __attribute__((always_inline)) void rvswd_phy_fast_sample_settle(void) {
    // 目标回复在 SWCLK 上升沿后建立，采样点保留实板验证通过的等待窗口
    __asm volatile(".rept 16\n"
                   "nop\n"
                   ".endr\n");
}

static inline __attribute__((always_inline)) void rvswd_phy_slow_low_period(void) {
    // 48 MHz 下按抓包实测基准校准为低电平约 2 us
    __asm volatile(".rept 92\n"
                   "nop\n"
                   ".endr\n");
}

static inline __attribute__((always_inline)) void rvswd_phy_slow_high_period(void) {
    // 48 MHz 下按抓包实测基准校准为高电平约 1 us
    __asm volatile(".rept 42\n"
                   "nop\n"
                   ".endr\n");
}

static inline __attribute__((always_inline)) void rvswd_phy_low_period(bool fast_timing) {
    if (fast_timing) {
        rvswd_phy_fast_low_period();
        return;
    }
    rvswd_phy_slow_low_period();
}

static inline __attribute__((always_inline)) void rvswd_phy_high_period(bool fast_timing) {
    if (fast_timing) {
        rvswd_phy_fast_high_period();
        return;
    }
    rvswd_phy_slow_high_period();
}

static inline __attribute__((always_inline)) void rvswd_phy_slow_sample_half_period(void) {
    // 慢档目标回复按官方长帧抓包保留约 2.1 us 的上升沿间隔
    __asm volatile(".rept 30\n"
                   "nop\n"
                   ".endr\n");
}

static inline __attribute__((always_inline)) void rvswd_phy_start_half_period(
    bool fast_timing) {
    (void)fast_timing;
    // 起始序列按官方抓包保持约 1 us 的 SWDIO 高电平窗口
    __asm volatile(".rept 16\n"
                   "nop\n"
                   ".endr\n");
}

static inline __attribute__((always_inline)) void rvswd_phy_half_period(bool fast_timing) {
    if (fast_timing) {
        rvswd_phy_fast_sample_settle();
        return;
    }
    rvswd_phy_slow_sample_half_period();
}

static inline __attribute__((always_inline)) void rvswd_phy_clock_low(void) {
    GPIOA->BCR = rvswd_phy_clock_pin;
}

static inline __attribute__((always_inline)) void rvswd_phy_clock_high(void) {
    GPIOA->BSHR = rvswd_phy_clock_pin;
}

static inline __attribute__((always_inline)) void rvswd_phy_data_low(void) {
    GPIOA->BCR = rvswd_phy_data_pin;
}

static inline __attribute__((always_inline)) void rvswd_phy_data_high(void) {
    GPIOA->BSHR = rvswd_phy_data_pin;
}

static inline __attribute__((always_inline)) void rvswd_phy_sample_settle(bool fast_timing) {
    // 目标回复位在时钟上升沿后建立，读取前先完成当前档位的高相位等待
    if (fast_timing) {
        rvswd_phy_fast_sample_settle();
    } else {
        rvswd_phy_slow_sample_half_period();
    }
}

static inline __attribute__((always_inline)) void rvswd_phy_drive_bit_fast(uint8_t value) {
    // 官方 LinkE 在低相位内更新数据，时钟下降沿先于数据边沿
    GPIOA->BCR = rvswd_phy_clock_pin;
    if (value != 0u) {
        rvswd_phy_data_high();
    } else {
        rvswd_phy_data_low();
    }
    rvswd_phy_fast_low_period();
    GPIOA->BSHR = rvswd_phy_clock_pin;
    rvswd_phy_fast_high_period();
}

static inline __attribute__((always_inline)) uint8_t rvswd_phy_sample_bit_fast(void) {
    uint8_t value;

    // 目标在上升沿后驱动 SWDIO，采样点固定在高相位等待结束处
    GPIOA->BCR = rvswd_phy_clock_pin;
    rvswd_phy_half_period(true);
    GPIOA->BSHR = rvswd_phy_clock_pin;
    rvswd_phy_sample_settle(true);
    value = (GPIOA->INDR & rvswd_phy_data_pin) != 0u ? 1u : 0u;
    return value;
}

void rvswd_phy_gpio_config_data_output(void) {
    GPIOA->CFGLR = (GPIOA->CFGLR & ~(0xfu << rvswd_phy_data_cfg_shift)) |
                   (rvswd_phy_output_mode_50mhz << rvswd_phy_data_cfg_shift);
}

void rvswd_phy_gpio_config_data_input(void) {
    // turnaround 开始前将锁存置高，释放 PA3 SWDIO，由 PA3 内部上拉保持空闲电平
    GPIOA->BSHR = rvswd_phy_data_pin;
    GPIOA->CFGLR = (GPIOA->CFGLR & ~(0xfu << rvswd_phy_data_cfg_shift)) |
                   (0x08u << rvswd_phy_data_cfg_shift);
}

void rvswd_phy_gpio_start(bool fast_timing) {
    rvswd_phy_gpio_config_data_output();
    rvswd_phy_clock_high();
    rvswd_phy_data_high();
    rvswd_phy_start_half_period(fast_timing);
    rvswd_phy_data_low();
    rvswd_phy_start_half_period(fast_timing);
}

void rvswd_phy_gpio_stop(bool fast_timing) {
    // 调用方在结束采样前已接管 SWDIO，直接输出停止条件，避免每帧重复配置 GPIO 模式
    rvswd_phy_clock_low();
    rvswd_phy_data_low();
    rvswd_phy_low_period(fast_timing);
    rvswd_phy_clock_high();
    rvswd_phy_high_period(fast_timing);
    rvswd_phy_data_high();
    rvswd_phy_high_period(fast_timing);
}

static inline __attribute__((always_inline)) void rvswd_phy_drive_bit(
    bool fast_timing, uint8_t value) {
    // 数据在低相位内更新，为目标保留完整的数据建立窗口
    rvswd_phy_clock_low();
    if (value != 0u) {
        rvswd_phy_data_high();
    } else {
        rvswd_phy_data_low();
    }
    rvswd_phy_low_period(fast_timing);
    rvswd_phy_clock_high();
    rvswd_phy_high_period(fast_timing);
}

static inline __attribute__((always_inline)) uint8_t rvswd_phy_sample_bit(bool fast_timing) {
    uint8_t value;

    // 先产生下降沿和上升沿，再在高相位末端读取目标回复位
    rvswd_phy_clock_low();
    rvswd_phy_half_period(fast_timing);
    rvswd_phy_clock_high();
    rvswd_phy_sample_settle(fast_timing);
    value = (GPIOA->INDR & rvswd_phy_data_pin) != 0u ? 1u : 0u;
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
        rvswd_phy_drive_bit(fast_timing, rvswd_frame_get_bit(frame, (uint8_t)(first_bit + bit)));
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
        rvswd_frame_set_bit(frame, (uint8_t)(first_bit + bit), rvswd_phy_sample_bit(fast_timing));
    }
}

void rvswd_phy_gpio_drive_value(bool fast_timing, uint32_t value, uint8_t bit_count) {
    if (fast_timing) {
        // 在循环外选择快路径，避免每一位重复判断时序档位
        for (uint8_t bit = bit_count; bit > 0u; --bit) {
            rvswd_phy_drive_bit_fast((uint8_t)((value >> (bit - 1u)) & 1u));
        }
        return;
    }
    for (uint8_t bit = bit_count; bit > 0u; --bit) {
        rvswd_phy_drive_bit(false, (uint8_t)((value >> (bit - 1u)) & 1u));
    }
}

uint32_t rvswd_phy_gpio_sample_value(bool fast_timing, uint8_t bit_count) {
    uint32_t value = 0u;

    if (fast_timing) {
        // 长帧读取沿用短帧快路径的采样窗口，并消除逐位档位判断
        for (uint8_t bit = 0u; bit < bit_count; ++bit) {
            value = (value << 1u) | rvswd_phy_sample_bit_fast();
        }
        return value;
    }
    for (uint8_t bit = 0u; bit < bit_count; ++bit) {
        value = (value << 1u) | rvswd_phy_sample_bit(false);
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
        rvswd_phy_low_period(fast_timing);
        rvswd_phy_clock_high();
        rvswd_phy_high_period(fast_timing);
    }
    if (stop_condition) {
        rvswd_phy_gpio_stop(fast_timing);
    }
    __enable_irq();
}

void rvswd_phy_gpio_init(void) {
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOA;
    GPIOA->BSHR = rvswd_phy_clock_pin | rvswd_phy_data_pin;
    GPIOA->CFGLR = (GPIOA->CFGLR & ~((0xfu << rvswd_phy_data_cfg_shift) |
                                     (0xfu << rvswd_phy_clock_cfg_shift))) |
                   (0x08u << rvswd_phy_data_cfg_shift) |
                   (rvswd_phy_output_mode_50mhz << rvswd_phy_clock_cfg_shift);
}

void rvswd_phy_gpio_disconnect(void) {
    GPIOA->BSHR = rvswd_phy_pins;
    // 会话结束后释放两根调试线，目标断电时禁止经 IO 反向供电
    GPIOA->CFGLR = (GPIOA->CFGLR & ~((0xfu << rvswd_phy_data_cfg_shift) |
                                     (0xfu << rvswd_phy_clock_cfg_shift))) |
                   (0x04u << rvswd_phy_data_cfg_shift) |
                   (0x04u << rvswd_phy_clock_cfg_shift);
}
