#include "wchlink/protocol/wchlink_wire.h"
#include "wchlink/session/wchlink_command_internal.h"
#include "wchlink/target/wchlink_target_control.h"
#include "wchlink/target/wchlink_target_flash.h"

struct wchlink_session_command_result wchlink_command_handle_partial_write(
    struct wchlink_command_context *context, const uint8_t *request,
    uint8_t *response, size_t response_capacity) {
    uint32_t address = wchlink_command_read_be32(&request[3]);

    if (!wchlink_transfer_start_partial_write(
            context->transfer, address, request[7])) {
        return wchlink_command_result(
            WCHLINK_SESSION_COMMAND_TARGET_FAILED,
            wchlink_wire_unsupported(response, response_capacity,
                                     WCHLINK_FAMILY_PARTIAL_WRITE));
    }
    return wchlink_command_result(
        WCHLINK_SESSION_COMMAND_COMPLETED,
        wchlink_wire_command_reply(response, response_capacity,
                                   WCHLINK_FAMILY_PARTIAL_WRITE, request[2]));
}

struct wchlink_session_command_result wchlink_command_handle_memory_write(
    struct wchlink_command_context *context, const uint8_t *request,
    uint8_t *response, size_t response_capacity) {
    uint32_t address = wchlink_command_read_be32(&request[3]);
    uint32_t length = wchlink_command_read_be32(&request[7]);

    wchlink_transfer_prepare_write(context->transfer, address, length);
    return wchlink_command_result(
        WCHLINK_SESSION_COMMAND_COMPLETED,
        wchlink_wire_command_reply(response, response_capacity,
                                   WCHLINK_TRANSFER_FAMILY_WRITE, 0x01u));
}

struct wchlink_session_command_result wchlink_command_handle_memory_read(
    struct wchlink_command_context *context, const uint8_t *request,
    uint8_t *response, size_t response_capacity) {
    uint32_t address = wchlink_command_read_be32(&request[3]);
    uint32_t length = wchlink_command_read_be32(&request[7]);

    wchlink_transfer_prepare_read(context->transfer, address, length);
    return wchlink_command_result(
        WCHLINK_SESSION_COMMAND_COMPLETED,
        wchlink_wire_command_reply(response, response_capacity,
                                   WCHLINK_TRANSFER_FAMILY_READ, 0x01u));
}

struct wchlink_session_command_result wchlink_command_handle_flash(
    struct wchlink_command_context *context, const uint8_t *request,
    uint8_t *response, size_t response_capacity) {
    const uint8_t family = WCHLINK_TRANSFER_FAMILY_FLASH;
    struct rvswd_target_result target_result;

    switch (request[3]) {
        case 0x01u:
            wchlink_transfer_clear_operation(context->transfer);
            target_result = rvswd_target_result_success();
            if (!wchlink_target_ports_info(context->target).connected) {
                // MRS 版本预检会先发送 STOP，基础全擦必须重新建立目标会话
                wchlink_command_target_init(context);
                target_result = wchlink_target_ports_connect(context->target);
            }
            if (target_result.ok &&
                wchlink_target_ports_info(context->target).connected) {
                target_result = wchlink_target_ports_flash_erase_all(
                    context->target);
            }
            if (!target_result.ok ||
                !wchlink_target_ports_info(context->target).connected) {
                size_t response_length =
                    response_capacity >= 4u
                        ? wchlink_wire_family_error(
                              response, response_capacity, family,
                              target_result.code)
                        : wchlink_wire_unsupported(response, response_capacity,
                                                   family);

                return wchlink_command_result(
                    WCHLINK_SESSION_COMMAND_TARGET_FAILED, response_length);
            }
            // MRS 的基础全擦命令要求应答状态为 1
            return wchlink_command_result(
                WCHLINK_SESSION_COMMAND_COMPLETED,
                wchlink_wire_command_reply(response, response_capacity, family,
                                           0x01u));
        case 0x05u:
            if (!wchlink_transfer_start_loader(context->transfer)) {
                return wchlink_command_result(
                    WCHLINK_SESSION_COMMAND_TARGET_FAILED,
                    wchlink_wire_unsupported(response, response_capacity,
                                             family));
            }
            return wchlink_command_result(
                WCHLINK_SESSION_COMMAND_COMPLETED,
                wchlink_wire_command_reply(response, response_capacity, family,
                                           request[3]));
        case 0x06u:
            // 官方 OpenOCD 在地址设置前发送 Prepare，状态必须跨过地址帧保留
            wchlink_transfer_mark_flash_prepare(context->transfer);
            return wchlink_command_result(
                WCHLINK_SESSION_COMMAND_COMPLETED,
                wchlink_wire_command_reply(response, response_capacity, family,
                                           request[3]));
        case 0x07u:
        case 0x0bu: {
            struct wchlink_transfer_finish_result finish =
                wchlink_transfer_finish_loader(context->transfer, request[3]);

            if (finish.status == WCHLINK_TRANSFER_FINISH_LOADER_ERROR) {
                return wchlink_command_result(
                    WCHLINK_SESSION_COMMAND_TARGET_FAILED,
                    response_capacity < 13u
                        ? 0u
                        : wchlink_wire_loader_error(
                              response, response_capacity, family,
                              finish.loader_error, finish.dmi_status,
                              finish.address, finish.abstractcs));
            }
            if (finish.status == WCHLINK_TRANSFER_FINISH_INCOMPLETE) {
                return wchlink_command_result(
                    WCHLINK_SESSION_COMMAND_BUSY,
                    wchlink_wire_unsupported(response, response_capacity,
                                             family));
            }
            if (finish.status == WCHLINK_TRANSFER_FINISH_READY) {
                return wchlink_command_result(
                    WCHLINK_SESSION_COMMAND_COMPLETED,
                    wchlink_wire_command_reply(response, response_capacity,
                                               family, request[3]));
            }
            return wchlink_command_result(
                WCHLINK_SESSION_COMMAND_TARGET_FAILED,
                response_capacity >= 4u
                    ? wchlink_wire_family_error(response, response_capacity,
                                                family, finish.target_value)
                    : 0u);
        }
        case 0x02u:
        case 0x03u:
        case 0x04u:
            if (!wchlink_transfer_start_flash(context->transfer,
                                              request[3])) {
                return wchlink_command_result(
                    WCHLINK_SESSION_COMMAND_TARGET_FAILED,
                    wchlink_wire_unsupported(response, response_capacity,
                                             family));
            }
            return wchlink_command_result(
                WCHLINK_SESSION_COMMAND_COMPLETED,
                wchlink_wire_command_reply(response, response_capacity, family,
                                           request[3]));
        case 0x08u:
            // Program End 将目标整理到复位入口并保持停止，后续 resume 才会进入应用
            target_result =
                wchlink_target_ports_reset_and_halt(context->target);
            wchlink_transfer_abort(context->transfer);
            if (!target_result.ok) {
                return wchlink_command_result(
                    WCHLINK_SESSION_COMMAND_TARGET_FAILED,
                    wchlink_wire_family_error(response, response_capacity,
                                              family, target_result.code));
            }
            return wchlink_command_result(
                WCHLINK_SESSION_COMMAND_COMPLETED,
                wchlink_wire_ack(response, response_capacity, family));
        case 0x0cu:
            wchlink_transfer_begin_read(context->transfer);
            return wchlink_command_result(
                WCHLINK_SESSION_COMMAND_COMPLETED,
                wchlink_wire_command_reply(response, response_capacity, family,
                                           request[3]));
        default:
            return wchlink_command_result(
                WCHLINK_SESSION_COMMAND_COMPLETED,
                wchlink_wire_ack(response, response_capacity, family));
    }
}
