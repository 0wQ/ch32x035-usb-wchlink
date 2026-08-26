#pragma once

#include "rvswd_types.h"

#include <stdbool.h>
#include <stdint.h>

// 每个 target session 独占一个 transport，packet mode、诊断和 GPIO timing 不跨会话共享
struct rvswd_transport {
    enum rvswd_packet_mode packet_mode;
    uint8_t last_status;
    bool failure_retryable;
    bool fast_timing;
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

// 结果查询只服务迁移中的 bool backend，backend 改为按值返回结果后删除
uint8_t rvswd_transport_last_status(
    const struct rvswd_transport *transport);
bool rvswd_transport_failure_retryable(
    const struct rvswd_transport *transport);

bool rvswd_transport_read(struct rvswd_transport *transport, uint8_t address,
                          uint32_t *value);
bool rvswd_transport_write(struct rvswd_transport *transport, uint8_t address,
                           uint32_t value);
bool rvswd_transport_probe_long(struct rvswd_transport *transport,
                                uint8_t operation, uint8_t address,
                                uint32_t value, uint8_t host_parity,
                                uint8_t *target_address, uint32_t *result,
                                uint8_t *status);
