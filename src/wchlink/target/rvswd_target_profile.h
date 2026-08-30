#pragma once

#include "wchlink/rvswd/rvswd_types.h"

#include <stdbool.h>
#include <stdint.h>

// 未迁移族 profile 表是无状态只读映射，不保存当前目标或连接候选
const struct rvswd_target_profile *rvswd_target_profile_from_chip_id(uint32_t chip_id);
const struct rvswd_target_profile *rvswd_target_profile_from_family(uint8_t family);

// 自动探测使用的公共身份布局也由 profile 模块统一提供
const struct rvswd_target_identity_profile *rvswd_target_probe_identity_ch32(void);
const struct rvswd_target_identity_profile *rvswd_target_probe_identity_ch5xx(void);

// 真实 ChipID 始终优先，只有受保护路径显式激活 hint 时才接受候选 profile
const struct rvswd_target_profile *rvswd_target_profile_resolve(uint32_t chip_id, uint8_t family_hint, bool family_hint_active);
