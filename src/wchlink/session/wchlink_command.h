#pragma once

#include "wchlink/session/wchlink_session.h"

#include <stddef.h>
#include <stdint.h>

struct wchlink_command_context;

void wchlink_command_reset(struct wchlink_command_context *context);
struct wchlink_session_command_result wchlink_command_process(
    struct wchlink_command_context *context, const uint8_t *request,
    size_t request_length, uint8_t *response, size_t response_capacity);
