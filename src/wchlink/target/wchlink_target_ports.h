#pragma once

#include "rvswd_target_info.h"
#include "rvswd_target_result.h"
#include "rvswd_transport.h"

#include <stdbool.h>
#include <stdint.h>

// target ports 是 session 与 RVSWD target 深模块之间的唯一内部 seam
// transport、目标身份和连接状态归该模块所有，调用者只接收本次操作结果
struct wchlink_target_ports {
    struct rvswd_transport transport;
    struct rvswd_target_info info;
    uint8_t family_hint;
    uint8_t connect_error;
    bool family_hint_active;
};

void wchlink_target_ports_init(struct wchlink_target_ports *ports);
void wchlink_target_ports_disconnect(struct wchlink_target_ports *ports);
struct rvswd_target_result wchlink_target_ports_connect(
    struct wchlink_target_ports *ports);
void wchlink_target_ports_set_family_hint(
    struct wchlink_target_ports *ports, uint8_t family);
bool wchlink_target_ports_is_connected(
    const struct wchlink_target_ports *ports);
const struct rvswd_target_info *wchlink_target_ports_info(
    const struct wchlink_target_ports *ports);
uint8_t wchlink_target_ports_family(
    const struct wchlink_target_ports *ports);
uint32_t wchlink_target_ports_chip_id(
    const struct wchlink_target_ports *ports);
bool wchlink_target_ports_uses_ch5xx_loader(
    const struct wchlink_target_ports *ports);
bool wchlink_target_ports_uses_l103_loader(
    const struct wchlink_target_ports *ports);
bool wchlink_target_ports_supports_memory_streaming(
    const struct wchlink_target_ports *ports);

struct rvswd_target_result wchlink_target_ports_read_dmi(
    struct wchlink_target_ports *ports, uint8_t address);
struct rvswd_target_result wchlink_target_ports_write_dmi(
    struct wchlink_target_ports *ports, uint8_t address, uint32_t value);
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
struct rvswd_target_result wchlink_target_ports_reset_and_halt(
    struct wchlink_target_ports *ports);
struct rvswd_target_result wchlink_target_ports_soft_reset_and_run(
    struct wchlink_target_ports *ports);
struct rvswd_target_result wchlink_target_ports_reset_and_run(
    struct wchlink_target_ports *ports);
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
