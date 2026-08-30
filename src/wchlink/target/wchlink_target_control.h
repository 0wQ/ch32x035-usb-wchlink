#pragma once

#include "wchlink/target/rvswd_target_info.h"
#include "wchlink/target/rvswd_target_result.h"

#include <stdbool.h>
#include <stdint.h>

struct wchlink_target_ports;

struct rvswd_target_chip_info {
    bool ch5xx;
    uint32_t flash_size;
    uint32_t uid_low;
    uint32_t uid_high;
    uint32_t uid_tail;
    uint32_t chip_id;
};

// 芯片信息查询按值返回数据和失败诊断，不向 command 泄漏 ESIG 地址
struct rvswd_target_chip_info_result {
    struct rvswd_target_result result;
    struct rvswd_target_chip_info info;
};

// control port 只提供目标身份和连接生命周期操作，不保存独立状态
void wchlink_target_ports_init(struct wchlink_target_ports *ports);
void wchlink_target_ports_disconnect(struct wchlink_target_ports *ports);
bool wchlink_target_ports_is_connected(
    const struct wchlink_target_ports *ports);
bool wchlink_target_ports_uses_ch5xx_loader(
    const struct wchlink_target_ports *ports);
struct rvswd_target_result wchlink_target_ports_connect(
    struct wchlink_target_ports *ports);
void wchlink_target_ports_set_family_hint(
    struct wchlink_target_ports *ports, uint8_t family);
// 主机 SetSpeed 速度码：Low=0x03 使用慢时序，其余使用快时序
void wchlink_target_ports_set_speed(struct wchlink_target_ports *ports,
                                    uint8_t speed);
// context 在调用期间必须有效，info 以单次只读快照返回
struct rvswd_target_info wchlink_target_ports_info(
    const struct wchlink_target_ports *ports);
struct rvswd_target_chip_info_result wchlink_target_ports_read_chip_info(
    struct wchlink_target_ports *ports);

struct rvswd_target_result wchlink_target_ports_reset_and_halt(
    struct wchlink_target_ports *ports);
struct rvswd_target_result wchlink_target_ports_soft_reset_and_run(
    struct wchlink_target_ports *ports);
struct rvswd_target_result wchlink_target_ports_reset_and_run(
    struct wchlink_target_ports *ports);
