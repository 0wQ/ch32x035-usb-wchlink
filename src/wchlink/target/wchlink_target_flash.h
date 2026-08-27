#pragma once

#include "wchlink/target/rvswd_target_result.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct wchlink_target_ports;

// Flash port 选择已锁定 profile 对应的 backend，只返回结构化 target result
struct rvswd_target_result wchlink_target_ports_flash_erase_all(
    struct wchlink_target_ports *ports);
struct rvswd_target_result wchlink_target_ports_flash_rewrite_page(
    struct wchlink_target_ports *ports, uint32_t address,
    const uint8_t *data);
struct rvswd_target_result wchlink_target_ports_flash_read_protected(
    struct wchlink_target_ports *ports);
struct rvswd_target_result wchlink_target_ports_flash_write_protected(
    struct wchlink_target_ports *ports);
struct rvswd_target_result wchlink_target_ports_flash_set_read_protected(
    struct wchlink_target_ports *ports, bool protected);
struct rvswd_target_result wchlink_target_ports_flash_set_option_bytes(
    struct wchlink_target_ports *ports, const uint8_t *values, size_t count);
