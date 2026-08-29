#pragma once

#include "wchlink/rvswd/rvswd_operation.h"

#include <stdbool.h>

struct rvswd_target_profile;

// 按 profile 策略把目标切换为 loader 可运行环境，profile 为 NULL 或 NONE 时直接成功
bool rvswd_execute_prepare(struct rvswd_operation *operation,
                           const struct rvswd_target_profile *profile);
