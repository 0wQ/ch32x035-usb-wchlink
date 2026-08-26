#pragma once

#include "wchlink/target/rvswd_target_result.h"

#include <stdbool.h>
#include <stdint.h>

struct wchlink_target_ports;

struct rvswd_target_loader_execute {
    uint32_t mode;
    uint32_t address;
    uint32_t length;
    uint32_t checksum;
    bool write_checksum;
};

// transfer port 按 loader 语义映射目标 RAM，调用者不接触布局和 abstract command
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
