#pragma once

#include "rvswd_types.h"

#include <stdbool.h>
#include <stdint.h>

// 每个 target session 独占一个 transport，packet mode 和 GPIO timing 不跨会话共享
struct rvswd_transport {
    enum rvswd_packet_mode packet_mode;
    // bool backend 迁移完成前保留最后一次诊断，target_dmi 已直接使用按值结果
    uint8_t last_status;
    bool failure_retryable;
    bool fast_timing;
};

// raw long probe 只描述一次物理帧采样，不应用 DMI 重试和状态分类
struct rvswd_transport_probe_result {
    uint32_t value;
    uint8_t address;
    uint8_t status;
};

// DMI 结果与一次 read/write 调用绑定，失败诊断不会被后续事务覆盖
struct rvswd_transport_result {
    uint32_t value;
    uint8_t status;
    bool ok;
    bool retryable;
};

void rvswd_transport_init(struct rvswd_transport *transport);
void rvswd_transport_disconnect(struct rvswd_transport *transport);
void rvswd_transport_set_packet_mode(struct rvswd_transport *transport,
                                     enum rvswd_packet_mode mode);
enum rvswd_packet_mode rvswd_transport_packet_mode(
    const struct rvswd_transport *transport);
void rvswd_transport_set_fast_timing(struct rvswd_transport *transport,
                                     bool enabled);
void rvswd_transport_wakeup(struct rvswd_transport *transport,
                            bool stop_condition);
uint8_t rvswd_transport_last_status(
    const struct rvswd_transport *transport);
bool rvswd_transport_failure_retryable(
    const struct rvswd_transport *transport);

struct rvswd_transport_result rvswd_transport_read(
    struct rvswd_transport *transport, uint8_t address);
struct rvswd_transport_result rvswd_transport_write(
    struct rvswd_transport *transport, uint8_t address, uint32_t value);
struct rvswd_transport_probe_result rvswd_transport_probe_long(
    struct rvswd_transport *transport, uint8_t operation, uint8_t address,
    uint32_t value, uint8_t host_parity);
