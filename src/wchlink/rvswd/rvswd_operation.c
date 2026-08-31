#include "wchlink/rvswd/rvswd_operation.h"

#include <stddef.h>

void rvswd_operation_init(struct rvswd_operation *operation, struct rvswd_transport *transport) {
    if (operation == NULL) {
        return;
    }
    *operation = (struct rvswd_operation){
        .transport = transport,
    };
}

static struct rvswd_transport_result rvswd_operation_capture(
    struct rvswd_operation *operation,
    struct rvswd_transport_result result) {
    if (operation != NULL) {
        // 每笔事务都更新诊断，逻辑错误因此对应最后一笔已完成的 DMI 状态
        operation->dmi_status = result.status;
        operation->retryable = !result.ok && result.retryable;
    }
    return result;
}

struct rvswd_transport_result rvswd_operation_read_dmi(
    struct rvswd_operation *operation, uint8_t address) {
    if (operation == NULL || operation->transport == NULL) {
        return (struct rvswd_transport_result){0};
    }
    return rvswd_operation_capture(
        operation, rvswd_transport_read(operation->transport, address));
}

struct rvswd_transport_result rvswd_operation_write_dmi(
    struct rvswd_operation *operation, uint8_t address, uint32_t value) {
    if (operation == NULL || operation->transport == NULL) {
        return (struct rvswd_transport_result){0};
    }
    return rvswd_operation_capture(
        operation, rvswd_transport_write(operation->transport, address, value));
}

void rvswd_operation_cleanup_write_dmi(struct rvswd_operation *operation, uint8_t address, uint32_t value) {
    if (operation == NULL || operation->transport == NULL) {
        return;
    }
    // 清理事务不能覆盖触发清理的原始失败诊断
    (void)rvswd_transport_write(operation->transport, address, value);
}
