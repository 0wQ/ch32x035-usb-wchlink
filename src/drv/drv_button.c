#include "drv/drv_button.h"

#include <ch32x035.h>

#define BUTTON_PIN GPIO_Pin_5

void drv_button_init(void) {
    GPIO_InitTypeDef gpio = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    gpio.GPIO_Pin = BUTTON_PIN;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &gpio);
}

bool drv_button_is_pressed(void) {
    return GPIO_ReadInputDataBit(GPIOA, BUTTON_PIN) == Bit_RESET;
}
