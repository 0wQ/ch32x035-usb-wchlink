#pragma once

#include "rvswd_transport.h"

#include <stdbool.h>
#include <stdint.h>

// operation 只存在于一次 target 调用栈，集中保存该次操作的失败诊断
struct rvswd_operation {
    struct rvswd_transport *transport;
    // memory 与 Flash 分别拥有阶段码，嵌套调用不能互相覆盖
    uint32_t memory_code;
    uint32_t flash_code;
    uint32_t address;
    uint32_t abstractcs;
    uint8_t dmi_status;
    bool retryable;
};

void rvswd_operation_init(struct rvswd_operation *operation,
                          struct rvswd_transport *transport);
struct rvswd_transport_result rvswd_operation_read_dmi(
    struct rvswd_operation *operation, uint8_t address);
struct rvswd_transport_result rvswd_operation_write_dmi(
    struct rvswd_operation *operation, uint8_t address, uint32_t value);
void rvswd_operation_cleanup_write_dmi(struct rvswd_operation *operation,
                                       uint8_t address, uint32_t value);
