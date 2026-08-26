#pragma once

#include <stdbool.h>
#include <stdint.h>

// 结果域标识失败发生在哪一层，wire 层不直接解释底层寄存器状态
enum rvswd_target_result_domain {
    RVSWD_TARGET_RESULT_NONE,
    RVSWD_TARGET_RESULT_CONNECT,
    RVSWD_TARGET_RESULT_DMI,
    RVSWD_TARGET_RESULT_MEMORY,
    RVSWD_TARGET_RESULT_DEBUG,
    RVSWD_TARGET_RESULT_FLASH,
    RVSWD_TARGET_RESULT_RESET,
};

// code 保留底层诊断值，调用者通过 domain 区分其来源
struct rvswd_target_result {
    bool ok;
    enum rvswd_target_result_domain domain;
    uint32_t code;
    bool retryable;
    uint32_t address;
    uint8_t dmi_status;
    uint32_t abstractcs;
    uint32_t value;
};

struct rvswd_target_result rvswd_target_result_success(void);
struct rvswd_target_result rvswd_target_result_failure(
    enum rvswd_target_result_domain domain, uint32_t code, bool retryable);
