#pragma once

#include "wchlink/rvswd/rvswd_types.h"
#include "wchlink/target/rvswd_target_module.h"

#include <stdbool.h>
#include <stdint.h>

// registry 只负责将已确认的 ChipID 或 family 分派到族模块
const struct rvswd_target_module *rvswd_target_registry_module_from_chip_id(
    uint32_t chip_id);
const struct rvswd_target_module *rvswd_target_registry_module_from_family(
    uint8_t family);
