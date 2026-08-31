#pragma once

#include "wchlink/target/rvswd_target_info.h"
#include "wchlink/transport/rvswd_transport.h"

struct rvswd_target_profile;
struct rvswd_target_module;

// 具体 target 状态只对 target 实现和拥有该存储的 session 可见
struct wchlink_target_ports {
    struct rvswd_transport transport;
    struct rvswd_target_info info;
    const struct rvswd_target_module *module;
    uint8_t family_hint;
    uint8_t connect_error;
    // 主机 SetSpeed 请求值，0 表示尚未请求，connect 重新配置时序后需要恢复
    uint8_t requested_speed;
    bool family_hint_active;
};

void wchlink_target_ports_refresh_info(struct wchlink_target_ports *ports);
