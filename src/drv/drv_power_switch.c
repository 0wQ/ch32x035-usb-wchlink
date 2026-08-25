#include "drv/drv_power_switch.h"

#include <ch32x035.h>

#define POWER_EN_PIN GPIO_Pin_12

void drv_power_switch_init(void) {
    GPIO_InitTypeDef gpio = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    // 先保持功率开关关闭，再启用推挽输出
    GPIO_ResetBits(GPIOB, POWER_EN_PIN);
    gpio.GPIO_Pin = POWER_EN_PIN;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOB, &gpio);
}

void drv_power_switch_set_enabled(bool enabled) {
    if (enabled) {
        GPIO_SetBits(GPIOB, POWER_EN_PIN);
    } else {
        GPIO_ResetBits(GPIOB, POWER_EN_PIN);
    }
}
