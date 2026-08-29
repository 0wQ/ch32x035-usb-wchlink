// Target command cluster 负责目标访问、连接控制和供电命令，不持有 USB buffer
#include "bsp/bsp_delay.h"
#include "drv/drv_power_switch.h"
#include "wchlink/protocol/wchlink_family.h"
#include "wchlink/protocol/wchlink_wire.h"
#include "wchlink/session/wchlink_command_internal.h"
#include "wchlink/target/wchlink_target_control.h"
#include "wchlink/target/wchlink_target_dmi.h"
#include "wchlink/target/wchlink_target_flash.h"

#include <stdbool.h>

enum {
    WCHLINK_CONFIG_PAYLOAD_OFFSET = 4u,
};

void wchlink_command_target_init(struct wchlink_command_context *context) {
    wchlink_target_ports_init(context->target);
    wchlink_transfer_bind_target(context->transfer, context->target);
}

static void wchlink_command_target_disconnect(
    struct wchlink_command_context *context) {
    wchlink_target_ports_disconnect(context->target);
}

static bool wchlink_command_target_uses_ch5xx_loader(
    const struct wchlink_command_context *context) {
    return wchlink_target_ports_info(context->target).loader ==
           RVSWD_TARGET_LOADER_CH5XX;
}

static struct wchlink_session_command_result wchlink_handle_chip_info(
    struct wchlink_command_context *context, uint8_t *response,
    size_t capacity) {
    struct rvswd_target_chip_info_result target_info;
    struct wchlink_wire_chip_info wire_info;

    if (capacity < 20u) {
        return wchlink_command_result(WCHLINK_SESSION_COMMAND_MALFORMED, 0u);
    }
    target_info = wchlink_target_ports_read_chip_info(context->target);
    if (!target_info.result.ok) {
        return wchlink_command_result(WCHLINK_SESSION_COMMAND_TARGET_FAILED,
                                      0u);
    }
    wire_info = (struct wchlink_wire_chip_info){
        .ch5xx = target_info.info.ch5xx,
        .flash_size = target_info.info.flash_size,
        .uid_low = target_info.info.uid_low,
        .uid_high = target_info.info.uid_high,
        .uid_tail = target_info.info.uid_tail,
        .chip_id = target_info.info.chip_id,
    };
    return wchlink_command_result(
        WCHLINK_SESSION_COMMAND_COMPLETED,
        wchlink_wire_chip_info(response, capacity, &wire_info));
}

static struct wchlink_session_command_result wchlink_handle_memory_type(
    struct wchlink_command_context *context, bool extended, bool write,
    const uint8_t *request, size_t request_length, uint8_t *response,
    size_t capacity) {
    struct rvswd_target_result target_result;

    if (write) {
        if (request_length != 5u) {
            return wchlink_command_result(
                WCHLINK_SESSION_COMMAND_MALFORMED,
                wchlink_wire_unsupported(response, capacity,
                                         WCHLINK_FAMILY_CONTROL));
        }
        target_result = wchlink_target_ports_flash_set_memory_type(
            context->target, extended, request[4]);
        if (!target_result.ok) {
            return wchlink_command_result(
                WCHLINK_SESSION_COMMAND_TARGET_FAILED,
                wchlink_wire_target_error(response, capacity,
                                          target_result.code));
        }
        return wchlink_command_result(
            WCHLINK_SESSION_COMMAND_COMPLETED,
            wchlink_wire_command_reply(response, capacity,
                                       WCHLINK_FAMILY_CONTROL, request[3]));
    }

    if (request_length != 4u) {
        return wchlink_command_result(
            WCHLINK_SESSION_COMMAND_MALFORMED,
            wchlink_wire_unsupported(response, capacity,
                                     WCHLINK_FAMILY_CONTROL));
    }
    target_result = wchlink_target_ports_flash_read_memory_type(
        context->target, extended);
    if (!target_result.ok) {
        return wchlink_command_result(
            WCHLINK_SESSION_COMMAND_TARGET_FAILED,
            wchlink_wire_target_error(response, capacity, target_result.code));
    }
    // 查询回复的第四字节是目标分配编码，不是查询命令编号
    return wchlink_command_result(
        WCHLINK_SESSION_COMMAND_COMPLETED,
        wchlink_wire_command_reply(response, capacity, WCHLINK_FAMILY_CONTROL,
                                   (uint8_t)target_result.value));
}

static struct wchlink_session_command_result wchlink_handle_qe(
    const uint8_t *request, size_t request_length, uint8_t *response,
    size_t capacity) {
    if (request_length != 4u) {
        return wchlink_command_result(
            WCHLINK_SESSION_COMMAND_MALFORMED,
            wchlink_wire_unsupported(response, capacity,
                                     WCHLINK_FAMILY_CONTROL));
    }
    if (request[3] == WCHLINK_CONTROL_CHECK_QE) {
        // 内置 Code Flash 不需要外部 QE 配置，向主机报告已处于可用状态
        return wchlink_command_result(
            WCHLINK_SESSION_COMMAND_COMPLETED,
            wchlink_wire_ack(response, capacity, WCHLINK_FAMILY_CONTROL));
    }
    // 内置存储没有独立 QE 寄存器，启用请求保持幂等并回显成功子命令
    return wchlink_command_result(
        WCHLINK_SESSION_COMMAND_COMPLETED,
        wchlink_wire_command_reply(response, capacity, WCHLINK_FAMILY_CONTROL,
                                   request[3]));
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
    data = wchlink_command_read_be32(&request[4]);
    if (request[8] == WCHLINK_DMI_OPERATION_READ) {
        if (wchlink_direct_dmi_resume_take_status(
                &context->direct_dmi_resume, address, &data)) {
            result = rvswd_target_result_success();
        } else {
            result = wchlink_target_ports_read_dmi(context->target, address);
            if (result.ok) {
                data = result.value;
            }
        }
    } else if (request[8] == WCHLINK_DMI_OPERATION_WRITE) {
        wchlink_direct_dmi_resume_reset(&context->direct_dmi_resume);
        if (wchlink_direct_dmi_resume_is_request(address, data)) {
            // resume completion 保持当前 DPC，只为缺少轮询的主机完成 WCH 调试握手
            result = wchlink_target_ports_resume_dmi(context->target, data);
            if (result.ok) {
                wchlink_direct_dmi_resume_store_status(
                    &context->direct_dmi_resume, result.value);
            }
        } else {
            result = wchlink_target_ports_write_dmi(context->target, address,
                                                    data);
        }
    } else {
        wchlink_direct_dmi_resume_reset(&context->direct_dmi_resume);
        result = rvswd_target_result_failure(RVSWD_TARGET_RESULT_DMI, 0u, false);
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
    if (!wchlink_target_ports_info(context->target).connected) {
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
            if (request_length ==
                WCHLINK_CONFIG_PAYLOAD_OFFSET +
                    RVSWD_OPTION_CONFIG_BYTE_COUNT) {
                // 扩展解除保护帧携带 USER、DATA0、DATA1 和 WRP0..3
                target_result = wchlink_target_ports_flash_set_option_bytes(
                    context->target, &request[WCHLINK_CONFIG_PAYLOAD_OFFSET],
                    RVSWD_OPTION_CONFIG_BYTE_COUNT);
                if (!target_result.ok) {
                    return wchlink_command_result(
                        WCHLINK_SESSION_COMMAND_TARGET_FAILED,
                        wchlink_wire_target_error(response, capacity, target_result.code));
                }
                result = request[3];
                break;
            }
            if (request_length != 4u) {
                return wchlink_command_result(
                    WCHLINK_SESSION_COMMAND_MALFORMED,
                    wchlink_wire_unsupported(response, capacity,
                                             WCHLINK_FAMILY_CONFIG));
            }
            target_result = wchlink_target_ports_flash_set_read_protected(
                context->target, false);
            if (!target_result.ok) {
                return wchlink_command_result(
                    WCHLINK_SESSION_COMMAND_TARGET_FAILED,
                    wchlink_wire_target_error(response, capacity,
                                              target_result.code));
            }
            result = request[3];
            break;
        case WCHLINK_CONFIG_ENABLE_PROTECTION:
            // 启用保护只接受基础帧，避免丢弃扩展 Option Byte 参数
            if (request_length != 4u) {
                return wchlink_command_result(
                    WCHLINK_SESSION_COMMAND_MALFORMED,
                    wchlink_wire_unsupported(response, capacity,
                                             WCHLINK_FAMILY_CONFIG));
            }
            target_result = wchlink_target_ports_flash_set_read_protected(
                context->target, true);
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
    wchlink_direct_dmi_resume_reset(&context->direct_dmi_resume);
    wchlink_transfer_reset(context->transfer);
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
    wchlink_target_ports_set_speed(context->target, request[4]);
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
        wchlink_target_ports_info(context->target).connected) {
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
        wchlink_target_ports_info(context->target).connected) {
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
        wchlink_target_ports_info(context->target).connected &&
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
    struct rvswd_target_info info =
        wchlink_target_ports_info(context->target);
    bool connected = target_result.ok && info.connected;

    return wchlink_command_result(
        connected && info.family != 0u ? WCHLINK_SESSION_COMMAND_COMPLETED
                                       : WCHLINK_SESSION_COMMAND_TARGET_FAILED,
        wchlink_wire_connect_reply(
            response, response_capacity, connected, target_result.code,
            info.family, info.chip_id));
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
            if (wchlink_target_ports_info(context->target).connected) {
                // 旧版 ROM/RAM 查询与设置芯片型号使用同一个子命令
                return wchlink_handle_memory_type(
                    context, false, false, request, request_length, response,
                    response_capacity);
            }
            // MRS 在设置两线速度后立即发起连接，目标调试模块需要短暂稳定时间
            bsp_delay_ms(20u);
            wchlink_command_target_init(context);
            target_result =
                wchlink_target_ports_connect(context->target);
            return wchlink_connect_result(context, target_result, response,
                                          response_capacity);
        case WCHLINK_CONTROL_SET_ROMRAM_OLD:
            return wchlink_handle_memory_type(
                context, false, true, request, request_length, response,
                response_capacity);
        case WCHLINK_CONTROL_GET_ROMRAM_NEW:
            return wchlink_handle_memory_type(
                context, true, false, request, request_length, response,
                response_capacity);
        case WCHLINK_CONTROL_SET_ROMRAM_NEW:
            return wchlink_handle_memory_type(
                context, true, true, request, request_length, response,
                response_capacity);
        case WCHLINK_CONTROL_CHECK_QE:
        case WCHLINK_CONTROL_ENABLE_QE:
            return wchlink_handle_qe(request, request_length, response,
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
            if (!wchlink_target_ports_info(context->target).connected) {
                // MRS 直接通过清擦除命令建立目标会话，末字节携带目标 family
                wchlink_target_ports_set_family_hint(
                    context->target, request[4]);
                wchlink_command_target_init(context);
                target_result = wchlink_target_ports_connect(
                    context->target);
            }
            if (target_result.ok &&
                wchlink_target_ports_info(context->target).connected) {
                target_result = wchlink_target_ports_flash_erase_all(
                    context->target);
            }
            if (!target_result.ok ||
                !wchlink_target_ports_info(context->target).connected) {
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
        case WCHLINK_CONTROL_POWER_5V_ON:
            drv_power_switch_set_enabled(true);
            return wchlink_command_result(
                WCHLINK_SESSION_COMMAND_COMPLETED,
                wchlink_wire_command_reply(
                    response, response_capacity, WCHLINK_FAMILY_CONTROL,
                    request[3]));
        case WCHLINK_CONTROL_POWER_3V3_OFF:
        case WCHLINK_CONTROL_POWER_5V_OFF:
            wchlink_transfer_invalidate_cache(context->transfer);
            wchlink_command_reset(context);
            drv_power_switch_set_enabled(false);
            return wchlink_command_result(
                WCHLINK_SESSION_COMMAND_COMPLETED,
                wchlink_wire_command_reply(
                    response, response_capacity, WCHLINK_FAMILY_CONTROL,
                    request[3]));
        case WCHLINK_CONTROL_HOLD:
        case WCHLINK_CONTROL_RESET_LOW:
        default:
            return wchlink_command_result(
                WCHLINK_SESSION_COMMAND_COMPLETED,
                wchlink_wire_ack(response, response_capacity,
                                 WCHLINK_FAMILY_CONTROL));
    }
}

struct wchlink_session_command_result wchlink_command_handle_target(
    struct wchlink_command_context *context, const uint8_t *request,
    size_t request_length, uint8_t *response, size_t response_capacity) {
    switch (request[1]) {
        case WCHLINK_FAMILY_DMI:
            if (request_length >= 9u) {
                return wchlink_handle_dmi(context, request, response,
                                          response_capacity);
            }
            break;
        case WCHLINK_FAMILY_CONFIG:
            return wchlink_handle_config(context, request, request_length,
                                         response, response_capacity);
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
        wchlink_wire_ack(response, response_capacity, request[1]));
}
