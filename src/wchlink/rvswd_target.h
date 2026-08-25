#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "rvswd_types.h"

const struct rvswd_target_profile *rvswd_target_profile_from_chip_id(
    uint32_t chip_id);
const struct rvswd_target_profile *rvswd_target_profile_from_family(
    uint8_t family);
const struct rvswd_target_profile *rvswd_target_profile_current(void);

void rvswd_target_reset(void);
void rvswd_target_set_chip_id(uint32_t chip_id);
uint32_t rvswd_target_chip_id(void);
void rvswd_target_set_family_hint(uint8_t family);
uint8_t rvswd_target_family_hint(void);
bool rvswd_target_family_hint_active(void);
void rvswd_target_set_family_hint_active(bool active);
uint8_t rvswd_target_family(void);
bool rvswd_target_supports_memory_streaming(void);
