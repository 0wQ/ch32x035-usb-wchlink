#pragma once

#include "wchlink/protocol/wchlink_wire.h"
#include "wchlink/target/rvswd_target_result.h"
#include "wchlink/target/wchlink_target_ports.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum wchlink_transfer_mode {
    WCHLINK_TRANSFER_IDLE,
    WCHLINK_TRANSFER_LOADER,
    WCHLINK_TRANSFER_FLASH,
    WCHLINK_TRANSFER_PARTIAL_WRITE,
};

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

// 该结构由 wchlink_session 独占，USB callback 不持有其中任何 buffer
struct wchlink_transfer {
    struct wchlink_target_ports *target;
    uint32_t read_address;
    uint32_t read_remaining;
    bool read_active;
    uint32_t write_address;
    uint32_t write_remaining;
    enum wchlink_transfer_mode write_mode;
    uint32_t loader_received;
    uint32_t loader_expected;
    bool loader_variable_length;
    uint8_t loader_error;
    uint32_t flash_data_received;
    uint32_t flash_transfer_received;
    uint32_t flash_transfer_length;
    uint32_t flash_chunk_length;
    uint32_t flash_loader_mode;
    uint32_t flash_checksum;
    bool loader_ready;
    bool flash_openocd_mode;
    bool flash_prepare_seen;
    uint32_t partial_write_address;
    uint8_t partial_write_length;
    uint8_t partial_write_data[WCHLINK_FLASH_PACKET_SIZE];
    uint8_t partial_write_page[WCHLINK_FLASH_PACKET_SIZE];
    uint8_t partial_cache[WCHLINK_FLASH_CHUNK_SIZE];
    uint8_t flash_chunk_data[WCHLINK_FLASH_CHUNK_SIZE];
    bool partial_cache_valid;
    bool data_reply_pending;
    uint8_t data_reply_status;
    uint8_t loader_failure_dmi_status;
    uint32_t loader_failure_address;
    uint32_t loader_failure_abstractcs;
    uint8_t flash_padding[WCHLINK_FLASH_PACKET_SIZE];
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
