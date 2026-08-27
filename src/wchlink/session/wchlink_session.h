#pragma once

#include <stddef.h>
#include <stdint.h>

enum wchlink_session_data_io {
    WCHLINK_SESSION_DATA_IO_NONE = 0u,
    WCHLINK_SESSION_DATA_IO_IN = 1u << 0u,
    WCHLINK_SESSION_DATA_IO_OUT = 1u << 1u,
};

enum wchlink_session_command_status {
    WCHLINK_SESSION_COMMAND_COMPLETED,
    WCHLINK_SESSION_COMMAND_MALFORMED,
    WCHLINK_SESSION_COMMAND_TARGET_FAILED,
    WCHLINK_SESSION_COMMAND_BUSY,
    WCHLINK_SESSION_COMMAND_NO_RESPONSE,
};

enum wchlink_session_action {
    WCHLINK_SESSION_ACTION_NONE,
    WCHLINK_SESSION_ACTION_ENTER_ISP,
};

// Session 返回回复生命周期语义，USB adapter 负责映射为端点超时
enum wchlink_session_response_policy {
    WCHLINK_SESSION_RESPONSE_STANDARD,
    WCHLINK_SESSION_RESPONSE_SESSION_END,
};

// Command result 按值交给 USB 主循环，session 不保留 response buffer 或一次性 action
struct wchlink_session_command_result {
    enum wchlink_session_command_status status;
    enum wchlink_session_action action;
    enum wchlink_session_response_policy response_policy;
    size_t response_length;
};

void wchlink_session_reset(void);
// USB host 放弃 data 阶段后取消旧操作，保留已经建立的目标连接
void wchlink_session_abort_transfer(void);
struct wchlink_session_command_result wchlink_session_process(
    const uint8_t *request, size_t request_length, uint8_t *response,
    size_t response_capacity);
// USB 主循环按该方向挂载一次端点，callback 只负责复制数据和设置 pending
enum wchlink_session_data_io wchlink_session_next_data_io(void);
size_t wchlink_session_poll_data_in(uint8_t *data, size_t capacity);
void wchlink_session_submit_data_out(const uint8_t *data, size_t length);
