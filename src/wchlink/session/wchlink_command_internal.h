#pragma once

#include "wchlink/session/wchlink_command.h"
#include "wchlink/session/wchlink_direct_dmi_resume.h"
#include "wchlink/session/wchlink_transfer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct wchlink_target_ports;

enum wchlink_transfer_family {
    WCHLINK_TRANSFER_FAMILY_WRITE = 0x01u,
    WCHLINK_TRANSFER_FAMILY_FLASH = 0x02u,
    WCHLINK_TRANSFER_FAMILY_READ = 0x03u,
};

// Command context 只引用 session 独占状态，不保留 request 或 response buffer
struct wchlink_command_context {
    struct wchlink_target_ports *target;
    struct wchlink_transfer *transfer;
    struct wchlink_direct_dmi_resume direct_dmi_resume;
    bool ch5xx_info_query_seen;
};

struct wchlink_session_command_result wchlink_command_result(
    enum wchlink_session_command_status status, size_t response_length);
uint32_t wchlink_command_read_be32(const uint8_t *data);
void wchlink_command_target_init(struct wchlink_command_context *context);

struct wchlink_session_command_result wchlink_command_handle_target(
    struct wchlink_command_context *context, const uint8_t *request,
    size_t request_length, uint8_t *response, size_t response_capacity);

struct wchlink_session_command_result wchlink_command_handle_partial_write(
    struct wchlink_command_context *context, const uint8_t *request,
    uint8_t *response, size_t response_capacity);
struct wchlink_session_command_result wchlink_command_handle_memory_write(
    struct wchlink_command_context *context, const uint8_t *request,
    uint8_t *response, size_t response_capacity);
struct wchlink_session_command_result wchlink_command_handle_memory_read(
    struct wchlink_command_context *context, const uint8_t *request,
    uint8_t *response, size_t response_capacity);
struct wchlink_session_command_result wchlink_command_handle_flash(
    struct wchlink_command_context *context, const uint8_t *request,
    uint8_t *response, size_t response_capacity);
