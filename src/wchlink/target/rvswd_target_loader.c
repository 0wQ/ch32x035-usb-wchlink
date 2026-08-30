#include "wchlink/target/rvswd_target_loader.h"

#include "wchlink/rvswd/rvswd_debug.h"
#include "wchlink/transport/rvswd_transport.h"

#include <stddef.h>

static const uint32_t rvswd_target_loader_execute_timeout_ms = 5000u;

bool rvswd_target_loader_execute_x03x(
    struct rvswd_operation *operation, uint32_t entry, uint32_t stack_top,
    uint32_t mode, uint32_t address, uint32_t length, uint32_t *result) {
    // 官方 LinkE 在 loader 初始化前配置 DCSR，使 RAM loader 的 ebreak 进入调试停机
    if (((mode & 1u) != 0u && !rvswd_debug_write_register(operation, 0x7b0u, 0x000090c3u)) ||
        ((mode & 1u) == 0u && !rvswd_debug_write_register(operation, 0x300u, 0u)) ||
        !rvswd_debug_write_raw_gpr(operation, 10u, mode) ||
        !rvswd_debug_write_raw_gpr(operation, 11u, address) ||
        !rvswd_debug_write_raw_gpr(operation, 12u, length) ||
        !rvswd_debug_write_raw_gpr(operation, 2u, stack_top) ||
        !rvswd_debug_write_register(operation, 0x7b1u, entry)) {
        if (result != NULL) {
            *result = 0xe101u;
        }
        return false;
    }
    if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_CONTROL, 0x40000001u).ok ||
        !rvswd_debug_wait_dmstatus(operation, 1u << 9u, true, rvswd_target_loader_execute_timeout_ms)) {
        if (result != NULL) {
            *result = 0xe102u;
        }
        return false;
    }
    // 保持 dmactive，loader 返回后的 SRAM 和 checksum abstract 访问仍沿用当前会话
    if (result != NULL && !rvswd_debug_read_raw_gpr(operation, 10u, result)) {
        *result = 0xe103u;
        return false;
    }
    return true;
}
