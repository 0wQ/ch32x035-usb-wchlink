#include "drv/drv_sbu_mux.h"

#include <ch32x035.h>

#define SBU_MUX_SEL_PIN  GPIO_Pin_4
#define SBU_MUX_EN_N_PIN GPIO_Pin_0
#define SBU_MUX_PINS     (SBU_MUX_SEL_PIN | SBU_MUX_EN_N_PIN)

static bool sbu_mux_enabled;
static bool sbu_mux_reversed;

void drv_sbu_mux_init(void) {
    GPIO_InitTypeDef gpio = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    // 先清零输出锁存，再启用推挽输出，避免初始化时产生高脉冲
    GPIO_ResetBits(GPIOA, SBU_MUX_PINS);
    gpio.GPIO_Pin = SBU_MUX_PINS;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOA, &gpio);

    // 初始化后明确选择正向映射并打开模拟开关，避免状态只由输出锁存值决定
    drv_sbu_mux_set_reversed(false);
    drv_sbu_mux_set_enabled(true);
}

void drv_sbu_mux_set_enabled(bool enabled) {
    if (enabled) {
        GPIO_ResetBits(GPIOA, SBU_MUX_EN_N_PIN);
    } else {
        GPIO_SetBits(GPIOA, SBU_MUX_EN_N_PIN);
    }

    sbu_mux_enabled = enabled;
}

void drv_sbu_mux_set_reversed(bool reversed) {
    if (reversed) {
        GPIO_SetBits(GPIOA, SBU_MUX_SEL_PIN);
    } else {
        GPIO_ResetBits(GPIOA, SBU_MUX_SEL_PIN);
    }

    sbu_mux_reversed = reversed;
}

bool drv_sbu_mux_is_enabled(void) {
    return sbu_mux_enabled;
}

bool drv_sbu_mux_is_reversed(void) {
    return sbu_mux_reversed;
}
