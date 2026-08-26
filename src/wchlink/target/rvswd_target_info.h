#pragma once

#include "wchlink/rvswd/rvswd_types.h"

#include <stdbool.h>
#include <stdint.h>

// target info 是按值返回的只读快照，不泄漏锁定 profile 和 transport 状态
struct rvswd_target_info {
    uint32_t chip_id;
    uint8_t family;
    uint8_t loader;
    bool connected;
    bool memory_streaming;
};
