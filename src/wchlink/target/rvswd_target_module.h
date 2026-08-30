#pragma once

#include "wchlink/rvswd/rvswd_operation.h"

#include <stdbool.h>
#include <stdint.h>

struct rvswd_target_profile;

typedef bool (*rvswd_target_loader_prepare_fn)(
    struct rvswd_operation *operation,
    const struct rvswd_target_profile *profile, uint32_t mode);
typedef bool (*rvswd_target_loader_execute_fn)(
    struct rvswd_operation *operation, uint32_t entry, uint32_t stack_top,
    uint32_t mode, uint32_t address, uint32_t length, uint32_t data_address,
    uint32_t dpc_value, uint32_t *result);

// 族模块只暴露 profile 和确实变化的 loader 行为，registry 不执行这些回调
struct rvswd_target_module {
    const struct rvswd_target_profile *profile;
    rvswd_target_loader_prepare_fn loader_prepare;
    rvswd_target_loader_execute_fn loader_execute;
};
