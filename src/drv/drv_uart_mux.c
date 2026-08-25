#include "drv/drv_uart_mux.h"

#include <ch32x035.h>

#define UART_MUX_SEL_PIN  GPIO_Pin_4
#define UART_MUX_EN_N_PIN GPIO_Pin_0
#define UART_MUX_PINS     (UART_MUX_SEL_PIN | UART_MUX_EN_N_PIN)

void drv_uart_mux_init(void) {
    GPIO_InitTypeDef gpio = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    // 先清零输出锁存，再启用推挽输出，避免初始化时产生高脉冲
    GPIO_ResetBits(GPIOA, UART_MUX_PINS);
    gpio.GPIO_Pin = UART_MUX_PINS;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOA, &gpio);
}
