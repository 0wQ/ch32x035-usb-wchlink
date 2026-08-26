#pragma once

#include "wchlink/target/rvswd_target_info.h"
#include "wchlink/target/rvswd_target_result.h"

#include <stdint.h>

struct wchlink_target_ports;

// control port 只提供目标身份和连接生命周期操作，不保存独立状态
void wchlink_target_ports_init(struct wchlink_target_ports *ports);
void wchlink_target_ports_disconnect(struct wchlink_target_ports *ports);
struct rvswd_target_result wchlink_target_ports_connect(
    struct wchlink_target_ports *ports);
void wchlink_target_ports_set_family_hint(
    struct wchlink_target_ports *ports, uint8_t family);
// context 在调用期间必须有效，info 以单次只读快照返回
struct rvswd_target_info wchlink_target_ports_info(
    const struct wchlink_target_ports *ports);

struct rvswd_target_result wchlink_target_ports_reset_and_halt(
    struct wchlink_target_ports *ports);
struct rvswd_target_result wchlink_target_ports_soft_reset_and_run(
    struct wchlink_target_ports *ports);
struct rvswd_target_result wchlink_target_ports_reset_and_run(
    struct wchlink_target_ports *ports);
