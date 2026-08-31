#include "wchlink/rvswd/rvswd_memory.h"

#include "wchlink/rvswd/rvswd_debug.h"
#include "wchlink/rvswd/rvswd_types.h"

#include <stddef.h>

static const uint8_t rvswd_memory_read_retry_count = 3u;
static const uint32_t rvswd_debug_data_address_base = 0xe0000000u;
static const uint32_t rvswd_abstract_command_execute = 0x00240000u;
static const uint32_t rvswd_abstractauto_data0 = 0x00000001u;

static bool rvswd_memory_read32_synchronized(
    struct rvswd_operation *operation, uint32_t address, uint32_t *value) {
    uint32_t abstractcs;
    struct rvswd_transport_result read_result;

    // 使用 x8 执行 c.lw，避免连续内存访问时的寄存器别名问题
    operation->memory_code = 0u;
    if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_ABSTRACTCS, 0x00000700u).ok) {
        operation->memory_code = 0x81u;
        return false;
    }
    if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_PROGBUF0, 0x90024000u).ok) {
        operation->memory_code = 0x82u;
        return false;
    }
    if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_DATA0, address).ok) {
        operation->memory_code = 0x83u;
        return false;
    }
    if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_COMMAND, 0x00271008u).ok) {
        operation->memory_code = 0x84u;
        return false;
    }
    if (!rvswd_debug_wait_abstract_idle(operation, &abstractcs)) {
        operation->memory_code = 0x85u;
        return false;
    }
    if (((abstractcs >> 8u) & 0x07u) != 0u) {
        operation->memory_code = 0x90u | (uint8_t)((abstractcs >> 8u) & 0x07u);
        return false;
    }
    if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_COMMAND, 0x00221008u).ok) {
        operation->memory_code = 0x86u;
        return false;
    }
    if (!rvswd_debug_wait_abstract_idle(operation, &abstractcs)) {
        operation->memory_code = 0x87u;
        return false;
    }
    if (((abstractcs >> 8u) & 0x07u) != 0u) {
        operation->memory_code = 0xa0u | (uint8_t)((abstractcs >> 8u) & 0x07u);
        return false;
    }
    read_result = rvswd_operation_read_dmi(operation, RVSWD_DMI_DATA0);
    if (!read_result.ok) {
        operation->memory_code = 0x88u;
        return false;
    }

    *value = read_result.value;
    return true;
}
static bool rvswd_memory_read32_v30x_once(
    struct rvswd_operation *operation, uint32_t address, uint32_t *value) {
    struct rvswd_transport_result read_result;

    operation->memory_code = 0u;
    // V30X 官方 LinkE 使用 Data1 传地址，Access Memory 命令直接返回 Data0
    if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_DATA1, address).ok) {
        operation->memory_code = 0xb1u;
        return false;
    }
    if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_COMMAND, 0x02200000u).ok) {
        operation->memory_code = 0xb2u;
        return false;
    }
    read_result = rvswd_operation_read_dmi(operation, RVSWD_DMI_DATA0);
    if (!read_result.ok) {
        operation->memory_code = 0xb3u;
        return false;
    }

    *value = read_result.value;
    return true;
}

bool rvswd_memory_write32_direct(
    struct rvswd_operation *operation, uint32_t address, uint32_t value) {
    uint32_t abstractcs;

    operation->memory_code = 0u;
    operation->address = address;
    // X03X 官方 LinkE 使用 Data1、Data0 和 Access Memory 写命令
    // 每次新 abstract 操作先清除上一次可能残留的 cmderr
    if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_ABSTRACTCS, 0x00000700u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_DATA1, address).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_DATA0, value).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_COMMAND, 0x02210000u).ok) {
        operation->memory_code = 0xc1u;
        return false;
    }
    if (!rvswd_debug_wait_abstract_idle(operation, &abstractcs)) {
        operation->memory_code = 0xc2u;
        return false;
    }
    if (((abstractcs >> 8u) & 0x07u) != 0u) {
        operation->memory_code = 0xc0u | (uint8_t)((abstractcs >> 8u) & 0x07u);
        // 清除 cmderr，避免失败状态污染后续 abstract 操作
        rvswd_operation_cleanup_write_dmi(operation, RVSWD_DMI_ABSTRACTCS, 0x00000700u);
        return false;
    }
    return true;
}

static bool rvswd_memory_write_direct(struct rvswd_operation *operation, uint32_t address, const uint8_t *data, uint32_t length) {
    for (uint32_t offset = 0u; offset < length; offset += 4u) {
        uint32_t value = ((uint32_t)data[offset + 0u]) |
                         ((uint32_t)data[offset + 1u] << 8u) |
                         ((uint32_t)data[offset + 2u] << 16u) |
                         ((uint32_t)data[offset + 3u] << 24u);

        if (!rvswd_memory_write32_direct(operation, address + offset, value)) {
            return false;
        }
    }
    return true;
}

static bool rvswd_memory_read32_v30x(struct rvswd_operation *operation, uint32_t address, uint32_t *value) {
    for (uint8_t retry = 0u; retry < rvswd_memory_read_retry_count; ++retry) {
        if (rvswd_memory_read32_v30x_once(operation, address, value)) {
            return true;
        }
    }
    return false;
}

bool rvswd_memory_read32(struct rvswd_operation *operation, const struct rvswd_target_profile *profile, bool target_identified, uint32_t address, uint32_t *value) {
    if (value == NULL) {
        return false;
    }
    if (profile != NULL &&
        (profile->wchlink_family == WCHLINK_TARGET_FAMILY_CH32L10X ||
         profile->wchlink_family == WCHLINK_TARGET_FAMILY_CH58X ||
         profile->wchlink_family == WCHLINK_TARGET_FAMILY_CH59X)) {
        return rvswd_memory_read32_synchronized(operation, address, value);
    }
    if (rvswd_memory_read32_v30x(operation, address, value)) {
        return true;
    }
    if (profile != NULL || target_identified) {
        return false;
    }

    // 连接阶段尚未取得 ChipID，V30X 失败后兼容 L103 重试
    return rvswd_memory_read32_synchronized(operation, address, value);
}

bool rvswd_memory_write32(struct rvswd_operation *operation, uint32_t address, uint32_t value) {
    uint32_t abstractcs;

    // 使用 x8 保存数据，x9 保存目标地址
    if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_ABSTRACTCS, 0x00000700u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_PROGBUF0, 0x0084a023u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_PROGBUF1, 0x00100073u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_DATA0, address).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_COMMAND, 0x00231009u).ok ||
        !rvswd_debug_wait_abstract_idle(operation, &abstractcs) ||
        ((abstractcs >> 8u) & 0x07u) != 0u ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_DATA0, value).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_COMMAND, 0x00271008u).ok ||
        !rvswd_debug_wait_abstract_idle(operation, &abstractcs)) {
        return false;
    }

    return ((abstractcs >> 8u) & 0x07u) == 0u;
}

static bool rvswd_memory_write_streaming(struct rvswd_operation *operation, uint32_t address, const uint8_t *data, uint32_t length) {
    uint32_t hartinfo;
    uint32_t data0_address;
    uint32_t abstractcs;
    struct rvswd_transport_result read_result;

    // 通过 Debug Module 的 DATA0/DATA1 地址建立连续写入上下文
    if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_ABSTRACTAUTO, 0u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_ABSTRACTCS, 0x00000700u).ok) {
        return false;
    }
    read_result = rvswd_operation_read_dmi(operation, RVSWD_DMI_HARTINFO);
    if (!read_result.ok) {
        return false;
    }
    hartinfo = read_result.value;

    data0_address = rvswd_debug_data_address_base | (hartinfo & 0x07ffu);

    // x10 指向 DATA0，x11 指向 DATA1，Program Buffer 每次从这两个寄存器取数
    if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_PROGBUF0, 0x41844100u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_PROGBUF1, 0x0491c080u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_PROGBUF2, 0x9002c184u).ok ||
        !rvswd_debug_write_raw_gpr(operation, 10u, data0_address) ||
        !rvswd_debug_write_raw_gpr(operation, 11u, data0_address + 4u) ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_DATA1, address).ok) {
        return false;
    }

    // 第一字通过显式 COMMAND 启动，避免 V30X 在 COMMAND 前开启 autoexec
    if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_DATA0, ((uint32_t)data[0u]) | ((uint32_t)data[1u] << 8u) | ((uint32_t)data[2u] << 16u) | ((uint32_t)data[3u] << 24u)).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_COMMAND, rvswd_abstract_command_execute).ok ||
        !rvswd_debug_wait_abstract_idle(operation, &abstractcs) ||
        ((abstractcs >> 8u) & 0x07u) != 0u ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_ABSTRACTAUTO, rvswd_abstractauto_data0).ok) {
        return false;
    }

    // autoexec 让每次 DATA0 写入自动执行一次 Program Buffer，DATA1 由目标端自增
    for (uint32_t offset = 4u; offset < length; offset += 4u) {
        uint32_t value = ((uint32_t)data[offset + 0u]) |
                         ((uint32_t)data[offset + 1u] << 8u) |
                         ((uint32_t)data[offset + 2u] << 16u) |
                         ((uint32_t)data[offset + 3u] << 24u);

        operation->address = address + offset;
        if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_DATA0, value).ok) {
            rvswd_operation_cleanup_write_dmi(operation, RVSWD_DMI_ABSTRACTAUTO, 0u);
            rvswd_operation_cleanup_write_dmi(operation, RVSWD_DMI_ABSTRACTCS, 0x00000700u);
            return false;
        }
    }

    // 关闭自动执行后再读取 ABSTRACTCS，确保最后一个字已经完成
    if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_ABSTRACTAUTO, 0u).ok ||
        !rvswd_debug_wait_abstract_idle(operation, &abstractcs) ||
        ((abstractcs >> 8u) & 0x07u) != 0u) {
        rvswd_operation_cleanup_write_dmi(operation, RVSWD_DMI_ABSTRACTAUTO, 0u);
        rvswd_operation_cleanup_write_dmi(operation, RVSWD_DMI_ABSTRACTCS, 0x00000700u);
        return false;
    }

    return true;
}

static bool rvswd_memory_write_slow(struct rvswd_operation *operation,
                                    uint32_t address, const uint8_t *data,
                                    uint32_t length) {
    uint32_t abstractcs;
    struct rvswd_transport_result read_result;

    // 先配置一次程序缓冲区，后续每个字只更新地址和数据寄存器
    if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_ABSTRACTAUTO, 0u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_ABSTRACTCS,
                                   0x00000700u)
             .ok) {
        operation->memory_code = 0xe1u;
        return false;
    }
    if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_PROGBUF0, 0x0084a023u)
             .ok) {
        operation->memory_code = 0xe2u;
        return false;
    }
    if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_PROGBUF1, 0x00100073u)
             .ok) {
        operation->memory_code = 0xe3u;
        return false;
    }

    for (uint32_t offset = 0u; offset < length; offset += 4u) {
        uint32_t value = ((uint32_t)data[offset + 0u]) |
                         ((uint32_t)data[offset + 1u] << 8u) |
                         ((uint32_t)data[offset + 2u] << 16u) |
                         ((uint32_t)data[offset + 3u] << 24u);

        operation->address = address + offset;
        if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_ABSTRACTCS, 0x00000700u).ok) {
            operation->memory_code = 0x10u | (operation->dmi_status & 0x03u);
            return false;
        }
        if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_DATA0, address + offset).ok) {
            operation->memory_code = 0x20u | (operation->dmi_status & 0x03u);
            return false;
        }
        if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_COMMAND, 0x00231009u).ok) {
            operation->memory_code = 0x30u | (operation->dmi_status & 0x03u);
            return false;
        }
        if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_DATA0, value).ok) {
            operation->memory_code = 0x40u | (operation->dmi_status & 0x03u);
            return false;
        }
        if (!rvswd_operation_write_dmi(operation, RVSWD_DMI_COMMAND, 0x00271008u).ok) {
            operation->memory_code = 0x50u | (operation->dmi_status & 0x03u);
            return false;
        }
        if (!rvswd_debug_wait_abstract_idle(operation, &abstractcs)) {
            operation->memory_code = 0x60u | (operation->dmi_status & 0x03u);
            return false;
        }
        if (((abstractcs >> 8u) & 0x07u) != 0u) {
            operation->memory_code =
                0x70u | (uint8_t)((abstractcs >> 8u) & 0x07u);
            return false;
        }
    }

    read_result = rvswd_operation_read_dmi(operation, RVSWD_DMI_ABSTRACTCS);
    if (!read_result.ok) {
        operation->memory_code = 0xe5u;
        return false;
    }
    abstractcs = read_result.value;
    if (((abstractcs >> 8u) & 0x07u) != 0u) {
        operation->memory_code = 0xd0u | (uint8_t)((abstractcs >> 8u) & 0x07u);
        return false;
    }
    return true;
}

bool rvswd_memory_write(struct rvswd_operation *operation, const struct rvswd_target_profile *profile, uint32_t address, const uint8_t *data, uint32_t length) {
    bool success;

    operation->memory_code = 0u;
    operation->address = address;
    operation->abstractcs = 0xffffffffu;
    if (data == NULL || length == 0u || (address & 3u) != 0u || (length & 3u) != 0u) {
        operation->memory_code = 0xefu;
        return false;
    }

    if (profile != NULL &&
        profile->memory_write_mode == RVSWD_MEMORY_WRITE_DIRECT) {
        success = rvswd_memory_write_direct(operation, address, data, length);
    } else if (profile != NULL && profile->memory_write_mode == RVSWD_MEMORY_WRITE_STREAMING) {
        // 目标 profile 支持 autoexec 数据流，先尝试单字 DMI 写入的快速路径
        success = rvswd_memory_write_streaming(operation, address, data, length);
        if (!success) {
            // 快速路径失败后清理 abstract 状态，再完整重写当前数据块
            rvswd_operation_cleanup_write_dmi(operation, RVSWD_DMI_ABSTRACTAUTO, 0u);
            rvswd_operation_cleanup_write_dmi(operation, RVSWD_DMI_ABSTRACTCS, 0x00000700u);
            operation->memory_code = 0u;
            operation->address = address;
            operation->abstractcs = 0xffffffffu;
            operation->dmi_status = 0u;
            operation->retryable = false;
            success = rvswd_memory_write_slow(operation, address, data, length);
        }
    } else {
        success = rvswd_memory_write_slow(operation, address, data, length);
    }
    if (!success) {
        struct rvswd_transport_result diagnostic;

        // 诊断读取不得覆盖导致写入失败的 DMI status 和 retryable
        diagnostic = rvswd_transport_read(operation->transport, RVSWD_DMI_ABSTRACTCS);
        if (diagnostic.ok) {
            operation->abstractcs = diagnostic.value;
        }
    }
    return success;
}
