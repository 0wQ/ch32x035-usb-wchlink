#pragma once

#include <stdbool.h>

struct rvswd_operation;
struct rvswd_target_profile;

// erase 分派入口完成 profile 校验，backend 独占命令口与 RAM erase stub
bool rvswd_flash_ch5xx_erase_all(
    struct rvswd_operation *operation,
    const struct rvswd_target_profile *profile);
