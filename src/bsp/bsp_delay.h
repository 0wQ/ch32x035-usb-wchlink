#pragma once

#include <stdint.h>

// 必须在启用外设中断前调用，初始化后系统核心时钟必须保持不变
void bsp_delay_init(void);

void bsp_delay_us(uint32_t us);
void bsp_delay_ms(uint32_t ms);

// 返回自初始化以来的单调时间
uint64_t bsp_time_us(void);
uint64_t bsp_time_ms(void);
