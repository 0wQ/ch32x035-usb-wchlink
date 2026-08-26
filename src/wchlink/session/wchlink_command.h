#pragma once

#include "wchlink/session/wchlink_session.h"
#include "wchlink/session/wchlink_transfer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct wchlink_target_ports;

// Command context 引用 session 独占的 target 和 transfer，不保留 USB buffer
struct wchlink_command_context {
    struct wchlink_target_ports *target;
    struct wchlink_transfer *transfer;
    bool ch5xx_info_query_seen;
};

void wchlink_command_reset(struct wchlink_command_context *context);
struct wchlink_session_command_result wchlink_command_process(
    struct wchlink_command_context *context, const uint8_t *request,
    size_t request_length, uint8_t *response, size_t response_capacity);
