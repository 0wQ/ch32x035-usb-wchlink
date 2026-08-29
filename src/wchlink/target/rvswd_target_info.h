#pragma once

#include "wchlink/rvswd/rvswd_types.h"

#include <stdbool.h>
#include <stdint.h>

// target info 是按值返回的只读快照，不泄漏锁定 profile 和 transport 状态
struct rvswd_target_info {
    uint32_t chip_id;
    uint8_t family;
    enum rvswd_target_loader loader;
    uint32_t loader_download_limit;
    uint32_t loader_data_page_size;
    uint32_t code_flash_size;
    bool connected;
    bool memory_streaming;
    bool loader_variable_length;
};
