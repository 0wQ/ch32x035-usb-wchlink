#pragma once

#include "wchlink/rvswd/rvswd_types.h"

#include <stdbool.h>
#include <stdint.h>

// target info 是按值返回的只读快照，不泄漏锁定 profile 和 transport 状态
struct rvswd_target_info {
    uint32_t chip_id;                     // 目标芯片完整 ChipID
    uint8_t family;                       // WCH-Link 协议使用的目标族编号
    uint32_t loader_download_limit;       // loader 代码允许下载的最大长度
    uint32_t loader_data_page_size;       // loader 数据缓冲区的页对齐粒度
    uint32_t loader_initialize_mode;      // loader 首次初始化使用的执行 mode
    uint32_t loader_prepared_mode;        // Prepare 完成后再次初始化使用的执行 mode
    uint32_t loader_program_mode;         // 仅编程操作使用的执行 mode
    uint32_t loader_verify_mode;          // 仅校验操作使用的执行 mode
    uint32_t loader_program_verify_mode;  // 编程并校验操作使用的执行 mode
    uint32_t loader_checksum_mode_mask;   // mode 中表示需要写入 checksum 的位掩码
    uint32_t loader_length_mode_mask;     // mode 中表示需要写入长度 mailbox 的位掩码
    bool loader_repeat_initialize;        // loader 首次执行成功后是否需要再次初始化
    bool partial_write_supported;         // 是否支持目标页的局部写入流程
    bool connected;                       // 目标调试连接是否已经建立
    bool memory_streaming;                // 是否支持连续内存写入优化路径
    bool loader_variable_length;          // loader 数据长度是否由结束命令确定
};
