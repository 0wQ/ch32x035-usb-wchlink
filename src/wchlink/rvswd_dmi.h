#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "rvswd_types.h"

void rvswd_dmi_reset(void);
void rvswd_dmi_set_packet_mode(enum rvswd_packet_mode mode);
enum rvswd_packet_mode rvswd_dmi_packet_mode(void);
void rvswd_dmi_set_last_status(uint8_t status);
uint8_t rvswd_dmi_last_status(void);
void rvswd_dmi_set_failure_retryable(bool retryable);
bool rvswd_dmi_failure_retryable(void);
