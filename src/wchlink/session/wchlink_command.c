#include "wchlink/session/wchlink_command.h"

#include "bsp/bsp_delay.h"
#include "drv/drv_dp_pullup.h"
#include "drv/drv_power_switch.h"
#include "wchlink/protocol/wchlink_family.h"
#include "wchlink/protocol/wchlink_wire.h"

#include <stdbool.h>

enum wchlink_transfer_family {
    WCHLINK_TRANSFER_FAMILY_WRITE = 0x01u,
    WCHLINK_TRANSFER_FAMILY_FLASH = 0x02u,
    WCHLINK_TRANSFER_FAMILY_READ = 0x03u,
};

enum wchlink_dmi_operation {
    WCHLINK_DMI_OPERATION_READ = 0x01u,
    WCHLINK_DMI_OPERATION_WRITE = 0x02u,
};

static struct wchlink_session_command_result wchlink_command_result(
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

static uint32_t wchlink_read_be32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24u) | ((uint32_t)data[1] << 16u) |
           ((uint32_t)data[2] << 8u) | data[3];
}

static void wchlink_command_target_init(
    struct wchlink_command_context *context) {
    wchlink_target_ports_init(context->target);
    wchlink_transfer_bind_target(context->transfer, context->target);
}

static void wchlink_command_target_disconnect(
    struct wchlink_command_context *context) {
    wchlink_target_ports_disconnect(context->target);
}

static bool wchlink_command_target_uses_ch5xx_loader(
    const struct wchlink_command_context *context) {
    return wchlink_target_ports_uses_ch5xx_loader(context->target);
}

static struct wchlink_session_command_result wchlink_handle_chip_info(
    struct wchlink_command_context *context, uint8_t *response,
    size_t capacity) {
    struct wchlink_wire_chip_info info = {
        .ch5xx = wchlink_command_target_uses_ch5xx_loader(context),
        .chip_id =
            wchlink_target_ports_chip_id(context->target),
    };
    struct rvswd_target_result read_result;

    if (capacity < 20u) {
        return wchlink_command_result(WCHLINK_SESSION_COMMAND_MALFORMED, 0u);
    }
    if (!info.ch5xx) {
        read_result = wchlink_target_ports_read_memory32(
            context->target, 0x1ffff7e0u);
        if (!read_result.ok) {
            return wchlink_command_result(
                WCHLINK_SESSION_COMMAND_TARGET_FAILED, 0u);
        }
        info.flash_size = read_result.value;
        read_result = wchlink_target_ports_read_memory32(
            context->target, 0x1ffff7e8u);
        if (!read_result.ok) {
            return wchlink_command_result(
                WCHLINK_SESSION_COMMAND_TARGET_FAILED, 0u);
        }
        info.uid_low = read_result.value;
        read_result = wchlink_target_ports_read_memory32(
            context->target, 0x1ffff7ecu);
        if (!read_result.ok) {
            return wchlink_command_result(
                WCHLINK_SESSION_COMMAND_TARGET_FAILED, 0u);
        }
        info.uid_high = read_result.value;
        read_result = wchlink_target_ports_read_memory32(
            context->target, 0x1ffff7f0u);
        if (!read_result.ok) {
            return wchlink_command_result(
                WCHLINK_SESSION_COMMAND_TARGET_FAILED, 0u);
        }
        info.uid_tail = read_result.value;
    }
    return wchlink_command_result(
        WCHLINK_SESSION_COMMAND_COMPLETED,
        wchlink_wire_chip_info(response, capacity, &info));
}

static struct wchlink_session_command_result wchlink_handle_dmi(
    struct wchlink_command_context *context, const uint8_t *request,
    uint8_t *response, size_t capacity) {
    uint8_t address;
    uint32_t data;
    struct rvswd_target_result result;

    if (capacity < 9u) {
        return wchlink_command_result(WCHLINK_SESSION_COMMAND_MALFORMED, 0u);
    }
    address = request[3];
    data = wchlink_read_be32(&request[4]);
    if (request[8] == WCHLINK_DMI_OPERATION_READ) {
        result = wchlink_target_ports_read_dmi(context->target,
                                               address);
        if (result.ok) {
            data = result.value;
        }
    } else if (request[8] == WCHLINK_DMI_OPERATION_WRITE) {
        result = wchlink_target_ports_write_dmi(context->target,
                                                address, data);
    } else {
        result = rvswd_target_result_failure(RVSWD_TARGET_RESULT_DMI, 0u,
                                             false);
    }

    if (request[8] != WCHLINK_DMI_OPERATION_READ &&
        request[8] != WCHLINK_DMI_OPERATION_WRITE) {
        return wchlink_command_result(
            WCHLINK_SESSION_COMMAND_MALFORMED,
            wchlink_wire_dmi_reply(response, capacity, address, data, false,
                                   false));
    }
    return wchlink_command_result(
        result.ok ? WCHLINK_SESSION_COMMAND_COMPLETED
                  : (result.retryable ? WCHLINK_SESSION_COMMAND_BUSY
                                      : WCHLINK_SESSION_COMMAND_TARGET_FAILED),
        wchlink_wire_dmi_reply(response, capacity, address, data, result.ok,
                               result.retryable));
}

static struct wchlink_session_command_result wchlink_handle_config(
    struct wchlink_command_context *context, const uint8_t *request,
    size_t request_length, uint8_t *response, size_t capacity) {
    bool protected;
    uint8_t result;
    struct rvswd_target_result target_result;

    if (request_length < 4u || capacity < 4u) {
        return wchlink_command_result(
            WCHLINK_SESSION_COMMAND_MALFORMED,
            wchlink_wire_unsupported(response, capacity,
                                     WCHLINK_FAMILY_CONFIG));
    }
    if (!wchlink_target_ports_is_connected(context->target)) {
        return wchlink_command_result(
            WCHLINK_SESSION_COMMAND_TARGET_FAILED,
            wchlink_wire_unsupported(response, capacity,
                                     WCHLINK_FAMILY_CONFIG));
    }

    switch (request[3]) {
        case WCHLINK_CONFIG_READ_PROTECTION:
            if (wchlink_command_target_uses_ch5xx_loader(context)) {
                // CH5xx 不使用 CH32 Option Byte，LinkE 将保护查询报告为未保护
                result = WCHLINK_CONFIG_READ_UNPROTECTED;
                break;
            }
            target_result = wchlink_target_ports_flash_read_protected(
                context->target);
            if (!target_result.ok) {
                return wchlink_command_result(
                    WCHLINK_SESSION_COMMAND_TARGET_FAILED,
                    wchlink_wire_target_error(response, capacity,
                                              target_result.code));
            }
            protected = target_result.value != 0u;
            result = protected ? WCHLINK_CONFIG_READ_PROTECTED
                               : WCHLINK_CONFIG_READ_UNPROTECTED;
            break;
        case WCHLINK_CONFIG_DISABLE_PROTECTION:
        case WCHLINK_CONFIG_ENABLE_PROTECTION:
            // 扩展帧还包含 USER 和 WRP 配置，不能按基础保护命令处理
            if (request_length != 4u) {
                return wchlink_command_result(
                    WCHLINK_SESSION_COMMAND_MALFORMED,
                    wchlink_wire_unsupported(response, capacity,
                                             WCHLINK_FAMILY_CONFIG));
            }
            target_result = wchlink_target_ports_flash_set_read_protected(
                context->target,
                request[3] == WCHLINK_CONFIG_ENABLE_PROTECTION);
            if (!target_result.ok) {
                return wchlink_command_result(
                    WCHLINK_SESSION_COMMAND_TARGET_FAILED,
                    wchlink_wire_target_error(response, capacity,
                                              target_result.code));
            }
            result = request[3];
            break;
        case WCHLINK_CONFIG_WRITE_PROTECTION:
            target_result = wchlink_target_ports_flash_write_protected(
                context->target);
            if (!target_result.ok) {
                return wchlink_command_result(
                    WCHLINK_SESSION_COMMAND_TARGET_FAILED,
                    wchlink_wire_target_error(response, capacity,
                                              target_result.code));
            }
            protected = target_result.value != 0u;
            result = protected ? WCHLINK_CONFIG_WRITE_PROTECTED
                               : WCHLINK_CONFIG_WRITE_UNPROTECTED;
            break;
        default:
            return wchlink_command_result(
                WCHLINK_SESSION_COMMAND_MALFORMED,
                wchlink_wire_unsupported(response, capacity,
                                         WCHLINK_FAMILY_CONFIG));
    }

    return wchlink_command_result(
        WCHLINK_SESSION_COMMAND_COMPLETED,
        wchlink_wire_command_reply(response, capacity, WCHLINK_FAMILY_CONFIG,
                                   result));
}

void wchlink_command_reset(struct wchlink_command_context *context) {
    // 失败连接也会配置调试引脚，所有会话复位都必须释放总线
    wchlink_command_target_disconnect(context);
    context->ch5xx_info_query_seen = false;
    wchlink_transfer_reset(context->transfer);
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

static struct wchlink_session_command_result wchlink_handle_partial_write(
    struct wchlink_command_context *context, const uint8_t *request,
    uint8_t *response, size_t response_capacity) {
    uint32_t address = wchlink_read_be32(&request[3]);

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

static struct wchlink_session_command_result wchlink_handle_memory_write(
    struct wchlink_command_context *context, const uint8_t *request,
    uint8_t *response, size_t response_capacity) {
    uint32_t address = wchlink_read_be32(&request[3]);
    uint32_t length = wchlink_read_be32(&request[7]);

    wchlink_transfer_prepare_write(context->transfer, address,
                                   length);
    return wchlink_command_result(
        WCHLINK_SESSION_COMMAND_COMPLETED,
        wchlink_wire_command_reply(response, response_capacity,
                                   WCHLINK_TRANSFER_FAMILY_WRITE, 0x01u));
}

static struct wchlink_session_command_result wchlink_handle_memory_read(
    struct wchlink_command_context *context, const uint8_t *request,
    uint8_t *response, size_t response_capacity) {
    uint32_t address = wchlink_read_be32(&request[3]);
    uint32_t length = wchlink_read_be32(&request[7]);

    wchlink_transfer_prepare_read(context->transfer, address,
                                  length);
    return wchlink_command_result(
        WCHLINK_SESSION_COMMAND_COMPLETED,
        wchlink_wire_ack(response, response_capacity,
                         WCHLINK_TRANSFER_FAMILY_READ));
}

static struct wchlink_session_command_result wchlink_handle_flash(
    struct wchlink_command_context *context, const uint8_t *request,
    uint8_t *response, size_t response_capacity) {
    const uint8_t family = WCHLINK_TRANSFER_FAMILY_FLASH;
    struct rvswd_target_result target_result;

    switch (request[3]) {
        case 0x01u:
            wchlink_transfer_clear_operation(context->transfer);
            target_result = rvswd_target_result_success();
            if (!wchlink_target_ports_is_connected(
                    context->target)) {
                // MRS 版本预检会先发送 STOP，基础全擦必须重新建立目标会话
                wchlink_command_target_init(context);
                target_result = wchlink_target_ports_connect(
                    context->target);
            }
            if (target_result.ok &&
                wchlink_target_ports_is_connected(
                    context->target)) {
                target_result = wchlink_target_ports_flash_erase_all(
                    context->target);
            }
            if (!target_result.ok ||
                !wchlink_target_ports_is_connected(
                    context->target)) {
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
            if (!wchlink_transfer_start_loader(
                    context->transfer)) {
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
                wchlink_transfer_finish_loader(context->transfer,
                                               request[3]);

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
            wchlink_transfer_abort(context->transfer);
            return wchlink_command_result(
                WCHLINK_SESSION_COMMAND_COMPLETED,
                wchlink_wire_ack(response, response_capacity, family));
        case 0x0cu:
            wchlink_transfer_begin_read(context->transfer);
            return wchlink_command_result(
                WCHLINK_SESSION_COMMAND_COMPLETED,
                wchlink_wire_ack(response, response_capacity, family));
        default:
            return wchlink_command_result(
                WCHLINK_SESSION_COMMAND_COMPLETED,
                wchlink_wire_ack(response, response_capacity, family));
    }
}

static struct wchlink_session_command_result wchlink_handle_info(
    struct wchlink_command_context *context, uint8_t *response,
    size_t response_capacity) {
    // MRS 会在 STOP 前读取扩展信息，只有该会话出现过查询才返回 20 字节
    if (wchlink_command_target_uses_ch5xx_loader(context)) {
        context->ch5xx_info_query_seen = true;
    }
    return wchlink_handle_chip_info(context, response, response_capacity);
}

static struct wchlink_session_command_result wchlink_handle_speed(
    struct wchlink_command_context *context, const uint8_t *request,
    size_t request_length, uint8_t *response, size_t response_capacity) {
    if (request_length < 5u) {
        return wchlink_command_result(
            WCHLINK_SESSION_COMMAND_MALFORMED,
            wchlink_wire_unsupported(response, response_capacity,
                                     WCHLINK_FAMILY_SPEED));
    }
    wchlink_target_ports_set_family_hint(context->target,
                                         request[3]);
    return wchlink_command_result(
        WCHLINK_SESSION_COMMAND_COMPLETED,
        wchlink_wire_command_reply(response, response_capacity,
                                   WCHLINK_FAMILY_SPEED, 1u));
}

static struct wchlink_session_command_result wchlink_handle_reset(
    struct wchlink_command_context *context, const uint8_t *request,
    size_t request_length, uint8_t *response, size_t response_capacity) {
    if (request_length < 4u) {
        return wchlink_command_result(
            WCHLINK_SESSION_COMMAND_MALFORMED,
            wchlink_wire_ack(response, response_capacity,
                             WCHLINK_FAMILY_RESET));
    }
    if (request[3] == WCHLINK_RESET_SOFT &&
        wchlink_target_ports_is_connected(context->target)) {
        if (!wchlink_target_ports_soft_reset_and_run(
                 context->target)
                 .ok) {
            return wchlink_command_result(
                WCHLINK_SESSION_COMMAND_TARGET_FAILED,
                wchlink_wire_unsupported(response, response_capacity,
                                         WCHLINK_FAMILY_RESET));
        }
        return wchlink_command_result(
            WCHLINK_SESSION_COMMAND_COMPLETED,
            wchlink_wire_command_reply(response, response_capacity,
                                       WCHLINK_FAMILY_RESET, request[3]));
    }
    if (request[3] == WCHLINK_RESET_MRS_RUN &&
        wchlink_target_ports_is_connected(context->target)) {
        if (!wchlink_target_ports_reset_and_run(context->target)
                 .ok) {
            return wchlink_command_result(
                WCHLINK_SESSION_COMMAND_TARGET_FAILED,
                wchlink_wire_unsupported(response, response_capacity,
                                         WCHLINK_FAMILY_RESET));
        }
        return wchlink_command_result(
            WCHLINK_SESSION_COMMAND_COMPLETED,
            wchlink_wire_command_reply(response, response_capacity,
                                       WCHLINK_FAMILY_RESET, request[3]));
    }
    if (request[3] == WCHLINK_RESET_NORMAL &&
        wchlink_target_ports_is_connected(context->target) &&
        !wchlink_target_ports_reset_and_halt(context->target)
             .ok) {
        return wchlink_command_result(
            WCHLINK_SESSION_COMMAND_TARGET_FAILED,
            wchlink_wire_unsupported(response, response_capacity,
                                     WCHLINK_FAMILY_RESET));
    }
    return wchlink_command_result(
        WCHLINK_SESSION_COMMAND_COMPLETED,
        wchlink_wire_ack(response, response_capacity, WCHLINK_FAMILY_RESET));
}

static struct wchlink_session_command_result wchlink_connect_result(
    const struct wchlink_command_context *context,
    struct rvswd_target_result target_result, uint8_t *response,
    size_t response_capacity) {
    bool connected = target_result.ok && wchlink_target_ports_is_connected(
                                             context->target);
    uint8_t family =
        wchlink_target_ports_family(context->target);

    return wchlink_command_result(
        connected && family != 0u ? WCHLINK_SESSION_COMMAND_COMPLETED
                                  : WCHLINK_SESSION_COMMAND_TARGET_FAILED,
        wchlink_wire_connect_reply(
            response, response_capacity, connected, target_result.code, family,
            wchlink_target_ports_chip_id(context->target)));
}

static struct wchlink_session_command_result wchlink_handle_control(
    struct wchlink_command_context *context, const uint8_t *request,
    size_t request_length, uint8_t *response, size_t response_capacity) {
    struct rvswd_target_result target_result;

    switch (request[3]) {
        case WCHLINK_CONTROL_IDENTIFY:
            wchlink_command_reset(context);
            return wchlink_command_result(
                WCHLINK_SESSION_COMMAND_COMPLETED,
                wchlink_wire_identity(response, response_capacity));
        case WCHLINK_CONTROL_CONNECT:
            wchlink_command_reset(context);
            wchlink_command_target_init(context);
            target_result =
                wchlink_target_ports_connect(context->target);
            return wchlink_connect_result(context, target_result, response,
                                          response_capacity);
        case WCHLINK_CONTROL_STOP: {
            bool return_ch5xx_info =
                context->ch5xx_info_query_seen &&
                wchlink_command_target_uses_ch5xx_loader(context);
            struct wchlink_session_command_result result;

            wchlink_command_reset(context);
            if (return_ch5xx_info) {
                // MRS 在 CH5xx 设置芯片阶段从 STOP 命令读取 20 字节目标信息
                result = wchlink_handle_chip_info(context, response,
                                                  response_capacity);
            } else {
                result = wchlink_command_result(
                    WCHLINK_SESSION_COMMAND_COMPLETED,
                    wchlink_wire_ack(response, response_capacity,
                                     WCHLINK_FAMILY_CONTROL));
            }
            // STOP 已结束目标会话，USB 需为这次最终回复保留独立生命周期
            result.response_policy = WCHLINK_SESSION_RESPONSE_SESSION_END;
            return result;
        }
        case WCHLINK_CONTROL_SET_CHIP_TYPE:
            // MRS 将设置目标型号命令作为首次目标连接入口
            if (wchlink_target_ports_is_connected(
                    context->target)) {
                // wlink 在已连接会话中使用同一子命令查询 ROM/RAM 分割
                return wchlink_command_result(
                    WCHLINK_SESSION_COMMAND_COMPLETED,
                    wchlink_wire_ack(response, response_capacity,
                                     WCHLINK_FAMILY_CONTROL));
            }
            // MRS 在设置两线速度后立即发起连接，目标调试模块需要短暂稳定时间
            bsp_delay_ms(20u);
            wchlink_command_target_init(context);
            target_result =
                wchlink_target_ports_connect(context->target);
            return wchlink_connect_result(context, target_result, response,
                                          response_capacity);
        case WCHLINK_CONTROL_CLEAR_CODE_FLASH:
        case WCHLINK_CONTROL_CLEAR_CODE_FLASH_B:
            if (request_length < 5u) {
                return wchlink_command_result(
                    WCHLINK_SESSION_COMMAND_MALFORMED,
                    wchlink_wire_unsupported(response, response_capacity,
                                             WCHLINK_FAMILY_CONTROL));
            }
            target_result = rvswd_target_result_success();
            if (!wchlink_target_ports_is_connected(
                    context->target)) {
                // MRS 直接通过清擦除命令建立目标会话，末字节携带目标 family
                wchlink_target_ports_set_family_hint(
                    context->target, request[4]);
                wchlink_command_target_init(context);
                target_result = wchlink_target_ports_connect(
                    context->target);
            }
            if (target_result.ok &&
                wchlink_target_ports_is_connected(
                    context->target)) {
                target_result = wchlink_target_ports_flash_erase_all(
                    context->target);
            }
            if (!target_result.ok ||
                !wchlink_target_ports_is_connected(
                    context->target)) {
                return wchlink_command_result(
                    WCHLINK_SESSION_COMMAND_TARGET_FAILED,
                    wchlink_wire_target_error(response, response_capacity,
                                              target_result.code));
            }
            wchlink_transfer_clear_operation(context->transfer);
            // MRS 的全擦命令要求回显原子命令
            return wchlink_command_result(
                WCHLINK_SESSION_COMMAND_COMPLETED,
                wchlink_wire_command_reply(
                    response, response_capacity, WCHLINK_FAMILY_CONTROL,
                    request[3]));
        case WCHLINK_CONTROL_POWER_3V3_ON:
            drv_dp_pullup_set_enabled(true);
            return wchlink_command_result(
                WCHLINK_SESSION_COMMAND_COMPLETED,
                wchlink_wire_ack(response, response_capacity,
                                 WCHLINK_FAMILY_CONTROL));
        case WCHLINK_CONTROL_POWER_3V3_OFF:
            drv_dp_pullup_set_enabled(false);
            return wchlink_command_result(
                WCHLINK_SESSION_COMMAND_COMPLETED,
                wchlink_wire_ack(response, response_capacity,
                                 WCHLINK_FAMILY_CONTROL));
        case WCHLINK_CONTROL_POWER_5V_ON:
            drv_power_switch_set_enabled(true);
            return wchlink_command_result(
                WCHLINK_SESSION_COMMAND_COMPLETED,
                wchlink_wire_ack(response, response_capacity,
                                 WCHLINK_FAMILY_CONTROL));
        case WCHLINK_CONTROL_POWER_5V_OFF:
            wchlink_transfer_invalidate_cache(context->transfer);
            wchlink_command_reset(context);
            drv_power_switch_set_enabled(false);
            return wchlink_command_result(
                WCHLINK_SESSION_COMMAND_COMPLETED,
                wchlink_wire_ack(response, response_capacity,
                                 WCHLINK_FAMILY_CONTROL));
        case WCHLINK_CONTROL_HOLD:
        case WCHLINK_CONTROL_RESET_LOW:
        default:
            return wchlink_command_result(
                WCHLINK_SESSION_COMMAND_COMPLETED,
                wchlink_wire_ack(response, response_capacity,
                                 WCHLINK_FAMILY_CONTROL));
    }
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
            if (request_length >= 9u) {
                return wchlink_handle_dmi(context, request, response,
                                          response_capacity);
            }
            break;
        case WCHLINK_FAMILY_CONFIG:
            return wchlink_handle_config(context, request, request_length,
                                         response, response_capacity);
        case WCHLINK_FAMILY_DEVICE_MODE:
            if (request_length >= 4u) {
                return wchlink_handle_device_mode(request, response,
                                                  response_capacity);
            }
            break;
        case WCHLINK_FAMILY_PARTIAL_WRITE:
            if (request_length >= 8u) {
                return wchlink_handle_partial_write(
                    context, request, response, response_capacity);
            }
            break;
        case WCHLINK_TRANSFER_FAMILY_WRITE:
            if (request_length >= 11u) {
                return wchlink_handle_memory_write(
                    context, request, response, response_capacity);
            }
            break;
        case WCHLINK_TRANSFER_FAMILY_READ:
            if (request_length >= 11u) {
                return wchlink_handle_memory_read(
                    context, request, response, response_capacity);
            }
            break;
        case WCHLINK_TRANSFER_FAMILY_FLASH:
            if (request_length >= 4u) {
                return wchlink_handle_flash(context, request, response,
                                            response_capacity);
            }
            break;
        case WCHLINK_FAMILY_INFO:
            return wchlink_handle_info(context, response, response_capacity);
        case WCHLINK_FAMILY_SPEED:
            return wchlink_handle_speed(context, request, request_length,
                                        response, response_capacity);
        case WCHLINK_FAMILY_RESET:
            return wchlink_handle_reset(context, request, request_length,
                                        response, response_capacity);
        case WCHLINK_FAMILY_CONTROL:
            if (request_length >= 4u) {
                return wchlink_handle_control(context, request, request_length,
                                              response, response_capacity);
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
    return wchlink_command_dispatch(context, request, request_length, response,
                                    response_capacity);
}
