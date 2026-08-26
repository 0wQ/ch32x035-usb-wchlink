#pragma once

#include "wchlink/target/wchlink_target_ports.h"
#include "wchlink/transport/rvswd_transport.h"

// 具体 target 状态只对 target 实现和拥有该存储的 session 可见
struct wchlink_target_ports {
    struct rvswd_transport transport;
    struct rvswd_target_info info;
    uint8_t family_hint;
    uint8_t connect_error;
    bool family_hint_active;
};
