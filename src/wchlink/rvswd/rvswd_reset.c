#include "wchlink/rvswd/rvswd_reset.h"

#include "bsp/bsp_delay.h"
#include "wchlink/rvswd/rvswd_debug.h"

static const uint32_t rvswd_reset_release_delay_us = 1000u;

bool rvswd_reset_and_halt(struct rvswd_operation *operation) {
    // ndmreset 保持 Debug Module 工作，释放后重新停住目标核
    if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_CONTROL,
                                   0x80000003u)
             .ok) {
        return false;
    }
    bsp_delay_us(rvswd_reset_release_delay_us);
    return rvswd_operation_write_dmi(operation, RVSWD_DMI_CONTROL,
                                     0x80000001u)
               .ok &&
           rvswd_debug_wait_dmstatus(operation, 1u << 9u, true, 100u);
}

bool rvswd_soft_reset_and_run(struct rvswd_operation *operation) {
    // 软复位后显式发送 resumereq，确保目标从复位向量继续运行
    if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_CONTROL,
                                   0x00000003u)
             .ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_CONTROL,
                                   0x00000001u)
             .ok) {
        return false;
    }
    bsp_delay_us(rvswd_reset_release_delay_us);
    return rvswd_operation_write_dmi(operation, RVSWD_DMI_CONTROL,
                                     0x40000001u)
        .ok;
}

bool rvswd_reset_and_run(struct rvswd_operation *operation) {
    // 不设置 haltreq，释放 ndmreset 后让目标从复位向量继续运行
    if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_CONTROL,
                                   0x00000003u)
             .ok) {
        return false;
    }
    bsp_delay_us(rvswd_reset_release_delay_us);
    return rvswd_operation_write_dmi(operation, RVSWD_DMI_CONTROL,
                                     0x00000001u)
        .ok;
}
