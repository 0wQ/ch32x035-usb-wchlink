#include "drv/drv_dp_pullup.h"

#include <ch32x035.h>

#define DP_PULLUP_PIN GPIO_Pin_11

void drv_dp_pullup_init(void) {
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    drv_dp_pullup_set_enabled(false);
}

void drv_dp_pullup_set_enabled(bool enabled) {
    GPIO_InitTypeDef gpio = {0};

    gpio.GPIO_Pin = DP_PULLUP_PIN;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    if (enabled) {
        // 先将输出锁存置高，再启用推挽输出，避免 D+ 出现低脉冲
        GPIO_SetBits(GPIOB, DP_PULLUP_PIN);
        gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    } else {
        gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    }
    GPIO_Init(GPIOB, &gpio);
}
