#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct wchlink_target_ports;
struct wchlink_transfer;

// USB 适配层只根据该方向挂载端点，不读取 transfer 内部状态
enum wchlink_transfer_io_request {
    WCHLINK_TRANSFER_IO_NONE = 0u,
    WCHLINK_TRANSFER_IO_DATA_IN = 1u << 0u,
    WCHLINK_TRANSFER_IO_DATA_OUT = 1u << 1u,
};

enum wchlink_transfer_finish_status {
    WCHLINK_TRANSFER_FINISH_READY,
    WCHLINK_TRANSFER_FINISH_INCOMPLETE,
    WCHLINK_TRANSFER_FINISH_LOADER_ERROR,
    WCHLINK_TRANSFER_FINISH_TARGET_ERROR,
};

// loader 结束结果按值携带 wire 编码所需诊断，消费后不再依赖 transfer 状态
struct wchlink_transfer_finish_result {
    enum wchlink_transfer_finish_status status;
    uint8_t loader_error;
    uint8_t dmi_status;
    uint32_t address;
    uint32_t abstractcs;
    uint32_t target_value;
};

void wchlink_transfer_init(struct wchlink_transfer *transfer,
                           struct wchlink_target_ports *target);
void wchlink_transfer_bind_target(struct wchlink_transfer *transfer,
                                  struct wchlink_target_ports *target);
void wchlink_transfer_reset(struct wchlink_transfer *transfer);
void wchlink_transfer_clear_operation(struct wchlink_transfer *transfer);
void wchlink_transfer_invalidate_cache(struct wchlink_transfer *transfer);

void wchlink_transfer_prepare_write(struct wchlink_transfer *transfer,
                                    uint32_t address, uint32_t length);
void wchlink_transfer_prepare_read(struct wchlink_transfer *transfer,
                                   uint32_t address, uint32_t length);
bool wchlink_transfer_start_partial_write(struct wchlink_transfer *transfer,
                                          uint32_t address, uint8_t length);
bool wchlink_transfer_start_loader(struct wchlink_transfer *transfer);
void wchlink_transfer_mark_flash_prepare(struct wchlink_transfer *transfer);
struct wchlink_transfer_finish_result wchlink_transfer_finish_loader(
    struct wchlink_transfer *transfer, uint8_t command);
bool wchlink_transfer_start_flash(struct wchlink_transfer *transfer,
                                  uint8_t command);
void wchlink_transfer_abort(struct wchlink_transfer *transfer);
void wchlink_transfer_begin_read(struct wchlink_transfer *transfer);

enum wchlink_transfer_io_request wchlink_transfer_next_io(
    const struct wchlink_transfer *transfer);
void wchlink_transfer_write_data(struct wchlink_transfer *transfer,
                                 const uint8_t *data, size_t length);
bool wchlink_transfer_take_reply_status(struct wchlink_transfer *transfer,
                                        uint8_t *status);
size_t wchlink_transfer_read_data(struct wchlink_transfer *transfer,
                                  uint8_t *data, size_t capacity);
