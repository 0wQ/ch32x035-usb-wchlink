#include "wchlink/target/rvswd_target_result.h"

struct rvswd_target_result rvswd_target_result_success(void) {
    struct rvswd_target_result result = {
        .ok = true,
        .domain = RVSWD_TARGET_RESULT_NONE,
    };

    return result;
}

struct rvswd_target_result rvswd_target_result_failure(
    enum rvswd_target_result_domain domain, uint32_t code, bool retryable) {
    struct rvswd_target_result result = {
        .ok = false,
        .domain = domain,
        .code = code,
        .retryable = retryable,
    };

    return result;
}
