#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum wchlink_session_data_io {
    WCHLINK_SESSION_DATA_IO_NONE = 0u,
    WCHLINK_SESSION_DATA_IO_IN = 1u << 0u,
    WCHLINK_SESSION_DATA_IO_OUT = 1u << 1u,
};

void wchlink_session_reset(void);
size_t wchlink_session_process(const uint8_t *request, size_t request_length, uint8_t *response, size_t response_capacity);
bool wchlink_session_take_isp_request(void);
// USB 主循环按该方向挂载一次端点，callback 只负责复制数据和设置 pending
enum wchlink_session_data_io wchlink_session_next_data_io(void);
size_t wchlink_session_poll_data_in(uint8_t *data, size_t capacity);
void wchlink_session_submit_data_out(const uint8_t *data, size_t length);
