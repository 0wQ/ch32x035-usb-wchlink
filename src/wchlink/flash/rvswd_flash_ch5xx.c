#include "wchlink/flash/rvswd_flash_ch5xx.h"

#include "wchlink/flash/rvswd_flash.h"
#include "wchlink/rvswd/rvswd_debug.h"
#include "wchlink/rvswd/rvswd_memory.h"
#include "wchlink/rvswd/rvswd_operation.h"
#include "wchlink/rvswd/rvswd_types.h"

#include <stddef.h>

// CH58x 与 CH59x 共用同一套 Flash 命令口协议，profile 只负责入口约束和 RAM 写入策略
static const uint32_t rvswd_ch5xx_flash_key_address = 0x40001040u;
static const uint32_t rvswd_ch5xx_flash_word_data_address = 0x40001800u;
static const uint32_t rvswd_ch5xx_flash_byte_data_address = 0x40001804u;
static const uint32_t rvswd_ch5xx_flash_control_address = 0x40001806u;
static const uint32_t rvswd_ch5xx_debug_data_address = 0xe0000380u;
static const uint32_t rvswd_ch5xx_flash_end = 0x00078000u;
static const uint32_t rvswd_ch5xx_flash_page_size = 0x00000100u;
static const uint32_t rvswd_ch5xx_flash_status_retries = 102u;
static const uint32_t rvswd_ch5xx_page_program_timeout_us = 100000u;
static const uint32_t rvswd_ch5xx_erase_stub_address = 0x20004000u;
static const uint32_t rvswd_ch5xx_erase_stub_stack_top = 0x20007000u;
static const size_t rvswd_ch5xx_erase_stub_max_size = 512u;

enum rvswd_ch5xx_flash_error {
    RVSWD_CH5XX_FLASH_ERROR_ERASE_STUB_SIZE = 0xc1u,
    RVSWD_CH5XX_FLASH_ERROR_ERASE_STUB_WRITE = 0xc2u,
    RVSWD_CH5XX_FLASH_ERROR_ERASE_CODE_MODE = 0xc3u,
    RVSWD_CH5XX_FLASH_ERROR_ERASE_EXECUTE = 0xc4u,
    RVSWD_CH5XX_FLASH_ERROR_ERASE_RESULT = 0xc5u,
    RVSWD_CH5XX_FLASH_ERROR_COMMAND_BEGIN = 0xc2u,
    RVSWD_CH5XX_FLASH_ERROR_COMMAND_ADDRESS = 0xc3u,
    RVSWD_CH5XX_FLASH_ERROR_COMMAND_FINISH = 0xc4u,
    RVSWD_CH5XX_FLASH_ERROR_STATUS_BEGIN = 0xc5u,
    RVSWD_CH5XX_FLASH_ERROR_STATUS_READ_FIRST = 0xc6u,
    RVSWD_CH5XX_FLASH_ERROR_STATUS_READ_SECOND = 0xc7u,
    RVSWD_CH5XX_FLASH_ERROR_STATUS_FINISH = 0xc8u,
    RVSWD_CH5XX_FLASH_ERROR_STATUS_TIMEOUT = 0xc9u,
    RVSWD_CH5XX_FLASH_ERROR_PAGE_UNALIGNED = 0xcau,
    RVSWD_CH5XX_FLASH_ERROR_PAGE_ERASE_CODE_MODE = 0xcbu,
    RVSWD_CH5XX_FLASH_ERROR_PAGE_ERASE_OPEN = 0xccu,
    RVSWD_CH5XX_FLASH_ERROR_PAGE_PROGRAM_CODE_MODE = 0xcdu,
    RVSWD_CH5XX_FLASH_ERROR_PAGE_PROGRAM_OPEN = 0xceu,
    RVSWD_CH5XX_FLASH_ERROR_PAGE_PROGRAM_BEGIN = 0xd0u,
    RVSWD_CH5XX_FLASH_ERROR_PAGE_PROGRAM_SETUP = 0xd1u,
    RVSWD_CH5XX_FLASH_ERROR_PAGE_PROGRAM_DATA = 0xd2u,
    RVSWD_CH5XX_FLASH_ERROR_PAGE_PROGRAM_EXECUTE = 0xd3u,
    RVSWD_CH5XX_FLASH_ERROR_PAGE_PROGRAM_WAIT = 0xd4u,
    RVSWD_CH5XX_FLASH_ERROR_PAGE_PROGRAM_STATUS = 0xd5u,
};

extern const uint8_t ch5xx_flash_erase_stub_start[];
extern const uint8_t ch5xx_flash_erase_stub_end[];

enum rvswd_ch5xx_byte_access_mode {
    RVSWD_CH5XX_BYTE_ACCESS_NONE,
    RVSWD_CH5XX_BYTE_ACCESS_READ,
    RVSWD_CH5XX_BYTE_ACCESS_WRITE,
};

struct rvswd_ch5xx_byte_access {
    struct rvswd_operation *operation;
    enum rvswd_ch5xx_byte_access_mode mode;
};

static bool rvswd_flash_ch5xx_prepare_byte_access(
    struct rvswd_ch5xx_byte_access *access,
    enum rvswd_ch5xx_byte_access_mode mode) {
    uint32_t load_instruction;
    uint32_t store_instruction;

    if (access->mode == mode) {
        return true;
    }

    if (mode == RVSWD_CH5XX_BYTE_ACCESS_READ) {
        load_instruction = 0x00058483u;
        store_instruction = 0x00968223u;
    } else if (mode == RVSWD_CH5XX_BYTE_ACCESS_WRITE) {
        load_instruction = 0x00468483u;
        store_instruction = 0x00958023u;
    } else {
        return false;
    }

    // 全擦期间连续访问 Flash 命令口，切换读写方向时才重建 Program Buffer
    if (!rvswd_debug_write_raw_gpr(access->operation, 13u,
                                   rvswd_ch5xx_debug_data_address) ||
        !rvswd_operation_write_dmi(access->operation, RVSWD_DMI_ABSTRACTCS,
                                   0x00000700u)
             .ok ||
        !rvswd_operation_write_dmi(access->operation, RVSWD_DMI_PROGBUF0,
                                   load_instruction)
             .ok ||
        !rvswd_operation_write_dmi(access->operation, RVSWD_DMI_PROGBUF1,
                                   store_instruction)
             .ok ||
        !rvswd_operation_write_dmi(access->operation, RVSWD_DMI_PROGBUF2,
                                   0x00100073u)
             .ok) {
        access->mode = RVSWD_CH5XX_BYTE_ACCESS_NONE;
        return false;
    }

    access->mode = mode;
    return true;
}

static bool rvswd_flash_ch5xx_write8(
    struct rvswd_ch5xx_byte_access *access, uint32_t address, uint8_t value) {
    uint32_t abstractcs;

    if (!rvswd_flash_ch5xx_prepare_byte_access(
            access, RVSWD_CH5XX_BYTE_ACCESS_WRITE) ||
        !rvswd_operation_write_dmi(access->operation, RVSWD_DMI_DATA1, value).ok ||
        !rvswd_operation_write_dmi(access->operation, RVSWD_DMI_DATA0, address).ok ||
        !rvswd_operation_write_dmi(access->operation, RVSWD_DMI_COMMAND,
                                   0x0027100bu)
             .ok ||
        !rvswd_debug_wait_abstract_idle(access->operation, &abstractcs)) {
        return false;
    }

    return ((abstractcs >> 8u) & 0x07u) == 0u;
}

static bool rvswd_flash_ch5xx_read8(
    struct rvswd_ch5xx_byte_access *access, uint32_t address, uint8_t *value) {
    uint32_t abstractcs;
    struct rvswd_transport_result read_result;

    if (value == NULL ||
        !rvswd_flash_ch5xx_prepare_byte_access(
            access, RVSWD_CH5XX_BYTE_ACCESS_READ) ||
        !rvswd_operation_write_dmi(access->operation, RVSWD_DMI_DATA0, address).ok ||
        !rvswd_operation_write_dmi(access->operation, RVSWD_DMI_COMMAND,
                                   0x0027100bu)
             .ok ||
        !rvswd_debug_wait_abstract_idle(access->operation, &abstractcs) ||
        ((abstractcs >> 8u) & 0x07u) != 0u) {
        return false;
    }
    read_result = rvswd_operation_read_dmi(access->operation, RVSWD_DMI_DATA1);
    if (!read_result.ok) {
        return false;
    }

    *value = (uint8_t)read_result.value;
    return true;
}

static bool rvswd_flash_ch5xx_issue(struct rvswd_ch5xx_byte_access *access,
                                    uint8_t command) {
    return rvswd_flash_ch5xx_write8(
               access, rvswd_ch5xx_flash_control_address, 0u) &&
           rvswd_flash_ch5xx_write8(
               access, rvswd_ch5xx_flash_control_address, 5u) &&
           rvswd_flash_ch5xx_write8(
               access, rvswd_ch5xx_flash_byte_data_address, command);
}

static bool rvswd_flash_ch5xx_wait_control_ready(
    struct rvswd_ch5xx_byte_access *access) {
    for (uint32_t retry = 0u; retry < rvswd_ch5xx_flash_status_retries;
         ++retry) {
        uint8_t control;

        if (!rvswd_flash_ch5xx_read8(
                access, rvswd_ch5xx_flash_control_address, &control)) {
            return false;
        }
        if ((control & 0x80u) == 0u) {
            return true;
        }
    }
    return false;
}

static bool rvswd_flash_ch5xx_end(struct rvswd_ch5xx_byte_access *access) {
    return rvswd_flash_ch5xx_wait_control_ready(access) &&
           rvswd_flash_ch5xx_write8(
               access, rvswd_ch5xx_flash_control_address, 0u);
}

static bool rvswd_flash_ch5xx_out(struct rvswd_ch5xx_byte_access *access,
                                  uint8_t value) {
    return rvswd_flash_ch5xx_wait_control_ready(access) &&
           rvswd_flash_ch5xx_write8(
               access, rvswd_ch5xx_flash_byte_data_address, value);
}

static bool rvswd_flash_ch5xx_in(struct rvswd_ch5xx_byte_access *access,
                                 uint8_t *value) {
    return rvswd_flash_ch5xx_wait_control_ready(access) &&
           rvswd_flash_ch5xx_read8(
               access, rvswd_ch5xx_flash_byte_data_address, value);
}

static bool rvswd_flash_ch5xx_begin(struct rvswd_ch5xx_byte_access *access,
                                    uint8_t command) {
    // CH5xx 每次切换命令前先完成命令 6，命令口状态不能跨命令复用
    return rvswd_flash_ch5xx_issue(access, 6u) &&
           rvswd_flash_ch5xx_end(access) &&
           rvswd_flash_ch5xx_issue(access, command);
}

static bool rvswd_flash_ch5xx_open(struct rvswd_ch5xx_byte_access *access) {
    // FlashOpen 先使能命令口，再完成 0xff 初始化命令，擦页和写页均从此状态开始
    return rvswd_flash_ch5xx_write8(
               access, rvswd_ch5xx_flash_control_address, 4u) &&
           rvswd_flash_ch5xx_begin(access, 0xffu) &&
           rvswd_flash_ch5xx_end(access);
}

static bool rvswd_flash_ch5xx_enable_code_mode(
    struct rvswd_operation *operation) {
    uint32_t abstractcs;

    // SAFE_ACCESS_SIG 必须在修改 ROM 配置后清零，避免命令口继承未完成的安全访问状态
    if (!rvswd_debug_write_raw_gpr(operation, 13u,
                                   rvswd_ch5xx_flash_key_address) ||
        !rvswd_debug_write_raw_gpr(operation, 10u, 0x57u) ||
        !rvswd_debug_write_raw_gpr(operation, 11u, 0xa8u) ||
        !rvswd_debug_write_raw_gpr(operation, 12u, 0xe0u) ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_ABSTRACTAUTO, 0u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_ABSTRACTCS, 0x00000700u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_PROGBUF0, 0x00a68023u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_PROGBUF1, 0x00b68023u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_PROGBUF2, 0x00010001u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_PROGBUF3, 0x00c68223u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_PROGBUF4, 0x00068023u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_PROGBUF5, 0x00100073u).ok ||
        !rvswd_operation_write_dmi(operation, RVSWD_DMI_COMMAND, 0x00271000u).ok ||
        !rvswd_debug_wait_abstract_idle(operation, &abstractcs)) {
        return false;
    }

    return ((abstractcs >> 8u) & 0x07u) == 0u;
}

static bool rvswd_flash_ch5xx_write_address(
    struct rvswd_ch5xx_byte_access *access, uint32_t address) {
    return rvswd_flash_ch5xx_out(access, (uint8_t)(address >> 16u)) &&
           rvswd_flash_ch5xx_out(access, (uint8_t)(address >> 8u)) &&
           rvswd_flash_ch5xx_out(access, (uint8_t)address);
}

static bool rvswd_flash_ch5xx_wait_ready(
    struct rvswd_ch5xx_byte_access *access) {
    if (!rvswd_flash_ch5xx_end(access)) {
        access->operation->flash_code =
            RVSWD_CH5XX_FLASH_ERROR_COMMAND_FINISH;
        return false;
    }

    for (uint32_t retry = 0u; retry < rvswd_ch5xx_flash_status_retries;
         ++retry) {
        uint8_t status;

        // LinkE 连续读取两次状态，第二次读数用于判断命令是否仍在执行
        if (!rvswd_flash_ch5xx_begin(access, 5u)) {
            access->operation->flash_code =
                RVSWD_CH5XX_FLASH_ERROR_STATUS_BEGIN;
            return false;
        }
        if (!rvswd_flash_ch5xx_in(access, &status)) {
            access->operation->flash_code =
                RVSWD_CH5XX_FLASH_ERROR_STATUS_READ_FIRST;
            return false;
        }
        if (!rvswd_flash_ch5xx_in(access, &status)) {
            access->operation->flash_code =
                RVSWD_CH5XX_FLASH_ERROR_STATUS_READ_SECOND;
            return false;
        }
        if (!rvswd_flash_ch5xx_end(access)) {
            access->operation->flash_code =
                RVSWD_CH5XX_FLASH_ERROR_STATUS_FINISH;
            return false;
        }
        if ((status & 1u) == 0u) {
            return true;
        }
    }
    access->operation->flash_code = RVSWD_CH5XX_FLASH_ERROR_STATUS_TIMEOUT;
    return false;
}

static bool rvswd_flash_ch5xx_erase_command(
    struct rvswd_ch5xx_byte_access *access, uint32_t address, uint8_t command) {
    if (!rvswd_flash_ch5xx_begin(access, command)) {
        access->operation->flash_code =
            RVSWD_CH5XX_FLASH_ERROR_COMMAND_BEGIN;
        return false;
    }
    if (!rvswd_flash_ch5xx_write_address(access, address)) {
        access->operation->flash_code =
            RVSWD_CH5XX_FLASH_ERROR_COMMAND_ADDRESS;
        return false;
    }
    if (!rvswd_flash_ch5xx_wait_ready(access)) {
        return false;
    }
    return true;
}

static bool rvswd_flash_ch5xx_program_page(
    struct rvswd_ch5xx_byte_access *access, const uint8_t *data) {
    uint32_t abstractcs;

    // 官方 0x33D4 与 CH5xx 参考实现都使用该 Program Buffer，将 a4 的字写入 Flash 数据口
    access->mode = RVSWD_CH5XX_BYTE_ACCESS_NONE;
    if (!rvswd_debug_write_raw_gpr(access->operation, 13u,
                                   rvswd_ch5xx_flash_word_data_address) ||
        !rvswd_debug_write_raw_gpr(access->operation, 5u, 21u) ||
        !rvswd_operation_write_dmi(access->operation, RVSWD_DMI_ABSTRACTAUTO, 0u).ok ||
        !rvswd_operation_write_dmi(access->operation, RVSWD_DMI_ABSTRACTCS, 0x00000700u).ok ||
        !rvswd_operation_write_dmi(access->operation, RVSWD_DMI_PROGBUF0, 0x4791c298u).ok ||
        !rvswd_operation_write_dmi(access->operation, RVSWD_DMI_PROGBUF1, 0x00668703u).ok ||
        !rvswd_operation_write_dmi(access->operation, RVSWD_DMI_PROGBUF2, 0xfe074ee3u).ok ||
        !rvswd_operation_write_dmi(access->operation, RVSWD_DMI_PROGBUF3, 0x00568323u).ok ||
        !rvswd_operation_write_dmi(access->operation, RVSWD_DMI_PROGBUF4, 0xfbed17fdu).ok ||
        !rvswd_operation_write_dmi(access->operation, RVSWD_DMI_PROGBUF5, 0x00100073u).ok) {
        access->operation->flash_code =
            RVSWD_CH5XX_FLASH_ERROR_PAGE_PROGRAM_SETUP;
        return false;
    }

    for (uint32_t offset = 0u; offset < rvswd_ch5xx_flash_page_size;
         offset += 4u) {
        uint32_t value = (uint32_t)data[offset + 0u] |
                         ((uint32_t)data[offset + 1u] << 8u) |
                         ((uint32_t)data[offset + 2u] << 16u) |
                         ((uint32_t)data[offset + 3u] << 24u);

        if (!rvswd_operation_write_dmi(access->operation, RVSWD_DMI_DATA0, value)
                 .ok) {
            access->operation->flash_code =
                RVSWD_CH5XX_FLASH_ERROR_PAGE_PROGRAM_DATA;
            return false;
        }
        // 该 Abstract Command 同时把 data0 传入 a4 并执行 Program Buffer
        if (!rvswd_operation_write_dmi(access->operation, RVSWD_DMI_COMMAND,
                                       0x0027100eu)
                 .ok) {
            access->operation->flash_code =
                RVSWD_CH5XX_FLASH_ERROR_PAGE_PROGRAM_EXECUTE;
            return false;
        }
        // 目标执行真实 Flash 写入时 busy 可长于普通内存访问，单字仍受 100 ms 上限约束
        if (!rvswd_debug_wait_abstract_idle_timeout(
                access->operation, &abstractcs,
                rvswd_ch5xx_page_program_timeout_us)) {
            access->operation->flash_code =
                RVSWD_CH5XX_FLASH_ERROR_PAGE_PROGRAM_WAIT;
            return false;
        }
        if (((abstractcs >> 8u) & 0x07u) != 0u) {
            access->operation->flash_code =
                RVSWD_CH5XX_FLASH_ERROR_PAGE_PROGRAM_STATUS;
            return false;
        }
    }
    return true;
}

bool rvswd_flash_rewrite_page(struct rvswd_operation *operation,
                              const struct rvswd_target_profile *profile,
                              uint32_t address, const uint8_t *data) {
    struct rvswd_ch5xx_byte_access access = {
        .operation = operation,
        .mode = RVSWD_CH5XX_BYTE_ACCESS_NONE,
    };

    operation->flash_code = 0u;
    if (profile == NULL || !profile->ch5xx_protocol || data == NULL) {
        operation->flash_code = 0x0fu;
        return false;
    }
    if ((address & (rvswd_ch5xx_flash_page_size - 1u)) != 0u) {
        operation->flash_code = RVSWD_CH5XX_FLASH_ERROR_PAGE_UNALIGNED;
        return false;
    }

    // 官方 0x0A 路径先用命令 0x81 擦页，再独立建立命令 0x02 的整页写入状态
    if (!rvswd_flash_ch5xx_enable_code_mode(operation)) {
        operation->flash_code =
            RVSWD_CH5XX_FLASH_ERROR_PAGE_ERASE_CODE_MODE;
        return false;
    }
    if (!rvswd_flash_ch5xx_open(&access)) {
        operation->flash_code = RVSWD_CH5XX_FLASH_ERROR_PAGE_ERASE_OPEN;
        return false;
    }
    if (!rvswd_flash_ch5xx_erase_command(&access, address, 0x81u)) {
        return false;
    }
    // 擦页会结束前一条 Flash 命令，写页前重新写入 SAFE_ACCESS_SIG 和 code mode
    if (!rvswd_flash_ch5xx_enable_code_mode(operation)) {
        operation->flash_code =
            RVSWD_CH5XX_FLASH_ERROR_PAGE_PROGRAM_CODE_MODE;
        return false;
    }
    if (!rvswd_flash_ch5xx_open(&access)) {
        operation->flash_code = RVSWD_CH5XX_FLASH_ERROR_PAGE_PROGRAM_OPEN;
        return false;
    }
    if (!rvswd_flash_ch5xx_begin(&access, 0x02u) ||
        !rvswd_flash_ch5xx_write_address(&access, address)) {
        operation->flash_code = RVSWD_CH5XX_FLASH_ERROR_PAGE_PROGRAM_BEGIN;
        return false;
    }
    if (!rvswd_flash_ch5xx_program_page(&access, data)) {
        return false;
    }
    if (!rvswd_flash_ch5xx_wait_ready(&access)) {
        return false;
    }
    return true;
}

bool rvswd_flash_ch5xx_erase_all(
    struct rvswd_operation *operation,
    const struct rvswd_target_profile *profile) {
    size_t stub_length =
        (size_t)(ch5xx_flash_erase_stub_end - ch5xx_flash_erase_stub_start);
    uint32_t result;

    // RAM stub 按 LinkE 固件的 4 KiB 扇区路径完成整片擦除
    if (stub_length == 0u ||
        stub_length > rvswd_ch5xx_erase_stub_max_size ||
        (stub_length & 3u) != 0u) {
        operation->flash_code = RVSWD_CH5XX_FLASH_ERROR_ERASE_STUB_SIZE;
        return false;
    }
    if (!rvswd_memory_write(operation, profile,
                            rvswd_ch5xx_erase_stub_address,
                            ch5xx_flash_erase_stub_start,
                            (uint32_t)stub_length)) {
        operation->flash_code = RVSWD_CH5XX_FLASH_ERROR_ERASE_STUB_WRITE;
        return false;
    }
    if (!rvswd_flash_ch5xx_enable_code_mode(operation)) {
        operation->flash_code = RVSWD_CH5XX_FLASH_ERROR_ERASE_CODE_MODE;
        return false;
    }
    if (!rvswd_debug_restore_unlock(operation)) {
        operation->flash_code = RVSWD_CH5XX_FLASH_ERROR_ERASE_EXECUTE;
        return false;
    }
    if (!rvswd_debug_execute(operation, rvswd_ch5xx_erase_stub_address,
                             rvswd_ch5xx_erase_stub_stack_top, 0u, 0u,
                             rvswd_ch5xx_flash_end, 0u,
                             rvswd_ch5xx_erase_stub_address, &result)) {
        operation->flash_code = RVSWD_CH5XX_FLASH_ERROR_ERASE_EXECUTE;
        return false;
    }
    if (result != 0u) {
        operation->flash_code = RVSWD_CH5XX_FLASH_ERROR_ERASE_RESULT;
        return false;
    }
    return true;
}
