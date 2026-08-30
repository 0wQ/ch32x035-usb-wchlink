#pragma once

#include "wchlink/session/wchlink_transfer.h"

// C 文件作用域数组需要 integer constant expression，宏只描述 transfer 私有存储容量
#define WCHLINK_TRANSFER_PACKET_CAPACITY 256u
#define WCHLINK_TRANSFER_CHUNK_CAPACITY  4096u

// data IN 和 data OUT 是独立端点，两个方向允许同时处于活动状态
enum wchlink_transfer_in_state {
    WCHLINK_TRANSFER_IN_IDLE,
    WCHLINK_TRANSFER_IN_READING,
};

enum wchlink_transfer_out_state {
    WCHLINK_TRANSFER_OUT_IDLE,
    WCHLINK_TRANSFER_OUT_LOADER,
    WCHLINK_TRANSFER_OUT_FLASH,
    WCHLINK_TRANSFER_OUT_PARTIAL_WRITE,
};

// 该存储由 wchlink_session 独占，其他模块只能通过 transfer 接口改变状态
struct wchlink_transfer {
    struct wchlink_target_ports *target;
    enum wchlink_transfer_in_state in_state;
    enum wchlink_transfer_out_state out_state;
    uint32_t read_address;
    uint32_t read_remaining;
    uint32_t write_address;
    uint32_t write_remaining;
    uint32_t loader_received;
    uint32_t loader_expected;
    bool loader_variable_length;
    uint8_t loader_error;
    uint32_t flash_data_received;
    uint32_t flash_transfer_received;
    uint32_t flash_transfer_length;
    uint32_t flash_chunk_length;
    uint8_t flash_loader_operation;
    uint32_t flash_checksum;
    bool loader_ready;
    bool flash_openocd_mode;
    bool flash_prepare_seen;
    uint32_t partial_write_address;
    uint8_t partial_write_length;
    uint8_t partial_write_data[WCHLINK_TRANSFER_PACKET_CAPACITY];
    uint8_t partial_write_page[WCHLINK_TRANSFER_PACKET_CAPACITY];
    uint8_t partial_cache[WCHLINK_TRANSFER_CHUNK_CAPACITY];
    uint8_t flash_chunk_data[WCHLINK_TRANSFER_CHUNK_CAPACITY];
    bool partial_cache_valid;
    bool data_reply_pending;
    uint8_t data_reply_status;
    uint8_t loader_failure_dmi_status;
    uint32_t loader_failure_address;
    uint32_t loader_failure_abstractcs;
    uint8_t flash_padding[WCHLINK_TRANSFER_PACKET_CAPACITY];
};
