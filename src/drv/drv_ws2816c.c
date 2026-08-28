#include "drv/drv_ws2816c.h"

#include "bsp/bsp_delay.h"

#include <ch32x035.h>
#include <ch32x035_dma.h>

#define WS2816C_MOSI_PIN        GPIO_Pin_7
#define WS2816C_SPI_PINS        WS2816C_MOSI_PIN
#define WS2816C_SPI_PRESCALER   SPI_BaudRatePrescaler_8
#define WS2816C_RESET_BYTES     220u
#define WS2816C_MAX_PIXELS      8u
#define WS2816C_DATA_BYTES      (WS2816C_MAX_PIXELS * 48u)
#define WS2816C_DMA_BUFFER_SIZE (WS2816C_RESET_BYTES + WS2816C_DATA_BYTES + WS2816C_RESET_BYTES)

// SPI1 为 48MHz 时，/8 得到 6MHz，单个 SPI 位约 166.7ns
// 沿用已在 CH32X035 上验证过的 WS2812B 编码和 SPI 第二边沿发送模式
#define WS2816C_CODE_0 0x60u
#define WS2816C_CODE_1 0x7cu

static bool ws2816c_initialized;
static bool ws2816c_dma_busy;
static uint16_t ws2816c_brightness = UINT16_MAX;
static uint8_t ws2816c_dma_buffer[WS2816C_DMA_BUFFER_SIZE] __attribute__((aligned(4)));

static void ws2816c_config_mosi(GPIOMode_TypeDef mode) {
    GPIO_InitTypeDef gpio = {0};

    gpio.GPIO_Pin = WS2816C_MOSI_PIN;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = mode;
    GPIO_Init(GPIOA, &gpio);
}

static uint16_t ws2816c_scale_channel(uint16_t value) {
    return (uint16_t)(((uint32_t)value * ws2816c_brightness + (UINT16_MAX / 2u)) / UINT16_MAX);
}

static void ws2816c_encode_u16(uint16_t value, uint16_t *index) {
    for (uint8_t bit = 0u; bit < 16u; ++bit) {
        ws2816c_dma_buffer[(*index)++] =
            (value & (uint16_t)(1u << (15u - bit))) != 0u ? WS2816C_CODE_1
                                                          : WS2816C_CODE_0;
    }
}

static uint16_t ws2816c_encode_pixels(const drv_ws2816c_pixel_t *pixels,
                                      size_t pixel_count) {
    uint16_t index = WS2816C_RESET_BYTES;

    for (size_t pixel = 0u; pixel < pixel_count; ++pixel) {
        ws2816c_encode_u16(ws2816c_scale_channel(pixels[pixel].green), &index);
        ws2816c_encode_u16(ws2816c_scale_channel(pixels[pixel].red), &index);
        ws2816c_encode_u16(ws2816c_scale_channel(pixels[pixel].blue), &index);
    }
    while (index < WS2816C_RESET_BYTES + WS2816C_DATA_BYTES + WS2816C_RESET_BYTES) {
        ws2816c_dma_buffer[index++] = 0u;
    }
    return index;
}

static void ws2816c_update_busy(void) {
    if (!ws2816c_dma_busy) {
        return;
    }
    if (DMA_GetCurrDataCounter(DMA1_Channel3) == 0u &&
        SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == RESET) {
        ws2816c_dma_busy = false;
    }
}

static void ws2816c_wait_idle(void) {
    while (ws2816c_dma_busy) {
        ws2816c_update_busy();
    }
}

void drv_ws2816c_init(void) {
    SPI_InitTypeDef spi = {0};

    bsp_delay_init();
    DMA_InitTypeDef dma = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA |
                               RCC_APB2Periph_SPI1,
                           ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
    // SPI1 默认映射到 PA5/PA6/PA7，显式清除重映射保证 MOSI 固定在 PA7
    GPIO_PinRemapConfig(GPIO_FullRemap_SPI1, DISABLE);

    // 只接管 MOSI，PA5 保持原有功能，避免改变板上其他外设的 SCK 信号
    GPIO_ResetBits(GPIOA, WS2816C_SPI_PINS);
    ws2816c_config_mosi(GPIO_Mode_Out_PP);
    ws2816c_config_mosi(GPIO_Mode_AF_PP);

    SPI_I2S_DeInit(SPI1);
    spi.SPI_Direction = SPI_Direction_1Line_Tx;
    spi.SPI_Mode = SPI_Mode_Master;
    spi.SPI_DataSize = SPI_DataSize_8b;
    spi.SPI_CPOL = SPI_CPOL_Low;
    spi.SPI_CPHA = SPI_CPHA_2Edge;
    spi.SPI_NSS = SPI_NSS_Soft;
    spi.SPI_BaudRatePrescaler = WS2816C_SPI_PRESCALER;
    spi.SPI_FirstBit = SPI_FirstBit_MSB;
    spi.SPI_CRCPolynomial = 7u;
    SPI_Init(SPI1, &spi);
    SPI_NSSInternalSoftwareConfig(SPI1, SPI_NSSInternalSoft_Set);

    DMA_DeInit(DMA1_Channel3);
    dma.DMA_PeripheralBaseAddr = (uint32_t)&SPI1->DATAR;
    dma.DMA_MemoryBaseAddr = (uint32_t)ws2816c_dma_buffer;
    dma.DMA_DIR = DMA_DIR_PeripheralDST;
    dma.DMA_BufferSize = WS2816C_DMA_BUFFER_SIZE;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    dma.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    dma.DMA_Mode = DMA_Mode_Normal;
    dma.DMA_Priority = DMA_Priority_VeryHigh;
    dma.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel3, &dma);
    DMA_ClearITPendingBit(DMA1_IT_GL3);
    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, ENABLE);
    SPI_Cmd(SPI1, ENABLE);

    ws2816c_initialized = true;
    bsp_delay_us(300u);
}

bool drv_ws2816c_write(const drv_ws2816c_pixel_t *pixels, size_t pixel_count) {
    ws2816c_wait_idle();
    if (!drv_ws2816c_write_async(pixels, pixel_count)) {
        return false;
    }
    ws2816c_wait_idle();
    return true;
}

bool drv_ws2816c_write_async(const drv_ws2816c_pixel_t *pixels, size_t pixel_count) {
    uint16_t transfer_length;

    if (!ws2816c_initialized || pixels == NULL || pixel_count > WS2816C_MAX_PIXELS) {
        return false;
    }
    if (pixel_count == 0u) {
        return true;
    }

    ws2816c_update_busy();
    if (ws2816c_dma_busy) {
        return false;
    }
    transfer_length = ws2816c_encode_pixels(pixels, pixel_count);
    DMA_Cmd(DMA1_Channel3, DISABLE);
    DMA_ClearITPendingBit(DMA1_IT_GL3);
    DMA_SetCurrDataCounter(DMA1_Channel3, transfer_length);
    DMA_Cmd(DMA1_Channel3, ENABLE);
    ws2816c_dma_busy = true;
    return true;
}

void drv_ws2816c_process(void) {
    ws2816c_update_busy();
}

void drv_ws2816c_set_brightness(uint16_t brightness) {
    ws2816c_brightness = brightness;
}
