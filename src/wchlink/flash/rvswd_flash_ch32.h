#pragma once

#include <stdbool.h>

struct rvswd_operation;
struct rvswd_target_profile;

// CH32 backend 接收已确认的非 CH58X/CH59X profile，不处理目标选择
bool rvswd_flash_ch32_erase_all(
    struct rvswd_operation *operation,
    const struct rvswd_target_profile *profile);
