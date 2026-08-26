#include "wchlink_session.h"

#include "bsp/bsp_delay.h"
#include "drv/drv_dp_pullup.h"
#include "drv/drv_power_switch.h"
#include "wchlink_family.h"
#include "wchlink_target_ports.h"
#include "wchlink_transfer.h"
#include "wchlink_wire.h"

#include <string.h>

struct wchlink_session {
    struct wchlink_target_ports target;
    struct wchlink_transfer transfer;
    bool ch5xx_info_query_seen;
    bool isp_request_pending;
};

static struct wchlink_session wchlink_session_state;

static void wchlink_target_init(void) {
    wchlink_target_ports_init(&wchlink_session_state.target);
    wchlink_transfer_bind_target(&wchlink_session_state.transfer,
                                 &wchlink_session_state.target);
}

static void wchlink_target_disconnect(void) {
    wchlink_target_ports_disconnect(&wchlink_session_state.target);
}

static bool wchlink_target_uses_ch5xx_loader(void) {
    return wchlink_target_ports_uses_ch5xx_loader(
        &wchlink_session_state.target);
}

static size_t wchlink_ack(uint8_t *response, size_t capacity, uint8_t family) {
    if (capacity < 4u) {
        return 0u;
    }
    response[0] = WCHLINK_REPLY_PREFIX;
    response[1] = family;
    response[2] = 1u;
    response[3] = 0u;
    return 4u;
}

static size_t wchlink_unsupported(uint8_t *response, size_t capacity, uint8_t family) {
    if (capacity < 4u) {
        return 0u;
    }
    response[0] = WCHLINK_COMMAND_PREFIX;
    response[1] = family;
    response[2] = 1u;
    response[3] = 2u;
    return 4u;
}

static size_t wchlink_target_error(uint8_t *response, size_t capacity,
                                   uint32_t code) {
    if (capacity < 4u) {
        return 0u;
    }
    response[0] = WCHLINK_COMMAND_PREFIX;
    response[1] = 0x55u;
    response[2] = 1u;
    response[3] = (uint8_t)code;
    return 4u;
}

static size_t wchlink_command_reply(uint8_t *response, size_t capacity,
                                    uint8_t family, uint8_t command) {
    if (capacity < 4u) {
        return 0u;
    }
    response[0] = WCHLINK_REPLY_PREFIX;
    response[1] = family;
    response[2] = 1u;
    response[3] = command;
    return 4u;
}

static size_t wchlink_identity(uint8_t *response, size_t capacity) {
    if (capacity < 7u) {
        return 0u;
    }
    response[0] = WCHLINK_REPLY_PREFIX;
    response[1] = WCHLINK_FAMILY_CONTROL;
    response[2] = 4u;
    // 官方 LinkE 与 MRS 版本检查使用 3.3 身份，CH592 不改变 Link 固件版本
    response[3] = 3u;
    response[4] = 3u;
    response[5] = 0x12u;
    response[6] = 0u;
    return 7u;
}

static size_t wchlink_connect_reply(uint8_t *response, size_t capacity,
                                    struct rvswd_target_result connect_result) {
    uint32_t chip_id =
        wchlink_target_ports_chip_id(&wchlink_session_state.target);
    uint8_t chip_family =
        wchlink_target_ports_family(&wchlink_session_state.target);
    bool connected = connect_result.ok &&
                     wchlink_target_ports_is_connected(
                         &wchlink_session_state.target);
    bool unsupported_chip = connect_result.ok && chip_family == 0u;

    if (unsupported_chip) {
        connected = false;
    }
    if (!connected) {
        if (capacity < 4u) {
            return 0u;
        }
        response[0] = WCHLINK_COMMAND_PREFIX;
        response[1] = 0x55u;
        response[2] = 1u;
        response[3] = unsupported_chip ? 0x30u : (uint8_t)connect_result.code;
        return 4u;
    }
    if (capacity < 8u) {
        return 0u;
    }
    response[0] = WCHLINK_REPLY_PREFIX;
    response[1] = WCHLINK_FAMILY_CONTROL;
    response[2] = 5u;
    response[3] = chip_family;
    response[4] = (uint8_t)(chip_id >> 24u);
    response[5] = (uint8_t)(chip_id >> 16u);
    response[6] = (uint8_t)(chip_id >> 8u);
    response[7] = (uint8_t)chip_id;
    return 8u;
}

static size_t wchlink_chip_info(uint8_t *response, size_t capacity) {
    uint32_t flash_size = 0u;
    uint32_t uid_low = 0u;
    uint32_t uid_high = 0u;
    uint32_t uid_tail = 0u;
    uint32_t chip_id =
        wchlink_target_ports_chip_id(&wchlink_session_state.target);

    if (capacity < 20u) {
        return 0u;
    }

    if (wchlink_target_uses_ch5xx_loader()) {
        // MRS 的 CH5xx FlashOperation 路径固定读取 20 字节，LinkE 将 ChipID 放在第 4 字节
        memset(response, 0, 20u);
        response[0] = WCHLINK_REPLY_PREFIX;
        response[1] = WCHLINK_FAMILY_CONTROL;
        response[2] = 1u;
        response[3] = 0xffu;
        response[4] = (uint8_t)(chip_id >> 24u);
        return 20u;
    }

    {
        struct rvswd_target_result read_result;

        read_result = wchlink_target_ports_read_memory32(
            &wchlink_session_state.target, 0x1ffff7e0u);
        if (!read_result.ok) {
            return 0u;
        }
        flash_size = read_result.value;
        read_result = wchlink_target_ports_read_memory32(
            &wchlink_session_state.target, 0x1ffff7e8u);
        if (!read_result.ok) {
            return 0u;
        }
        uid_low = read_result.value;
        read_result = wchlink_target_ports_read_memory32(
            &wchlink_session_state.target, 0x1ffff7ecu);
        if (!read_result.ok) {
            return 0u;
        }
        uid_high = read_result.value;
        read_result = wchlink_target_ports_read_memory32(
            &wchlink_session_state.target, 0x1ffff7f0u);
        if (!read_result.ok) {
            return 0u;
        }
        uid_tail = read_result.value;
    }

    // 芯片信息查询使用无帧头的 20 字节原始回复
    response[0] = 0xffu;
    response[1] = 0xffu;
    response[2] = (uint8_t)(flash_size >> 8u);
    response[3] = (uint8_t)flash_size;
    response[4] = (uint8_t)(uid_low >> 24u);
    response[5] = (uint8_t)(uid_low >> 16u);
    response[6] = (uint8_t)(uid_low >> 8u);
    response[7] = (uint8_t)uid_low;
    response[8] = (uint8_t)(uid_high >> 24u);
    response[9] = (uint8_t)(uid_high >> 16u);
    response[10] = (uint8_t)(uid_high >> 8u);
    response[11] = (uint8_t)uid_high;
    response[12] = (uint8_t)(uid_tail >> 24u);
    response[13] = (uint8_t)(uid_tail >> 16u);
    response[14] = (uint8_t)(uid_tail >> 8u);
    response[15] = (uint8_t)uid_tail;
    response[16] = (uint8_t)(chip_id >> 24u);
    response[17] = (uint8_t)(chip_id >> 16u);
    response[18] = (uint8_t)(chip_id >> 8u);
    response[19] = (uint8_t)chip_id;
    return 20u;
}

static size_t wchlink_dmi(const uint8_t *request, uint8_t *response, size_t capacity) {
    uint8_t address;
    uint32_t data;
    struct rvswd_target_result result;

    if (capacity < 9u) {
        return 0u;
    }
    address = request[3];
    data = ((uint32_t)request[4] << 24u) | ((uint32_t)request[5] << 16u) |
           ((uint32_t)request[6] << 8u) | request[7];
    if (request[8] == 1u) {
        result = wchlink_target_ports_read_dmi(&wchlink_session_state.target,
                                               address);
        if (result.ok) {
            data = result.value;
        }
    } else if (request[8] == 2u) {
        result = wchlink_target_ports_write_dmi(&wchlink_session_state.target,
                                                address, data);
    } else {
        result = rvswd_target_result_failure(RVSWD_TARGET_RESULT_DMI, 0u,
                                             false);
    }

    response[0] = WCHLINK_REPLY_PREFIX;
    response[1] = WCHLINK_FAMILY_DMI;
    response[2] = 6u;
    response[3] = address;
    response[4] = (uint8_t)(data >> 24u);
    response[5] = (uint8_t)(data >> 16u);
    response[6] = (uint8_t)(data >> 8u);
    response[7] = (uint8_t)data;
    response[8] = result.ok ? 0u : (result.retryable ? 3u : 2u);
    return 9u;
}

static size_t wchlink_config(const uint8_t *request, size_t request_length,
                             uint8_t *response, size_t capacity) {
    bool protected;
    uint8_t result;
    struct rvswd_target_result target_result;

    if (request_length < 4u || capacity < 4u ||
        !wchlink_target_ports_is_connected(&wchlink_session_state.target)) {
        return wchlink_unsupported(response, capacity, WCHLINK_FAMILY_CONFIG);
    }

    switch (request[3]) {
        case WCHLINK_CONFIG_READ_PROTECTION:
            if (wchlink_target_uses_ch5xx_loader()) {
                // CH5xx 不使用 CH32 Option Byte，LinkE 将保护查询报告为未保护
                result = WCHLINK_CONFIG_READ_UNPROTECTED;
                break;
            }
            target_result = wchlink_target_ports_flash_read_protected(
                &wchlink_session_state.target);
            if (!target_result.ok) {
                return wchlink_target_error(response, capacity,
                                            target_result.code);
            }
            protected = target_result.value != 0u;
            result = protected ? WCHLINK_CONFIG_READ_PROTECTED
                               : WCHLINK_CONFIG_READ_UNPROTECTED;
            break;
        case WCHLINK_CONFIG_DISABLE_PROTECTION:
        case WCHLINK_CONFIG_ENABLE_PROTECTION:
            // 扩展帧还包含 USER 和 WRP 配置，不能按基础保护命令处理
            if (request_length != 4u) {
                return wchlink_unsupported(response, capacity,
                                           WCHLINK_FAMILY_CONFIG);
            }
            target_result = wchlink_target_ports_flash_set_read_protected(
                &wchlink_session_state.target,
                request[3] == WCHLINK_CONFIG_ENABLE_PROTECTION);
            if (!target_result.ok) {
                return request_length == 4u
                           ? wchlink_target_error(response, capacity,
                                                  target_result.code)
                           : wchlink_unsupported(response, capacity,
                                                 WCHLINK_FAMILY_CONFIG);
            }
            result = request[3];
            break;
        case WCHLINK_CONFIG_WRITE_PROTECTION:
            target_result = wchlink_target_ports_flash_write_protected(
                &wchlink_session_state.target);
            if (!target_result.ok) {
                return wchlink_target_error(response, capacity,
                                            target_result.code);
            }
            protected = target_result.value != 0u;
            result = protected ? WCHLINK_CONFIG_WRITE_PROTECTED
                               : WCHLINK_CONFIG_WRITE_UNPROTECTED;
            break;
        default:
            return wchlink_unsupported(response, capacity, WCHLINK_FAMILY_CONFIG);
    }

    response[0] = WCHLINK_REPLY_PREFIX;
    response[1] = WCHLINK_FAMILY_CONFIG;
    response[2] = 1u;
    response[3] = result;
    return 4u;
}

void wchlink_session_reset(void) {
    // 失败连接也会配置调试引脚，所有会话复位都必须释放总线
    wchlink_target_disconnect();
    wchlink_session_state.ch5xx_info_query_seen = false;
    wchlink_session_state.isp_request_pending = false;
    wchlink_transfer_reset(&wchlink_session_state.transfer);
}

bool wchlink_session_take_isp_request(void) {
    bool pending = wchlink_session_state.isp_request_pending;

    wchlink_session_state.isp_request_pending = false;
    return pending;
}

enum wchlink_session_data_io wchlink_session_next_data_io(void) {
    enum wchlink_transfer_io_request transfer_io =
        wchlink_transfer_next_io(&wchlink_session_state.transfer);
    unsigned int session_io = WCHLINK_SESSION_DATA_IO_NONE;

    if ((transfer_io & WCHLINK_TRANSFER_IO_DATA_IN) != 0u) {
        session_io |= WCHLINK_SESSION_DATA_IO_IN;
    }
    if ((transfer_io & WCHLINK_TRANSFER_IO_DATA_OUT) != 0u) {
        session_io |= WCHLINK_SESSION_DATA_IO_OUT;
    }
    return (enum wchlink_session_data_io)session_io;
}

size_t wchlink_session_poll_data_in(uint8_t *data, size_t capacity) {
    uint8_t status;

    if (data == NULL || capacity < 4u) {
        return 0u;
    }
    if (!wchlink_transfer_take_reply_status(&wchlink_session_state.transfer,
                                            &status)) {
        return wchlink_transfer_read_data(&wchlink_session_state.transfer, data,
                                          capacity);
    }
    data[0] = 0x41u;
    data[1] = 0x01u;
    data[2] = 0x01u;
    data[3] = status;
    return 4u;
}

void wchlink_session_submit_data_out(const uint8_t *data, size_t length) {
    wchlink_transfer_write_data(&wchlink_session_state.transfer, data, length);
}
size_t wchlink_session_process(const uint8_t *request, size_t request_length,
                               uint8_t *response, size_t response_capacity) {
    uint8_t family;
    size_t response_length;
    struct rvswd_target_result target_result;

    if (request == NULL || response == NULL || request_length < 2u || request[0] != WCHLINK_COMMAND_PREFIX) {
        return wchlink_ack(response, response_capacity, 0u);
    }

    family = request[1];
    if (family == WCHLINK_FAMILY_DMI && request_length >= 9u) {
        return wchlink_dmi(request, response, response_capacity);
    }
    if (family == WCHLINK_FAMILY_CONFIG) {
        return wchlink_config(request, request_length, response, response_capacity);
    }
    if (family == WCHLINK_FAMILY_DEVICE_MODE && request_length >= 4u) {
        if (request[3] == WCHLINK_DEVICE_MODE_QUERY) {
            // MRS 通过第四字节 2 识别 Link 当前处于 RISC-V 调试模式
            return wchlink_command_reply(response, response_capacity,
                                         WCHLINK_FAMILY_DEVICE_MODE,
                                         WCHLINK_DEVICE_MODE_QUERY);
        }
        if (request[3] == WCHLINK_DEVICE_MODE_IAP) {
            // 保留官方 SetIAPMode 的无响应语义，由 USB 层切换到本探针的维护 ISP
            wchlink_session_state.isp_request_pending = true;
            return SIZE_MAX;
        }
        return wchlink_unsupported(response, response_capacity,
                                   WCHLINK_FAMILY_DEVICE_MODE);
    }
    if (family == WCHLINK_FAMILY_PARTIAL_WRITE && request_length >= 8u) {
        uint32_t address = ((uint32_t)request[3] << 24u) |
                           ((uint32_t)request[4] << 16u) |
                           ((uint32_t)request[5] << 8u) | request[6];

        if (!wchlink_transfer_start_partial_write(
                &wchlink_session_state.transfer, address, request[7])) {
            return wchlink_unsupported(response, response_capacity,
                                       WCHLINK_FAMILY_PARTIAL_WRITE);
        }
        return wchlink_command_reply(response, response_capacity,
                                     WCHLINK_FAMILY_PARTIAL_WRITE, request[2]);
    }
    if (family == 0x01u && request_length >= 11u) {
        uint32_t first = ((uint32_t)request[3] << 24u) |
                         ((uint32_t)request[4] << 16u) |
                         ((uint32_t)request[5] << 8u) | request[6];
        uint32_t second = ((uint32_t)request[7] << 24u) |
                          ((uint32_t)request[8] << 16u) |
                          ((uint32_t)request[9] << 8u) | request[10];

        wchlink_transfer_prepare_write(&wchlink_session_state.transfer, first,
                                       second);
        return wchlink_command_reply(response, response_capacity, family, 0x01u);
    }
    if (family == 0x03u && request_length >= 11u) {
        uint32_t address = ((uint32_t)request[3] << 24u) |
                           ((uint32_t)request[4] << 16u) |
                           ((uint32_t)request[5] << 8u) | request[6];
        uint32_t length = ((uint32_t)request[7] << 24u) |
                          ((uint32_t)request[8] << 16u) |
                          ((uint32_t)request[9] << 8u) | request[10];

        wchlink_transfer_prepare_read(&wchlink_session_state.transfer, address,
                                      length);
        return wchlink_ack(response, response_capacity, family);
    }
    if (family == 0x02u && request_length >= 4u) {
        switch (request[3]) {
            case 0x01u:
                wchlink_transfer_clear_operation(
                    &wchlink_session_state.transfer);
                target_result = rvswd_target_result_success();
                if (!wchlink_target_ports_is_connected(
                        &wchlink_session_state.target)) {
                    // MRS 版本预检会先发送 STOP，基础全擦必须重新建立目标会话
                    wchlink_target_init();
                    target_result = wchlink_target_ports_connect(
                        &wchlink_session_state.target);
                }
                if (target_result.ok &&
                    wchlink_target_ports_is_connected(
                        &wchlink_session_state.target)) {
                    target_result = wchlink_target_ports_flash_erase_all(
                        &wchlink_session_state.target);
                }
                if (!target_result.ok ||
                    !wchlink_target_ports_is_connected(
                        &wchlink_session_state.target)) {
                    if (response_capacity >= 4u) {
                        response[0] = WCHLINK_COMMAND_PREFIX;
                        response[1] = family;
                        response[2] = 1u;
                        response[3] = (uint8_t)target_result.code;
                        return 4u;
                    }
                    return wchlink_unsupported(response, response_capacity, family);
                }
                response_length = wchlink_ack(response, response_capacity, family);
                if (response_length != 0u) {
                    // MRS 的基础全擦命令要求应答状态为 1
                    response[3] = 0x01u;
                }
                return response_length;
            case 0x05u:
                if (!wchlink_transfer_start_loader(
                        &wchlink_session_state.transfer)) {
                    return wchlink_unsupported(response, response_capacity, family);
                }
                return wchlink_command_reply(response, response_capacity, family,
                                             request[3]);
            case 0x06u:
                // 官方 OpenOCD 在地址设置前发送 Prepare，状态必须跨过地址帧保留
                wchlink_transfer_mark_flash_prepare(
                    &wchlink_session_state.transfer);
                return wchlink_command_reply(response, response_capacity, family,
                                             request[3]);
            case 0x07u:
            case 0x0bu: {
                struct wchlink_transfer_finish_result finish =
                    wchlink_transfer_finish_loader(
                        &wchlink_session_state.transfer, request[3]);

                if (finish.status == WCHLINK_TRANSFER_FINISH_LOADER_ERROR) {
                    if (response_capacity < 13u) {
                        return 0u;
                    }
                    response[0] = WCHLINK_COMMAND_PREFIX;
                    response[1] = family;
                    response[2] = 10u;
                    response[3] = finish.loader_error;
                    response[4] = finish.dmi_status;
                    response[5] = (uint8_t)(finish.address >> 24u);
                    response[6] = (uint8_t)(finish.address >> 16u);
                    response[7] = (uint8_t)(finish.address >> 8u);
                    response[8] = (uint8_t)finish.address;
                    response[9] = (uint8_t)(finish.abstractcs >> 24u);
                    response[10] = (uint8_t)(finish.abstractcs >> 16u);
                    response[11] = (uint8_t)(finish.abstractcs >> 8u);
                    response[12] = (uint8_t)finish.abstractcs;
                    return 13u;
                }
                if (finish.status == WCHLINK_TRANSFER_FINISH_INCOMPLETE) {
                    return wchlink_unsupported(response, response_capacity,
                                               family);
                }
                if (finish.status == WCHLINK_TRANSFER_FINISH_READY) {
                    return wchlink_command_reply(response, response_capacity,
                                                 family, request[3]);
                }
                if (response_capacity >= 4u) {
                    response[0] = WCHLINK_COMMAND_PREFIX;
                    response[1] = family;
                    response[2] = 1u;
                    response[3] = (uint8_t)finish.target_value;
                    return 4u;
                }
                return 0u;
            }
            case 0x02u:
            case 0x03u:
            case 0x04u:
                if (!wchlink_transfer_start_flash(
                        &wchlink_session_state.transfer, request[3])) {
                    return wchlink_unsupported(response, response_capacity, family);
                }
                return wchlink_command_reply(response, response_capacity, family,
                                             request[3]);
            case 0x08u:
                wchlink_transfer_abort(&wchlink_session_state.transfer);
                return wchlink_ack(response, response_capacity, family);
            case 0x0cu:
                wchlink_transfer_begin_read(&wchlink_session_state.transfer);
                return wchlink_ack(response, response_capacity, family);
            default:
                return wchlink_ack(response, response_capacity, family);
        }
    }
    if (family == WCHLINK_FAMILY_INFO) {
        // MRS 会在 STOP 前读取扩展信息，只有该会话出现过查询才返回 20 字节
        if (wchlink_target_uses_ch5xx_loader()) {
            wchlink_session_state.ch5xx_info_query_seen = true;
        }
        return wchlink_chip_info(response, response_capacity);
    }
    if (family == WCHLINK_FAMILY_SPEED) {
        if (request_length < 5u) {
            return wchlink_unsupported(response, response_capacity, family);
        }
        wchlink_target_ports_set_family_hint(&wchlink_session_state.target,
                                             request[3]);
        response_length = wchlink_ack(response, response_capacity, family);
        if (response_length != 0u) {
            response[3] = 1u;
        }
        return response_length;
    }
    if (family == WCHLINK_FAMILY_RESET) {
        if (request_length >= 4u && request[3] == WCHLINK_RESET_SOFT &&
            wchlink_target_ports_is_connected(&wchlink_session_state.target)) {
            if (!wchlink_target_ports_soft_reset_and_run(
                     &wchlink_session_state.target)
                     .ok) {
                return wchlink_unsupported(response, response_capacity, family);
            }
            return wchlink_command_reply(response, response_capacity, family,
                                         request[3]);
        }
        if (request_length >= 4u && request[3] == WCHLINK_RESET_MRS_RUN &&
            wchlink_target_ports_is_connected(&wchlink_session_state.target)) {
            if (!wchlink_target_ports_reset_and_run(
                     &wchlink_session_state.target)
                     .ok) {
                return wchlink_unsupported(response, response_capacity, family);
            }
            return wchlink_command_reply(response, response_capacity, family,
                                         request[3]);
        }
        if (request_length >= 4u && request[3] == WCHLINK_RESET_NORMAL &&
            wchlink_target_ports_is_connected(&wchlink_session_state.target) &&
            !wchlink_target_ports_reset_and_halt(&wchlink_session_state.target)
                 .ok) {
            return wchlink_unsupported(response, response_capacity, family);
        }
        return wchlink_ack(response, response_capacity, family);
    }
    if (family != WCHLINK_FAMILY_CONTROL || request_length < 4u) {
        return wchlink_ack(response, response_capacity, family);
    }

    switch (request[3]) {
        case WCHLINK_CONTROL_IDENTIFY:
            wchlink_session_reset();
            return wchlink_identity(response, response_capacity);
        case WCHLINK_CONTROL_CONNECT:
            wchlink_session_reset();
            wchlink_target_init();
            target_result =
                wchlink_target_ports_connect(&wchlink_session_state.target);
            return wchlink_connect_reply(response, response_capacity,
                                         target_result);
        case WCHLINK_CONTROL_STOP: {
            bool return_ch5xx_info =
                wchlink_session_state.ch5xx_info_query_seen && wchlink_target_uses_ch5xx_loader();

            wchlink_session_reset();
            if (return_ch5xx_info) {
                // MRS 在 CH5xx 设置芯片阶段从 STOP 命令读取 20 字节目标信息
                return wchlink_chip_info(response, response_capacity);
            }
            return wchlink_ack(response, response_capacity, family);
        }
        case WCHLINK_CONTROL_SET_CHIP_TYPE:
            // MRS 将设置目标型号命令作为首次目标连接入口
            if (wchlink_target_ports_is_connected(
                    &wchlink_session_state.target)) {
                // wlink 在已连接会话中使用同一子命令查询 ROM/RAM 分割
                return wchlink_ack(response, response_capacity, family);
            }
            // MRS 在设置两线速度后立即发起连接，目标调试模块需要短暂稳定时间
            bsp_delay_ms(20u);
            wchlink_target_init();
            target_result =
                wchlink_target_ports_connect(&wchlink_session_state.target);
            return wchlink_connect_reply(response, response_capacity,
                                         target_result);
        case WCHLINK_CONTROL_CLEAR_CODE_FLASH:
        case WCHLINK_CONTROL_CLEAR_CODE_FLASH_B:
            if (request_length < 5u) {
                return wchlink_unsupported(response, response_capacity, family);
            }
            target_result = rvswd_target_result_success();
            if (!wchlink_target_ports_is_connected(
                    &wchlink_session_state.target)) {
                // MRS 直接通过清擦除命令建立目标会话，末字节携带目标 family
                wchlink_target_ports_set_family_hint(
                    &wchlink_session_state.target, request[4]);
                wchlink_target_init();
                target_result = wchlink_target_ports_connect(
                    &wchlink_session_state.target);
            }
            if (target_result.ok &&
                wchlink_target_ports_is_connected(
                    &wchlink_session_state.target)) {
                target_result = wchlink_target_ports_flash_erase_all(
                    &wchlink_session_state.target);
            }
            if (!target_result.ok ||
                !wchlink_target_ports_is_connected(
                    &wchlink_session_state.target)) {
                return wchlink_target_error(response, response_capacity,
                                            target_result.code);
            }
            wchlink_transfer_clear_operation(&wchlink_session_state.transfer);
            response_length = wchlink_ack(response, response_capacity, family);
            if (response_length != 0u) {
                // MRS 的全擦命令要求回显原子命令
                response[3] = request[3];
            }
            return response_length;
        case WCHLINK_CONTROL_POWER_3V3_ON:
            drv_dp_pullup_set_enabled(true);
            return wchlink_ack(response, response_capacity, family);
        case WCHLINK_CONTROL_POWER_3V3_OFF:
            drv_dp_pullup_set_enabled(false);
            return wchlink_ack(response, response_capacity, family);
        case WCHLINK_CONTROL_POWER_5V_ON:
            drv_power_switch_set_enabled(true);
            return wchlink_ack(response, response_capacity, family);
        case WCHLINK_CONTROL_POWER_5V_OFF:
            wchlink_transfer_invalidate_cache(&wchlink_session_state.transfer);
            wchlink_session_reset();
            drv_power_switch_set_enabled(false);
            return wchlink_ack(response, response_capacity, family);
        case WCHLINK_CONTROL_HOLD:
        case WCHLINK_CONTROL_RESET_LOW:
            return wchlink_ack(response, response_capacity, family);
        default:
            return wchlink_ack(response, response_capacity, family);
    }
}
