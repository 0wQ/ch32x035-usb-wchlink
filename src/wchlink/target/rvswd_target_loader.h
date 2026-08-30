#pragma once

#include "wchlink/rvswd/rvswd_operation.h"

#include <stdbool.h>
#include <stdint.h>

// X03X loader 使用目标专用寄存器 ABI，执行组合放在 target 层
bool rvswd_target_loader_execute_x03x(
    struct rvswd_operation *operation, uint32_t entry, uint32_t stack_top,
    uint32_t mode, uint32_t address, uint32_t length, uint32_t *result);
