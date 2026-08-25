#include "rvswd_memory.h"

#include "rvswd_debug.h"
#include "rvswd_dmi.h"
#include "rvswd_target.h"
#include "rvswd_types.h"

#include <stddef.h>

#define RVSWD_DMI_DATA0 0x04u
#define RVSWD_DMI_DATA1 0x05u
#define RVSWD_DMI_HARTINFO 0x12u
#define RVSWD_DMI_ABSTRACTCS 0x16u
#define RVSWD_DMI_COMMAND 0x17u
#define RVSWD_DMI_ABSTRACTAUTO 0x18u
#define RVSWD_DMI_PROGBUF0 0x20u
#define RVSWD_DMI_PROGBUF1 0x21u
#define RVSWD_DMI_PROGBUF2 0x22u

#define RVSWD_MEMORY_READ_RETRY_COUNT 3u
#define RVSWD_DEBUG_DATA_ADDRESS_BASE 0xe0000000u
#define RVSWD_ABSTRACT_COMMAND_EXECUTE 0x00240000u
#define RVSWD_ABSTRACTAUTO_DATA0 0x00000001u

static uint8_t rvswd_memory_last_error_value;
static uint8_t rvswd_memory_failure_dmi_status_value;
static uint32_t rvswd_memory_failure_address_value;
static uint32_t rvswd_memory_failure_abstractcs_value;

static bool rvswd_memory_read32_synchronized(uint32_t address, uint32_t *value) {
    uint32_t abstractcs;
    uint32_t data;

    // 使用 x8 执行 c.lw，避免连续内存访问时的寄存器别名问题
    rvswd_memory_last_error_value = 0u;
    if (!rvswd_dmi_write(0x16u, 0x00000700u)) {
        rvswd_memory_last_error_value = 0x81u;
        return false;
    }
    if (!rvswd_dmi_write(0x20u, 0x90024000u)) {
        rvswd_memory_last_error_value = 0x82u;
        return false;
    }
    if (!rvswd_dmi_write(0x04u, address)) {
        rvswd_memory_last_error_value = 0x83u;
        return false;
    }
    if (!rvswd_dmi_write(0x17u, 0x00271008u)) {
        rvswd_memory_last_error_value = 0x84u;
        return false;
    }
    if (!rvswd_debug_wait_abstract_idle(&abstractcs)) {
        rvswd_memory_last_error_value = 0x85u;
        return false;
    }
    if (((abstractcs >> 8u) & 0x07u) != 0u) {
        rvswd_memory_last_error_value = 0x90u | (uint8_t)((abstractcs >> 8u) & 0x07u);
        return false;
    }
    if (!rvswd_dmi_write(0x17u, 0x00221008u)) {
        rvswd_memory_last_error_value = 0x86u;
        return false;
    }
    if (!rvswd_debug_wait_abstract_idle(&abstractcs)) {
        rvswd_memory_last_error_value = 0x87u;
        return false;
    }
    if (((abstractcs >> 8u) & 0x07u) != 0u) {
        rvswd_memory_last_error_value = 0xa0u | (uint8_t)((abstractcs >> 8u) & 0x07u);
        return false;
    }
    if (!rvswd_dmi_read(0x04u, &data)) {
        rvswd_memory_last_error_value = 0x88u;
        return false;
    }

    *value = data;
    return true;
}
static bool rvswd_memory_read32_v30x_once(uint32_t address, uint32_t *value) {
    uint32_t data;

    rvswd_memory_last_error_value = 0u;
    // V30X 官方 LinkE 使用 Data1 传地址，Access Memory 命令直接返回 Data0
    if (!rvswd_dmi_write(RVSWD_DMI_DATA1, address)) {
        rvswd_memory_last_error_value = 0xb1u;
        return false;
    }
    if (!rvswd_dmi_write(RVSWD_DMI_COMMAND, 0x02200000u)) {
        rvswd_memory_last_error_value = 0xb2u;
        return false;
    }
    if (!rvswd_dmi_read(RVSWD_DMI_DATA0, &data)) {
        rvswd_memory_last_error_value = 0xb3u;
        return false;
    }

    *value = data;
    return true;
}

static bool rvswd_memory_read32_v30x(uint32_t address, uint32_t *value) {
    for (uint8_t retry = 0u; retry < RVSWD_MEMORY_READ_RETRY_COUNT; ++retry) {
        if (rvswd_memory_read32_v30x_once(address, value)) {
            return true;
        }
    }
    return false;
}

bool rvswd_memory_read32(uint32_t address, uint32_t *value) {
    const struct rvswd_target_profile *profile = rvswd_target_profile_current();

    if (value == NULL) {
        return false;
    }
    if (profile == NULL) {
        profile =
            rvswd_target_profile_from_family(rvswd_target_family_hint());
    }
    if (profile != NULL &&
        (profile->wchlink_family == WCHLINK_TARGET_FAMILY_L103 ||
         profile->wchlink_family == WCHLINK_TARGET_FAMILY_CH58X ||
         profile->wchlink_family == WCHLINK_TARGET_FAMILY_CH59X)) {
        return rvswd_memory_read32_synchronized(address, value);
    }
    if (rvswd_memory_read32_v30x(address, value)) {
        return true;
    }
    if (profile != NULL || rvswd_target_chip_id() != 0u) {
        return false;
    }

    // 连接阶段尚未取得 ChipID，V30X 失败后兼容 L103 重试
    return rvswd_memory_read32_synchronized(address, value);
}

bool rvswd_memory_write32(uint32_t address, uint32_t value) {
    uint32_t abstractcs;

    // 使用 x8 保存数据，x9 保存目标地址
    if (!rvswd_dmi_write(0x16u, 0x00000700u) ||
        !rvswd_dmi_write(0x20u, 0x0084a023u) ||
        !rvswd_dmi_write(0x21u, 0x00100073u) ||
        !rvswd_dmi_write(0x04u, address) ||
        !rvswd_dmi_write(0x17u, 0x00231009u) ||
        !rvswd_debug_wait_abstract_idle(&abstractcs) ||
        ((abstractcs >> 8u) & 0x07u) != 0u ||
        !rvswd_dmi_write(0x04u, value) ||
        !rvswd_dmi_write(0x17u, 0x00271008u) ||
        !rvswd_debug_wait_abstract_idle(&abstractcs)) {
        return false;
    }

    return ((abstractcs >> 8u) & 0x07u) == 0u;
}

static bool rvswd_memory_write_streaming(uint32_t address,
                                              const uint8_t *data,
                                              uint32_t length) {
    uint32_t hartinfo;
    uint32_t data0_address;
    uint32_t abstractcs;

    // 通过 Debug Module 的 DATA0/DATA1 地址建立连续写入上下文
    if (!rvswd_dmi_write(RVSWD_DMI_ABSTRACTAUTO, 0u) ||
        !rvswd_dmi_write(RVSWD_DMI_ABSTRACTCS, 0x00000700u) ||
        !rvswd_dmi_read(RVSWD_DMI_HARTINFO, &hartinfo)) {
        return false;
    }

    data0_address = RVSWD_DEBUG_DATA_ADDRESS_BASE | (hartinfo & 0x07ffu);

    // x10 指向 DATA0，x11 指向 DATA1，Program Buffer 每次从这两个寄存器取数
    if (!rvswd_dmi_write(RVSWD_DMI_PROGBUF0, 0x41844100u) ||
        !rvswd_dmi_write(RVSWD_DMI_PROGBUF1, 0x0491c080u) ||
        !rvswd_dmi_write(RVSWD_DMI_PROGBUF2, 0x9002c184u) ||
        !rvswd_debug_write_raw_gpr(10u, data0_address) ||
        !rvswd_debug_write_raw_gpr(11u, data0_address + 4u) ||
        !rvswd_dmi_write(RVSWD_DMI_DATA1, address)) {
        return false;
    }

    // 第一字通过显式 COMMAND 启动，避免 V30X 在 COMMAND 前开启 autoexec
    if (!rvswd_dmi_write(
            RVSWD_DMI_DATA0,
            ((uint32_t)data[0u]) |
                ((uint32_t)data[1u] << 8u) |
                ((uint32_t)data[2u] << 16u) |
                ((uint32_t)data[3u] << 24u)) ||
        !rvswd_dmi_write(RVSWD_DMI_COMMAND,
                               RVSWD_ABSTRACT_COMMAND_EXECUTE) ||
        !rvswd_debug_wait_abstract_idle(&abstractcs) ||
        ((abstractcs >> 8u) & 0x07u) != 0u ||
        !rvswd_dmi_write(RVSWD_DMI_ABSTRACTAUTO,
                               RVSWD_ABSTRACTAUTO_DATA0)) {
        return false;
    }

    // autoexec 让每次 DATA0 写入自动执行一次 Program Buffer，DATA1 由目标端自增
    for (uint32_t offset = 4u; offset < length; offset += 4u) {
        uint32_t value = ((uint32_t)data[offset + 0u]) |
                         ((uint32_t)data[offset + 1u] << 8u) |
                         ((uint32_t)data[offset + 2u] << 16u) |
                         ((uint32_t)data[offset + 3u] << 24u);

        rvswd_memory_failure_address_value = address + offset;
        if (!rvswd_dmi_write(RVSWD_DMI_DATA0, value)) {
            (void)rvswd_dmi_write(RVSWD_DMI_ABSTRACTAUTO, 0u);
            (void)rvswd_dmi_write(RVSWD_DMI_ABSTRACTCS, 0x00000700u);
            return false;
        }
    }

    // 关闭自动执行后再读取 ABSTRACTCS，确保最后一个字已经完成
    if (!rvswd_dmi_write(RVSWD_DMI_ABSTRACTAUTO, 0u) ||
        !rvswd_debug_wait_abstract_idle(&abstractcs) ||
        ((abstractcs >> 8u) & 0x07u) != 0u) {
        (void)rvswd_dmi_write(RVSWD_DMI_ABSTRACTAUTO, 0u);
        (void)rvswd_dmi_write(RVSWD_DMI_ABSTRACTCS, 0x00000700u);
        return false;
    }

    return true;
}

static bool rvswd_memory_write_slow(uint32_t address, const uint8_t *data,
                                         uint32_t length) {
    uint32_t abstractcs;

    // 先配置一次程序缓冲区，后续每个字只更新地址和数据寄存器
    if (!rvswd_dmi_write(RVSWD_DMI_ABSTRACTAUTO, 0u) ||
        !rvswd_dmi_write(RVSWD_DMI_ABSTRACTCS, 0x00000700u)) {
        rvswd_memory_last_error_value = 0xe1u;
        return false;
    }
    if (!rvswd_dmi_write(RVSWD_DMI_PROGBUF0, 0x0084a023u)) {
        rvswd_memory_last_error_value = 0xe2u;
        return false;
    }
    if (!rvswd_dmi_write(RVSWD_DMI_PROGBUF1, 0x00100073u)) {
        rvswd_memory_last_error_value = 0xe3u;
        return false;
    }

    for (uint32_t offset = 0u; offset < length; offset += 4u) {
        uint32_t value = ((uint32_t)data[offset + 0u]) |
                         ((uint32_t)data[offset + 1u] << 8u) |
                         ((uint32_t)data[offset + 2u] << 16u) |
                         ((uint32_t)data[offset + 3u] << 24u);

        rvswd_memory_failure_address_value = address + offset;
        if (!rvswd_dmi_write(0x16u, 0x00000700u)) {
            rvswd_memory_last_error_value = 0x10u | (rvswd_dmi_last_status() & 0x03u);
            return false;
        }
        if (!rvswd_dmi_write(0x04u, address + offset)) {
            rvswd_memory_last_error_value = 0x20u | (rvswd_dmi_last_status() & 0x03u);
            return false;
        }
        if (!rvswd_dmi_write(0x17u, 0x00231009u)) {
            rvswd_memory_last_error_value = 0x30u | (rvswd_dmi_last_status() & 0x03u);
            return false;
        }
        if (!rvswd_dmi_write(0x04u, value)) {
            rvswd_memory_last_error_value = 0x40u | (rvswd_dmi_last_status() & 0x03u);
            return false;
        }
        if (!rvswd_dmi_write(0x17u, 0x00271008u)) {
            rvswd_memory_last_error_value = 0x50u | (rvswd_dmi_last_status() & 0x03u);
            return false;
        }
        if (!rvswd_debug_wait_abstract_idle(&abstractcs)) {
            rvswd_memory_last_error_value = 0x60u | (rvswd_dmi_last_status() & 0x03u);
            return false;
        }
        if (((abstractcs >> 8u) & 0x07u) != 0u) {
            rvswd_memory_last_error_value = 0x70u | (uint8_t)((abstractcs >> 8u) & 0x07u);
            return false;
        }
    }

    if (!rvswd_dmi_read(0x16u, &abstractcs)) {
        rvswd_memory_last_error_value = 0xe5u;
        return false;
    }
    if (((abstractcs >> 8u) & 0x07u) != 0u) {
        rvswd_memory_last_error_value = 0xd0u | (uint8_t)((abstractcs >> 8u) & 0x07u);
        return false;
    }
    return true;
}

bool rvswd_memory_write(uint32_t address, const uint8_t *data, uint32_t length) {
    const struct rvswd_target_profile *profile = rvswd_target_profile_current();
    bool success;

    rvswd_memory_last_error_value = 0u;
    rvswd_memory_failure_dmi_status_value = 0u;
    rvswd_memory_failure_address_value = address;
    rvswd_memory_failure_abstractcs_value = 0xffffffffu;
    if (data == NULL || length == 0u || (address & 3u) != 0u || (length & 3u) != 0u) {
        rvswd_memory_last_error_value = 0xefu;
        return false;
    }

    if (profile != NULL &&
        profile->memory_write_mode == RVSWD_MEMORY_WRITE_STREAMING) {
        // 目标 profile 支持 autoexec 数据流，先尝试单字 DMI 写入的快速路径
        success = rvswd_memory_write_streaming(address, data, length);
        if (!success) {
            // 快速路径失败后清理 abstract 状态，再完整重写当前数据块
            (void)rvswd_dmi_write(RVSWD_DMI_ABSTRACTAUTO, 0u);
            (void)rvswd_dmi_write(RVSWD_DMI_ABSTRACTCS, 0x00000700u);
            success = rvswd_memory_write_slow(address, data, length);
        }
    } else {
        success = rvswd_memory_write_slow(address, data, length);
    }
    if (!success) {
        rvswd_memory_failure_dmi_status_value = rvswd_dmi_last_status();
        (void)rvswd_dmi_read(0x16u, &rvswd_memory_failure_abstractcs_value);
    }
    return success;
}

uint8_t rvswd_memory_last_error(void) {
    return rvswd_memory_last_error_value;
}

uint8_t rvswd_memory_failure_dmi_status(void) {
    return rvswd_memory_failure_dmi_status_value;
}

uint32_t rvswd_memory_failure_address(void) {
    return rvswd_memory_failure_address_value;
}

uint32_t rvswd_memory_failure_abstractcs(void) {
    return rvswd_memory_failure_abstractcs_value;
}
