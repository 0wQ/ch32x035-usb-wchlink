#include <ch32x035.h>

void usb_dc_low_level_init(void) {
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOC, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBFS, ENABLE);

    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Pin = GPIO_Pin_16;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOC, &gpio);
    gpio.GPIO_Pin = GPIO_Pin_17;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOC, &gpio);

    AFIO->CTLR = (AFIO->CTLR & ~(AFIO_CTLR_UDP_PUE | AFIO_CTLR_UDM_PUE)) | AFIO_CTLR_USB_PHY_V33 | AFIO_CTLR_UDP_PUE | AFIO_CTLR_USB_IOEN;

    NVIC_InitTypeDef nvic = {0};
    nvic.NVIC_IRQChannel = USBFS_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1;
    nvic.NVIC_IRQChannelSubPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);
}

void usb_dc_low_level_deinit(void) {
    NVIC_InitTypeDef nvic = {0};
    nvic.NVIC_IRQChannel = USBFS_IRQn;
    nvic.NVIC_IRQChannelCmd = DISABLE;
    NVIC_Init(&nvic);
}
