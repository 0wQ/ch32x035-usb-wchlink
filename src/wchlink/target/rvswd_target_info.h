#pragma once

#include "wchlink/rvswd/rvswd_types.h"

#include <stdbool.h>
#include <stdint.h>

// target info 是一次连接的快照，profile 在识别成功后锁定
struct rvswd_target_info {
    uint32_t chip_id;
    uint8_t family;
    const struct rvswd_target_profile *profile;
    bool connected;
};
