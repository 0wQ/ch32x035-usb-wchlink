#include "wchlink/session/wchlink_session.h"

#include "wchlink/protocol/wchlink_wire.h"
#include "wchlink/session/wchlink_command_internal.h"
#include "wchlink/session/wchlink_transfer.h"
#include "wchlink/target/wchlink_target_ports_internal.h"

struct wchlink_session {
    struct wchlink_target_ports target;
    struct wchlink_transfer transfer;
};

// Session 独占目标和传输状态，command context 只保存同生命周期的稳定引用
static struct wchlink_session wchlink_session_state;
static struct wchlink_command_context wchlink_session_commands = {
    .target = &wchlink_session_state.target,
    .transfer = &wchlink_session_state.transfer,
};

void wchlink_session_reset(void) {
    // 失败连接也会配置调试引脚，所有会话复位都必须释放总线
    wchlink_command_reset(&wchlink_session_commands);
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
    return wchlink_wire_data_reply(data, capacity, status);
}

void wchlink_session_submit_data_out(const uint8_t *data, size_t length) {
    wchlink_transfer_write_data(&wchlink_session_state.transfer, data, length);
}

struct wchlink_session_command_result wchlink_session_process(
    const uint8_t *request, size_t request_length, uint8_t *response,
    size_t response_capacity) {
    return wchlink_command_process(&wchlink_session_commands, request,
                                   request_length, response, response_capacity);
}
