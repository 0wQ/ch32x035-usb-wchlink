#pragma once

#include "rvswd_types.h"

#include <stdint.h>

// Profile 表是无状态只读映射，不保存当前目标或连接候选
const struct rvswd_target_profile *rvswd_target_profile_from_chip_id(
    uint32_t chip_id);
const struct rvswd_target_profile *rvswd_target_profile_from_family(
    uint8_t family);
