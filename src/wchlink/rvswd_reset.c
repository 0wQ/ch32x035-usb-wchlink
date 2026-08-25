#include "rvswd_reset.h"

#include "bsp/bsp_delay.h"
#include "rvswd_debug.h"
#include "rvswd_dmi.h"

#define RVSWD_DMI_CONTROL 0x10u
#define RVSWD_RESUME_MIN_DELAY_US 1000u

bool rvswd_reset_and_halt(void) {
    // ndmreset 保持 Debug Module 工作，释放后重新停住目标核
    if (!rvswd_dmi_write(RVSWD_DMI_CONTROL, 0x80000003u)) {
        return false;
    }
    bsp_delay_us(1000u);
    return rvswd_dmi_write(RVSWD_DMI_CONTROL, 0x80000001u) &&
           rvswd_debug_wait_dmstatus(1u << 9u, true, 100u);
}

bool rvswd_soft_reset_and_run(void) {
    // 软复位后显式发送 resumereq，确保目标从复位向量继续运行
    if (!rvswd_dmi_write(RVSWD_DMI_CONTROL, 0x00000003u) ||
        !rvswd_dmi_write(RVSWD_DMI_CONTROL, 0x00000001u)) {
        return false;
    }
    bsp_delay_us(RVSWD_RESUME_MIN_DELAY_US);
    return rvswd_dmi_write(RVSWD_DMI_CONTROL, 0x40000001u);
}

bool rvswd_reset_and_run(void) {
    // 不设置 haltreq，释放 ndmreset 后让目标从复位向量继续运行
    if (!rvswd_dmi_write(RVSWD_DMI_CONTROL, 0x00000003u)) {
        return false;
    }
    bsp_delay_us(1000u);
    return rvswd_dmi_write(RVSWD_DMI_CONTROL, 0x00000001u);
}
