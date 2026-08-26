#include "wchlink/flash/rvswd_flash.h"

#include "wchlink/flash/rvswd_flash_ch32.h"
#include "wchlink/flash/rvswd_flash_ch5xx.h"
#include "wchlink/rvswd/rvswd_operation.h"
#include "wchlink/rvswd/rvswd_types.h"

#include <stddef.h>

bool rvswd_flash_erase_all(struct rvswd_operation *operation,
                           const struct rvswd_target_profile *profile) {
    operation->flash_code = 0u;
    if (profile == NULL) {
        operation->flash_code = 0x0fu;
        return false;
    }
    // profile 已由 target session 锁定，此处只按 Flash 协议选择 backend
    if (profile->ch5xx_protocol) {
        return rvswd_flash_ch5xx_erase_all(operation, profile);
    }
    return rvswd_flash_ch32_erase_all(operation, profile);
}
