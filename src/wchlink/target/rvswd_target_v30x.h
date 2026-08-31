#pragma once

#include "wchlink/target/rvswd_target_module.h"

#include <stdbool.h>
#include <stdint.h>

const struct rvswd_target_module *rvswd_target_v30x_module(void);
const struct rvswd_target_profile *rvswd_target_v30x_profile(void);
bool rvswd_target_v30x_matches_chip_id(uint32_t chip_id);
