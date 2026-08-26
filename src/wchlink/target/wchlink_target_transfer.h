#pragma once

#include "wchlink/target/rvswd_target_result.h"

#include <stdint.h>

struct wchlink_target_ports;

// transfer port 封装 memory 和 execute，调用者不接触 abstract command 与 DMI 地址
struct rvswd_target_result wchlink_target_ports_read_memory32(
    struct wchlink_target_ports *ports, uint32_t address);
struct rvswd_target_result wchlink_target_ports_write_memory32(
    struct wchlink_target_ports *ports, uint32_t address, uint32_t value);
struct rvswd_target_result wchlink_target_ports_write_memory(
    struct wchlink_target_ports *ports, uint32_t address,
    const uint8_t *data, uint32_t length);
struct rvswd_target_result wchlink_target_ports_execute(
    struct wchlink_target_ports *ports, uint32_t entry, uint32_t stack_top,
    uint32_t mode, uint32_t address, uint32_t length, uint32_t data_address);
