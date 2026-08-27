#include "wchlink/protocol/wchlink_family.h"
#include "wchlink/protocol/wchlink_wire.h"
#include "wchlink/session/wchlink_command_internal.h"
#include "wchlink/transport/rvswd_transport.h"

struct wchlink_session_command_result wchlink_command_result(
    enum wchlink_session_command_status status, size_t response_length) {
    return (struct wchlink_session_command_result){
        .status = status,
        .action = WCHLINK_SESSION_ACTION_NONE,
        .response_policy = WCHLINK_SESSION_RESPONSE_STANDARD,
        .response_length = response_length,
    };
}

static struct wchlink_session_command_result wchlink_command_no_response(
    enum wchlink_session_action action) {
    return (struct wchlink_session_command_result){
        .status = WCHLINK_SESSION_COMMAND_NO_RESPONSE,
        .action = action,
    };
}

uint32_t wchlink_command_read_be32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24u) | ((uint32_t)data[1] << 16u) |
           ((uint32_t)data[2] << 8u) | data[3];
}

static struct wchlink_session_command_result wchlink_handle_device_mode(
    const uint8_t *request, uint8_t *response, size_t response_capacity) {
    if (request[3] == WCHLINK_DEVICE_MODE_QUERY) {
        // MRS 通过第四字节 2 识别 Link 当前处于 RISC-V 调试模式
        return wchlink_command_result(
            WCHLINK_SESSION_COMMAND_COMPLETED,
            wchlink_wire_command_reply(response, response_capacity,
                                       WCHLINK_FAMILY_DEVICE_MODE,
                                       WCHLINK_DEVICE_MODE_QUERY));
    }
    if (request[3] == WCHLINK_DEVICE_MODE_IAP) {
        // 官方 SetIAPMode 不返回协议帧，USB 主循环消费 action 后进入维护 ISP
        return wchlink_command_no_response(WCHLINK_SESSION_ACTION_ENTER_ISP);
    }
    return wchlink_command_result(
        WCHLINK_SESSION_COMMAND_MALFORMED,
        wchlink_wire_unsupported(response, response_capacity,
                                 WCHLINK_FAMILY_DEVICE_MODE));
}

static struct wchlink_session_command_result wchlink_command_dispatch(
    struct wchlink_command_context *context, const uint8_t *request,
    size_t request_length, uint8_t *response, size_t response_capacity) {
    uint8_t family;

    if (request == NULL || response == NULL || request_length < 2u ||
        request[0] != WCHLINK_COMMAND_PREFIX) {
        return wchlink_command_result(
            WCHLINK_SESSION_COMMAND_MALFORMED,
            wchlink_wire_ack(response, response_capacity, 0u));
    }

    family = request[1];
    switch (family) {
        case WCHLINK_FAMILY_DMI:
        case WCHLINK_FAMILY_CONFIG:
        case WCHLINK_FAMILY_INFO:
        case WCHLINK_FAMILY_SPEED:
        case WCHLINK_FAMILY_RESET:
        case WCHLINK_FAMILY_CONTROL:
            return wchlink_command_handle_target(
                context, request, request_length, response, response_capacity);
        case WCHLINK_FAMILY_DEVICE_MODE:
            if (request_length >= 4u) {
                return wchlink_handle_device_mode(request, response,
                                                  response_capacity);
            }
            break;
        case WCHLINK_FAMILY_PARTIAL_WRITE:
            if (request_length >= 8u) {
                return wchlink_command_handle_partial_write(
                    context, request, response, response_capacity);
            }
            break;
        case WCHLINK_TRANSFER_FAMILY_WRITE:
            if (request_length >= 11u) {
                return wchlink_command_handle_memory_write(
                    context, request, response, response_capacity);
            }
            break;
        case WCHLINK_TRANSFER_FAMILY_READ:
            if (request_length >= 11u) {
                return wchlink_command_handle_memory_read(
                    context, request, response, response_capacity);
            }
            break;
        case WCHLINK_TRANSFER_FAMILY_FLASH:
            if (request_length >= 4u) {
                return wchlink_command_handle_flash(
                    context, request, response, response_capacity);
            }
            break;
        default:
            break;
    }

    return wchlink_command_result(
        WCHLINK_SESSION_COMMAND_MALFORMED,
        wchlink_wire_ack(response, response_capacity, family));
}

struct wchlink_session_command_result wchlink_command_process(
    struct wchlink_command_context *context, const uint8_t *request,
    size_t request_length, uint8_t *response, size_t response_capacity) {
    bool consumes_resume_status =
        request != NULL && request_length >= 9u &&
        request[0] == WCHLINK_COMMAND_PREFIX &&
        request[1] == WCHLINK_FAMILY_DMI &&
        request[3] == RVSWD_DMI_STATUS &&
        request[8] == WCHLINK_DMI_OPERATION_READ;

    // resume 状态只属于紧邻的一次 DMSTATUS 读取，不能跨其他命令泄漏
    if (!consumes_resume_status) {
        wchlink_direct_dmi_resume_reset(&context->direct_dmi_resume);
    }
    return wchlink_command_dispatch(context, request, request_length, response,
                                    response_capacity);
}
