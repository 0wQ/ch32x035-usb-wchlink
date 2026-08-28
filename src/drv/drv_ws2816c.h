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

// 按 GRB 顺序同步发送像素数据，发送完成后保持低电平超过复位时间
bool drv_ws2816c_write(const drv_ws2816c_pixel_t *pixels, size_t pixel_count);

// 启动一次异步像素发送，已有发送未完成时返回 false
bool drv_ws2816c_write_async(const drv_ws2816c_pixel_t *pixels,
                             size_t pixel_count);

// 轮询并回收已完成的 DMA 发送
void drv_ws2816c_process(void);

// 设置所有像素的统一亮度缩放，0 为关闭，UINT16_MAX 为不缩放
void drv_ws2816c_set_brightness(uint16_t brightness);
