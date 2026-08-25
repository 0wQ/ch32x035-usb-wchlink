#include "rvswd_dmi.h"

static enum rvswd_packet_mode rvswd_dmi_current_packet_mode =
    RVSWD_PACKET_SHORT;
static uint8_t rvswd_dmi_current_last_status;
static bool rvswd_dmi_current_failure_retryable;

void rvswd_dmi_reset(void) {
    rvswd_dmi_current_packet_mode = RVSWD_PACKET_SHORT;
    rvswd_dmi_current_last_status = 0u;
    rvswd_dmi_current_failure_retryable = false;
}

void rvswd_dmi_set_packet_mode(enum rvswd_packet_mode mode) {
    rvswd_dmi_current_packet_mode = mode;
}

enum rvswd_packet_mode rvswd_dmi_packet_mode(void) {
    return rvswd_dmi_current_packet_mode;
}

void rvswd_dmi_set_last_status(uint8_t status) {
    rvswd_dmi_current_last_status = status;
}

uint8_t rvswd_dmi_last_status(void) {
    return rvswd_dmi_current_last_status;
}

void rvswd_dmi_set_failure_retryable(bool retryable) {
    rvswd_dmi_current_failure_retryable = retryable;
}

bool rvswd_dmi_failure_retryable(void) {
    return rvswd_dmi_current_failure_retryable;
}
