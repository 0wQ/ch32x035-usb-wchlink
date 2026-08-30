#pragma once

#include "wchlink/target/rvswd_target_result.h"

#include <stdbool.h>
#include <stdint.h>

struct wchlink_target_ports;

enum wchlink_target_loader_operation {
    WCHLINK_TARGET_LOADER_INITIALIZE,
    WCHLINK_TARGET_LOADER_INITIALIZE_PREPARED,
    WCHLINK_TARGET_LOADER_PROGRAM,
    WCHLINK_TARGET_LOADER_VERIFY,
    WCHLINK_TARGET_LOADER_PROGRAM_VERIFY,
};

// loader 请求使用操作语义，target ports 负责映射族内 mode 和 mailbox
struct rvswd_target_loader_execute {
    enum wchlink_target_loader_operation operation;
    uint32_t address;
    uint32_t length;
    uint32_t checksum;
};

struct wchlink_target_loader_start {
    uint32_t download_limit;
    bool variable_length;
};

// transfer port 按 loader 语义映射目标 RAM，调用者不接触布局和 abstract command
bool wchlink_target_ports_loader_start(
    struct wchlink_target_ports *ports,
    struct wchlink_target_loader_start *start);
bool wchlink_target_ports_loader_supports_partial_write(
    const struct wchlink_target_ports *ports);
bool wchlink_target_ports_loader_uses_streaming(
    const struct wchlink_target_ports *ports);
bool wchlink_target_ports_loader_repeats_initialize(
    const struct wchlink_target_ports *ports);
uint32_t wchlink_target_ports_loader_data_length(
    const struct wchlink_target_ports *ports, uint32_t length);
bool wchlink_target_ports_loader_flash_range_valid(
    const struct wchlink_target_ports *ports, uint32_t address,
    uint32_t length);
struct rvswd_target_result wchlink_target_ports_read_memory32(
    struct wchlink_target_ports *ports, uint32_t address);
struct rvswd_target_result wchlink_target_ports_write_loader_code(
    struct wchlink_target_ports *ports, uint32_t offset,
    const uint8_t *data, uint32_t length);
struct rvswd_target_result wchlink_target_ports_write_loader_data(
    struct wchlink_target_ports *ports, uint32_t offset,
    const uint8_t *data, uint32_t length);
struct rvswd_target_result wchlink_target_ports_execute_loader(
    struct wchlink_target_ports *ports,
    const struct rvswd_target_loader_execute *request);
