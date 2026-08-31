#include "wchlink/rvswd/rvswd_debug.h"

#include "bsp/bsp_delay.h"

#include <stddef.h>

static const uint32_t rvswd_debug_abstract_timeout_us = 10000u;
static const uint32_t rvswd_debug_resume_poll_interval_us = 10u;
static const uint32_t rvswd_debug_resume_timeout_us = 3000u;

bool rvswd_debug_wait_abstract_idle_timeout(struct rvswd_operation *operation, uint32_t *abstractcs, uint32_t timeout_us) {
    uint64_t start = bsp_time_us();

    do {
        struct rvswd_transport_result read_result = rvswd_operation_read_dmi(operation, RVSWD_DMI_ABSTRACTCS);

        if (!read_result.ok) {
            if (!read_result.retryable) {
                return false;
            }
            continue;
        }
        *abstractcs = read_result.value;
        operation->abstractcs = read_result.value;
        if ((*abstractcs & (1u << 12u)) == 0u) {
            return true;
        }
    } while ((bsp_time_us() - start) < timeout_us);

    return false;
}

bool rvswd_debug_wait_abstract_idle(struct rvswd_operation *operation, uint32_t *abstractcs) {
    return rvswd_debug_wait_abstract_idle_timeout(operation, abstractcs, rvswd_debug_abstract_timeout_us);
}

bool rvswd_debug_write_register(struct rvswd_operation *operation, uint16_t regno, uint32_t value) {
    struct rvswd_transport_result read_result;

    if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_DATA0, value).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_ABSTRACTCS, 0x00000700u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_COMMAND, 0x00230000u | (uint32_t)regno).ok) {
        return false;
    }
    read_result = rvswd_operation_read_dmi(operation, RVSWD_DMI_ABSTRACTCS);
    if (!read_result.ok) {
        return false;
    }
    return ((read_result.value >> 8u) & 0x07u) == 0u;
}

bool rvswd_debug_read_register(struct rvswd_operation *operation, uint16_t regno, uint32_t *value) {
    struct rvswd_transport_result read_result;

    if (value == NULL ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_ABSTRACTCS, 0x00000700u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_COMMAND, 0x00220000u | (uint32_t)regno).ok) {
        return false;
    }
    read_result = rvswd_operation_read_dmi(operation, RVSWD_DMI_ABSTRACTCS);
    if (!read_result.ok || ((read_result.value >> 8u) & 0x07u) != 0u) {
        return false;
    }
    read_result = rvswd_operation_read_dmi(operation, RVSWD_DMI_DATA0);
    if (!read_result.ok) {
        return false;
    }
    *value = read_result.value;
    return true;
}

bool rvswd_debug_write_raw_gpr(struct rvswd_operation *operation, uint8_t regno, uint32_t value) {
    struct rvswd_transport_result read_result;

    if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_DATA0, value).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_ABSTRACTCS, 0x00000700u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_COMMAND, 0x00231000u | (uint32_t)regno).ok) {
        return false;
    }
    read_result = rvswd_operation_read_dmi(operation, RVSWD_DMI_ABSTRACTCS);
    if (!read_result.ok) {
        return false;
    }
    return ((read_result.value >> 8u) & 0x07u) == 0u;
}

bool rvswd_debug_read_raw_gpr(struct rvswd_operation *operation, uint8_t regno, uint32_t *value) {
    struct rvswd_transport_result read_result;

    if (value == NULL ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_ABSTRACTCS, 0x00000700u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_COMMAND, 0x00221000u | (uint32_t)regno).ok) {
        return false;
    }
    read_result = rvswd_operation_read_dmi(operation, RVSWD_DMI_ABSTRACTCS);
    if (!read_result.ok || ((read_result.value >> 8u) & 0x07u) != 0u) {
        return false;
    }
    read_result = rvswd_operation_read_dmi(operation, RVSWD_DMI_DATA0);
    if (!read_result.ok) {
        return false;
    }
    *value = read_result.value;
    return true;
}

bool rvswd_debug_wait_dmstatus(struct rvswd_operation *operation, uint32_t mask, bool set, uint32_t timeout_ms) {
    uint64_t start = bsp_time_us();

    do {
        struct rvswd_transport_result read_result = rvswd_operation_read_dmi(operation, RVSWD_DMI_STATUS);

        if (!read_result.ok) {
            return false;
        }
        if (((read_result.value & mask) != 0u) == set) {
            return true;
        }
        bsp_delay_us(100u);
    } while ((bsp_time_us() - start) < (uint64_t)timeout_ms * 1000u);

    return false;
}

bool rvswd_debug_halt(struct rvswd_operation *operation) {
    return rvswd_operation_write_dmi(operation, RVSWD_DMI_CONTROL, 0x80000001u).ok &&
           rvswd_debug_wait_dmstatus(operation, 1u << 9u, true, 100u);
}

bool rvswd_debug_resume(struct rvswd_operation *operation, uint32_t dmcontrol, uint32_t *dmstatus) {
    const uint32_t idle_control = dmcontrol & ~RVSWD_DMCONTROL_RESUMEREQ;
    uint64_t start;

    if (operation == NULL || dmstatus == NULL ||
        (dmcontrol & (RVSWD_DMCONTROL_DMACTIVE | RVSWD_DMCONTROL_RESUMEREQ)) != (RVSWD_DMCONTROL_DMACTIVE | RVSWD_DMCONTROL_RESUMEREQ) ||
        (dmcontrol & ~RVSWD_DMCONTROL_RESUME_ALLOWED) != 0u) {
        return false;
    }

    // WCH OpenOCD 在每次 resume 前清除 DMOD，避免目标保留旧的 Debug Mode 状态
    if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_WCH_DMOD, 0u).ok) {
        return false;
    }
    if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_CONTROL, dmcontrol).ok) {
        // 写事务失败也可能已经到达目标，使用独立清理事务释放 resumereq
        rvswd_operation_cleanup_write_dmi(operation, RVSWD_DMI_CONTROL, idle_control);
        return false;
    }

    start = bsp_time_us();
    do {
        struct rvswd_transport_result read_result;

        bsp_delay_us(rvswd_debug_resume_poll_interval_us);
        read_result = rvswd_operation_read_dmi(operation, RVSWD_DMI_STATUS);

        if (read_result.ok && (read_result.value & RVSWD_DMSTATUS_RUNNING) == RVSWD_DMSTATUS_RUNNING) {
            if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_CONTROL, idle_control).ok) {
                rvswd_operation_cleanup_write_dmi(operation, RVSWD_DMI_CONTROL, idle_control);
                return false;
            }
            // WCH V30x 已 running 时仍可能不置 resumeack，仅在真实运行后补齐主机快照
            *dmstatus = read_result.value | RVSWD_DMSTATUS_RESUMEACK;
            return true;
        }
        if (!read_result.ok && !read_result.retryable) {
            break;
        }
    } while ((bsp_time_us() - start) < rvswd_debug_resume_timeout_us);

    // 失败路径也释放 resumereq，不能把跨命令状态留给下一次调试操作
    rvswd_operation_cleanup_write_dmi(operation, RVSWD_DMI_CONTROL, idle_control);
    return false;
}

bool rvswd_debug_restore_unlock(struct rvswd_operation *operation) {
    // QingKe 调试模块在 loader 运行后会丢失解锁写入，每次执行前重新恢复
    return rvswd_operation_write_dmi(operation, RVSWD_DMI_WCH_SHADOW, 0x5aa50400u).ok &&
           rvswd_operation_write_dmi(operation, RVSWD_DMI_WCH_CONFIG, 0x5aa50400u).ok &&
           rvswd_operation_write_dmi(operation, RVSWD_DMI_WCH_SHADOW, 0x5aa50400u).ok &&
           rvswd_operation_write_dmi(operation, RVSWD_DMI_WCH_CONFIG, 0x5aa50400u).ok;
}
