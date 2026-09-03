#pragma once

#include "wchlink/target/rvswd_target_module.h"

#include <stdbool.h>
#include <stdint.h>

// CH32V20X 模块实现已抓包确认的 CH32V203 loader、Flash 和基础 reset 路径
const struct rvswd_target_module *rvswd_target_v20x_module(void);
bool rvswd_target_v20x_matches_chip_id(uint32_t chip_id);
