#pragma once

#include <stdbool.h>

void drv_sbu_mux_init(void);
void drv_sbu_mux_set_enabled(bool enabled);
void drv_sbu_mux_set_reversed(bool reversed);
bool drv_sbu_mux_is_enabled(void);
bool drv_sbu_mux_is_reversed(void);
