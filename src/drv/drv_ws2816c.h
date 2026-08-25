#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint16_t red;
    uint16_t green;
    uint16_t blue;
} drv_ws2816c_pixel_t;

// 初始化 SPI1 和 PA7，PA7 是 WS2816C 的 DIN 输出
void drv_ws2816c_init(void);

// 按 GRB 顺序发送像素数据，发送完成后保持低电平超过复位时间
bool drv_ws2816c_write(const drv_ws2816c_pixel_t *pixels, size_t pixel_count);
