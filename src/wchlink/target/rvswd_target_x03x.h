#pragma once

#include "wchlink/target/rvswd_target_module.h"

#include <stdbool.h>
#include <stdint.h>

// X03X 族模块提供 profile、loader prepare 和专属 loader execute
const struct rvswd_target_module *rvswd_target_x03x_module(void);
const struct rvswd_target_profile *rvswd_target_x03x_profile(void);
bool rvswd_target_x03x_matches_chip_id(uint32_t chip_id);

bool rvswd_target_x03x_loader_prepare(
    struct rvswd_operation *operation,
    const struct rvswd_target_profile *profile, uint32_t mode);
bool rvswd_target_x03x_loader_execute(
    struct rvswd_operation *operation, uint32_t entry, uint32_t stack_top,
    uint32_t mode, uint32_t address, uint32_t length, uint32_t data_address,
    uint32_t dpc_value, uint32_t *result);
