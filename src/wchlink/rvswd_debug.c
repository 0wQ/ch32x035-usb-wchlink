#include "rvswd_debug.h"

#include "bsp/bsp_delay.h"
#include "rvswd_dmi.h"
#include "rvswd_gpio.h"

#include <stddef.h>

#define RVSWD_DMI_CONTROL 0x10u
#define RVSWD_DMI_ABSTRACTCS 0x16u
#define RVSWD_DMI_COMMAND 0x17u
#define RVSWD_DMI_DATA0 0x04u
#define RVSWD_ABSTRACT_TIMEOUT_US 10000u
#define RVSWD_RESUME_MIN_DELAY_US 1000u
#define RVSWD_EXECUTE_TIMEOUT_MS 5000u

bool rvswd_debug_wait_abstract_idle_timeout(uint32_t *abstractcs,
                                            uint32_t timeout_us) {
    uint64_t start = bsp_time_us();

    do {
        if (!rvswd_dmi_read(RVSWD_DMI_ABSTRACTCS, abstractcs)) {
            if (!rvswd_dmi_failure_retryable()) {
                return false;
            }
            continue;
        }
        if ((*abstractcs & (1u << 12u)) == 0u) {
            return true;
        }
    } while ((bsp_time_us() - start) < timeout_us);

    return false;
}

bool rvswd_debug_wait_abstract_idle(uint32_t *abstractcs) {
    return rvswd_debug_wait_abstract_idle_timeout(abstractcs,
                                                  RVSWD_ABSTRACT_TIMEOUT_US);
}

bool rvswd_debug_write_register(uint16_t regno, uint32_t value) {
    uint32_t abstractcs;

    if (!rvswd_dmi_write(RVSWD_DMI_DATA0, value) ||
        !rvswd_dmi_write(RVSWD_DMI_ABSTRACTCS, 0x00000700u) ||
        !rvswd_dmi_write(RVSWD_DMI_COMMAND, 0x00230000u | (uint32_t)regno) ||
        !rvswd_dmi_read(RVSWD_DMI_ABSTRACTCS, &abstractcs)) {
        return false;
    }
    return ((abstractcs >> 8u) & 0x07u) == 0u;
}

bool rvswd_debug_read_register(uint16_t regno, uint32_t *value) {
    uint32_t abstractcs;

    if (value == NULL ||
        !rvswd_dmi_write(RVSWD_DMI_ABSTRACTCS, 0x00000700u) ||
        !rvswd_dmi_write(RVSWD_DMI_COMMAND, 0x00220000u | (uint32_t)regno) ||
        !rvswd_dmi_read(RVSWD_DMI_ABSTRACTCS, &abstractcs) ||
        ((abstractcs >> 8u) & 0x07u) != 0u) {
        return false;
    }
    return rvswd_dmi_read(RVSWD_DMI_DATA0, value);
}

bool rvswd_debug_write_raw_gpr(uint8_t regno, uint32_t value) {
    uint32_t abstractcs;

    if (!rvswd_dmi_write(RVSWD_DMI_DATA0, value) ||
        !rvswd_dmi_write(RVSWD_DMI_ABSTRACTCS, 0x00000700u) ||
        !rvswd_dmi_write(RVSWD_DMI_COMMAND, 0x00231000u | (uint32_t)regno) ||
        !rvswd_dmi_read(RVSWD_DMI_ABSTRACTCS, &abstractcs)) {
        return false;
    }
    return ((abstractcs >> 8u) & 0x07u) == 0u;
}

bool rvswd_debug_read_raw_gpr(uint8_t regno, uint32_t *value) {
    uint32_t abstractcs;

    if (value == NULL ||
        !rvswd_dmi_write(RVSWD_DMI_ABSTRACTCS, 0x00000700u) ||
        !rvswd_dmi_write(RVSWD_DMI_COMMAND, 0x00221000u | (uint32_t)regno) ||
        !rvswd_dmi_read(RVSWD_DMI_ABSTRACTCS, &abstractcs) ||
        ((abstractcs >> 8u) & 0x07u) != 0u) {
        return false;
    }
    return rvswd_dmi_read(RVSWD_DMI_DATA0, value);
}

bool rvswd_debug_wait_dmstatus(uint32_t mask, bool set,
                               uint32_t timeout_ms) {
    uint64_t start = bsp_time_us();

    do {
        uint32_t status;

        if (!rvswd_dmi_read(0x11u, &status)) {
            return false;
        }
        if (((status & mask) != 0u) == set) {
            return true;
        }
        bsp_delay_us(100u);
    } while ((bsp_time_us() - start) < (uint64_t)timeout_ms * 1000u);

    return false;
}

bool rvswd_debug_halt(void) {
    return rvswd_dmi_write(RVSWD_DMI_CONTROL, 0x80000001u) &&
           rvswd_debug_wait_dmstatus(1u << 9u, true, 100u);
}

bool rvswd_debug_execute(uint32_t entry, uint32_t stack_top, uint32_t mode,
                         uint32_t address, uint32_t length,
                         uint32_t data_address, uint32_t *result) {
    if (!rvswd_debug_write_raw_gpr(10u, mode)) {
        if (result != NULL) *result = 0xe001u;
        return false;
    }
    if (!rvswd_debug_write_raw_gpr(11u, address)) {
        if (result != NULL) *result = 0xe002u;
        return false;
    }
    if (!rvswd_debug_write_raw_gpr(12u, length)) {
        if (result != NULL) *result = 0xe003u;
        return false;
    }
    if (!rvswd_debug_write_raw_gpr(13u, data_address)) {
        if (result != NULL) *result = 0xe004u;
        return false;
    }
    if (!rvswd_debug_write_register(0x1002u, stack_top) ||
        !rvswd_debug_write_register(0x7b0u, 0x000090c3u) ||
        !rvswd_debug_write_register(0x300u, 0u) ||
        !rvswd_debug_write_register(0x7b1u, entry)) {
        if (result != NULL) *result = 0xe005u;
        return false;
    }
    if (!rvswd_dmi_write(RVSWD_DMI_CONTROL, 0x80000001u) ||
        !rvswd_dmi_write(RVSWD_DMI_CONTROL, 0x80000001u) ||
        !rvswd_dmi_write(RVSWD_DMI_CONTROL, 0x00000001u) ||
        !rvswd_dmi_write(RVSWD_DMI_CONTROL, 0x40000001u)) {
        if (result != NULL) *result = 0xe006u;
        return false;
    }
    // V30X 的 resumeack 会跨会话保持，给 resumereq 留出处理时间后直接等待 ebreak
    bsp_delay_us(RVSWD_RESUME_MIN_DELAY_US);
    if (!rvswd_dmi_write(RVSWD_DMI_CONTROL, 0x00000001u)) {
        if (result != NULL) *result = 0xe006u;
        return false;
    }
    if (!rvswd_debug_wait_dmstatus(1u << 9u, true,
                                   RVSWD_EXECUTE_TIMEOUT_MS)) {
        (void)rvswd_debug_halt();
        if (result != NULL) *result = 0xe007u;
        return false;
    }
    if (result != NULL) {
        if (!rvswd_debug_read_raw_gpr(10u, result)) {
            *result = 0xe008u;
            return false;
        }
    }
    return true;
}
