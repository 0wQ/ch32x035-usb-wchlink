#include "wchlink/protocol/wchlink_wire.h"
#include "wchlink/session/wchlink_transfer_internal.h"
#include "wchlink/target/wchlink_target_control.h"
#include "wchlink/target/wchlink_target_flash.h"
#include "wchlink/target/wchlink_target_transfer.h"

#include <string.h>

static uint32_t wchlink_transfer_checksum_add(uint32_t checksum,
                                              const uint8_t *data,
                                              size_t length) {
    for (size_t offset = 0u; offset < length; offset += 4u) {
        checksum += (uint32_t)data[offset + 0u] |
                    ((uint32_t)data[offset + 1u] << 8u) |
                    ((uint32_t)data[offset + 2u] << 16u) |
                    ((uint32_t)data[offset + 3u] << 24u);
    }
    return checksum;
}

static uint32_t wchlink_transfer_padded_data_length(
    const struct wchlink_transfer *transfer) {
    const struct rvswd_target_info target_info =
        wchlink_target_ports_info(transfer->target);
    uint32_t length = transfer->flash_chunk_length;
    uint32_t page_size = target_info.loader_data_page_size;

    // 需要页对齐的 loader 以完整页参与校验，尾页保持擦除态
    if (page_size > 1u) {
        length = (length + (page_size - 1u)) & ~(page_size - 1u);
    }
    return length;
}

static struct rvswd_target_result wchlink_transfer_write_padding(
    struct wchlink_transfer *transfer, uint32_t from, uint32_t to) {
    while (from < to) {
        uint32_t length = to - from;
        struct rvswd_target_result result;

        if (length > sizeof(transfer->flash_padding)) {
            length = sizeof(transfer->flash_padding);
        }
        memset(transfer->flash_padding, 0xff, length);
        result = wchlink_target_ports_write_loader_data(
            transfer->target, from, transfer->flash_padding, length);
        if (!result.ok) {
            return result;
        }
        transfer->flash_checksum = wchlink_transfer_checksum_add(
            transfer->flash_checksum, transfer->flash_padding, length);
        from += length;
    }
    return rvswd_target_result_success();
}

static void wchlink_transfer_cache_range(struct wchlink_transfer *transfer,
                                         uint32_t address,
                                         const uint8_t *data,
                                         uint32_t length) {
    uint32_t start = address;
    uint32_t end = address + length;

    if (start >= WCHLINK_TRANSFER_CHUNK_CAPACITY || end <= start) {
        return;
    }
    if (end > WCHLINK_TRANSFER_CHUNK_CAPACITY) {
        end = WCHLINK_TRANSFER_CHUNK_CAPACITY;
    }
    memcpy(&transfer->partial_cache[start], data, end - start);
}

static bool wchlink_transfer_rewrite_partial_page(
    struct wchlink_transfer *transfer) {
    uint32_t page_address = transfer->partial_write_address &
                            ~(WCHLINK_TRANSFER_PACKET_CAPACITY - 1u);
    uint32_t sector_address = transfer->partial_write_address &
                              ~(WCHLINK_TRANSFER_CHUNK_CAPACITY - 1u);
    uint32_t page_offset = transfer->partial_write_address &
                           (WCHLINK_TRANSFER_PACKET_CAPACITY - 1u);

    if (!wchlink_target_ports_info(transfer->target).partial_write_supported ||
        page_offset + transfer->partial_write_length >
            WCHLINK_TRANSFER_PACKET_CAPACITY) {
        return false;
    }

    if (transfer->partial_cache_valid && sector_address == 0u) {
        uint32_t cache_offset = page_address - sector_address;

        memcpy(transfer->partial_write_page,
               &transfer->partial_cache[cache_offset],
               WCHLINK_TRANSFER_PACKET_CAPACITY);
    } else {
        // 没有下载缓存时只回读目标页，不扩展为整个 4 KiB 扇区
        for (uint32_t offset = 0u; offset < WCHLINK_TRANSFER_PACKET_CAPACITY;
             offset += 4u) {
            struct rvswd_target_result result =
                wchlink_target_ports_read_memory32(
                    transfer->target, page_address + offset);
            uint32_t value = result.value;

            if (!result.ok) {
                return false;
            }
            transfer->partial_write_page[offset + 0u] = (uint8_t)value;
            transfer->partial_write_page[offset + 1u] =
                (uint8_t)(value >> 8u);
            transfer->partial_write_page[offset + 2u] =
                (uint8_t)(value >> 16u);
            transfer->partial_write_page[offset + 3u] =
                (uint8_t)(value >> 24u);
        }
    }
    memcpy(&transfer->partial_write_page[page_offset],
           transfer->partial_write_data, transfer->partial_write_length);

    // CH5xx 只能把 1 写成 0，软件断点替换指令前必须整页读改写
    if (!wchlink_target_ports_flash_rewrite_page(
             transfer->target, page_address, transfer->partial_write_page)
             .ok) {
        return false;
    }

    if (transfer->partial_cache_valid && sector_address == 0u) {
        uint32_t cache_offset = page_address - sector_address;

        memcpy(&transfer->partial_cache[cache_offset],
               transfer->partial_write_page, WCHLINK_TRANSFER_PACKET_CAPACITY);
    }
    return true;
}

static void wchlink_transfer_set_reply(struct wchlink_transfer *transfer,
                                       uint8_t status) {
    transfer->data_reply_status = status;
    transfer->data_reply_pending = true;
}

static uint8_t wchlink_transfer_target_error_code(
    struct rvswd_target_result result) {
    return result.code == 0u ? 0x15u : (uint8_t)result.code;
}

static bool wchlink_transfer_accepts_data_out(
    const struct wchlink_transfer *transfer) {
    return transfer->out_state != WCHLINK_TRANSFER_OUT_IDLE;
}

void wchlink_transfer_init(struct wchlink_transfer *transfer,
                           struct wchlink_target_ports *target) {
    if (transfer == NULL) {
        return;
    }
    memset(transfer, 0, sizeof(*transfer));
    transfer->target = target;
}

void wchlink_transfer_bind_target(struct wchlink_transfer *transfer,
                                  struct wchlink_target_ports *target) {
    if (transfer != NULL) {
        transfer->target = target;
    }
}

void wchlink_transfer_clear_operation(struct wchlink_transfer *transfer) {
    if (transfer == NULL) {
        return;
    }

    // 下载缓存和 OpenOCD Prepare 跨相邻 command 保留，其余状态在新操作前清空
    transfer->in_state = WCHLINK_TRANSFER_IN_IDLE;
    transfer->out_state = WCHLINK_TRANSFER_OUT_IDLE;
    transfer->read_address = 0u;
    transfer->read_remaining = 0u;
    transfer->write_address = 0u;
    transfer->write_remaining = 0u;
    transfer->loader_received = 0u;
    transfer->loader_expected = 0u;
    transfer->loader_variable_length = false;
    transfer->loader_error = 0u;
    transfer->flash_data_received = 0u;
    transfer->flash_transfer_received = 0u;
    transfer->flash_transfer_length = 0u;
    transfer->flash_chunk_length = 0u;
    transfer->flash_loader_mode = 0u;
    transfer->flash_checksum = 0u;
    transfer->loader_ready = false;
    transfer->flash_openocd_mode = false;
    transfer->partial_write_address = 0u;
    transfer->partial_write_length = 0u;
    memset(transfer->partial_write_data, 0,
           sizeof(transfer->partial_write_data));
    transfer->data_reply_pending = false;
    transfer->data_reply_status = 0u;
    transfer->loader_failure_dmi_status = 0u;
    transfer->loader_failure_address = 0u;
    transfer->loader_failure_abstractcs = 0u;
}

void wchlink_transfer_reset(struct wchlink_transfer *transfer) {
    if (transfer == NULL) {
        return;
    }
    transfer->flash_prepare_seen = false;
    wchlink_transfer_clear_operation(transfer);
}

void wchlink_transfer_invalidate_cache(struct wchlink_transfer *transfer) {
    if (transfer != NULL) {
        transfer->partial_cache_valid = false;
    }
}

void wchlink_transfer_prepare_write(struct wchlink_transfer *transfer,
                                    uint32_t address, uint32_t length) {
    if (transfer == NULL) {
        return;
    }
    wchlink_transfer_clear_operation(transfer);
    memset(transfer->partial_cache, 0xff, sizeof(transfer->partial_cache));
    transfer->partial_cache_valid = true;
    transfer->write_address = address;
    transfer->write_remaining = length;
    transfer->flash_chunk_length =
        length > WCHLINK_TRANSFER_CHUNK_CAPACITY ? WCHLINK_TRANSFER_CHUNK_CAPACITY : length;
}

void wchlink_transfer_prepare_read(struct wchlink_transfer *transfer,
                                   uint32_t address, uint32_t length) {
    if (transfer == NULL) {
        return;
    }
    wchlink_transfer_clear_operation(transfer);
    transfer->read_address = address;
    transfer->read_remaining = length;
}

bool wchlink_transfer_start_partial_write(struct wchlink_transfer *transfer,
                                          uint32_t address, uint8_t length) {
    if (transfer == NULL || transfer->target == NULL || length == 0u ||
        !wchlink_target_ports_info(transfer->target).connected) {
        return false;
    }
    transfer->partial_write_address = address;
    transfer->partial_write_length = length;
    transfer->out_state = WCHLINK_TRANSFER_OUT_PARTIAL_WRITE;
    return true;
}

bool wchlink_transfer_start_loader(struct wchlink_transfer *transfer) {
    struct rvswd_target_info target_info;

    if (transfer == NULL || transfer->target == NULL ||
        transfer->write_remaining == 0u) {
        return false;
    }
    target_info = wchlink_target_ports_info(transfer->target);
    if (!target_info.connected) {
        return false;
    }

    transfer->out_state = WCHLINK_TRANSFER_OUT_LOADER;
    transfer->loader_received = 0u;
    transfer->loader_error = 0u;
    transfer->loader_failure_dmi_status = 0u;
    transfer->loader_failure_address = 0u;
    transfer->loader_failure_abstractcs = 0u;
    transfer->loader_ready = false;
    // 变长 loader 由主机结束命令确定实际长度，固定 loader 收满后立即停止 OUT
    transfer->loader_variable_length = target_info.loader_variable_length;
    transfer->loader_expected = target_info.loader_download_limit;
    if (transfer->loader_expected == 0u) {
        transfer->out_state = WCHLINK_TRANSFER_OUT_IDLE;
        return false;
    }
    return true;
}

void wchlink_transfer_mark_flash_prepare(struct wchlink_transfer *transfer) {
    if (transfer != NULL) {
        transfer->flash_prepare_seen = true;
    }
}

struct wchlink_transfer_finish_result wchlink_transfer_finish_loader(
    struct wchlink_transfer *transfer, uint8_t command) {
    struct wchlink_transfer_finish_result finish = {
        .status = WCHLINK_TRANSFER_FINISH_INCOMPLETE,
        .target_value = 0xffffffffu,
    };
    struct rvswd_target_loader_execute execute = {0};
    struct rvswd_target_info target_info;
    struct rvswd_target_result result;
    bool success;

    if (transfer == NULL || transfer->target == NULL) {
        return finish;
    }
    target_info = wchlink_target_ports_info(transfer->target);
    execute.mode = target_info.loader_initialize_mode;

    // loader 数据到此结束，后续数据只能在初始化成功后进入 Flash 状态
    transfer->out_state = WCHLINK_TRANSFER_OUT_IDLE;
    if (transfer->loader_error != 0u) {
        finish.status = WCHLINK_TRANSFER_FINISH_LOADER_ERROR;
        finish.loader_error = transfer->loader_error;
        finish.dmi_status = transfer->loader_failure_dmi_status;
        finish.address = transfer->loader_failure_address;
        finish.abstractcs = transfer->loader_failure_abstractcs;
        return finish;
    }
    if ((!transfer->loader_variable_length && !transfer->loader_ready) ||
        (transfer->loader_variable_length && transfer->loader_received == 0u)) {
        return finish;
    }

    // 目标 profile 决定是否需要第二次初始化以及 Prepare 后的执行 mode
    result = wchlink_target_ports_execute_loader(transfer->target, &execute);
    finish.target_value = result.value;
    success = result.ok && result.value == 0u;
    if (success && target_info.loader_repeat_initialize) {
        execute.mode = transfer->flash_prepare_seen
                           ? target_info.loader_prepared_mode
                           : target_info.loader_initialize_mode;
        result =
            wchlink_target_ports_execute_loader(transfer->target, &execute);
        finish.target_value = result.value;
        success = result.ok && result.value == 0u;
    }
    if (!success) {
        transfer->loader_ready = false;
        finish.status = WCHLINK_TRANSFER_FINISH_TARGET_ERROR;
        return finish;
    }

    transfer->loader_ready = true;
    // OpenOCD 路径和 Prepare 路径的 mode 由目标 loader profile 提供
    transfer->flash_loader_mode =
        command == 0x0bu
            ? target_info.loader_verify_mode
        : transfer->flash_prepare_seen
            ? target_info.loader_program_verify_mode
            : target_info.loader_program_mode;
    transfer->out_state = WCHLINK_TRANSFER_OUT_FLASH;
    transfer->flash_openocd_mode = true;
    transfer->flash_data_received = 0u;
    transfer->flash_transfer_received = 0u;
    transfer->flash_transfer_length = 0u;
    transfer->flash_checksum = 0u;
    transfer->flash_prepare_seen = false;
    finish.status = WCHLINK_TRANSFER_FINISH_READY;
    return finish;
}

bool wchlink_transfer_start_flash(struct wchlink_transfer *transfer,
                                  uint8_t command) {
    struct rvswd_target_info target_info;

    if (transfer == NULL || !transfer->loader_ready ||
        transfer->write_remaining == 0u ||
        (command != 0x02u && command != 0x03u && command != 0x04u)) {
        return false;
    }
    target_info = wchlink_target_ports_info(transfer->target);
    transfer->out_state = WCHLINK_TRANSFER_OUT_FLASH;
    transfer->flash_data_received = 0u;
    transfer->flash_transfer_received = 0u;
    transfer->flash_transfer_length = 0u;
    transfer->flash_checksum = 0u;
    transfer->flash_openocd_mode = false;
    transfer->flash_loader_mode =
        command == 0x02u
            ? target_info.loader_program_mode
        : command == 0x03u
            ? target_info.loader_verify_mode
            : target_info.loader_program_verify_mode;
    return true;
}

void wchlink_transfer_abort(struct wchlink_transfer *transfer) {
    if (transfer == NULL) {
        return;
    }
    transfer->flash_prepare_seen = false;
    wchlink_transfer_clear_operation(transfer);
}

void wchlink_transfer_begin_read(struct wchlink_transfer *transfer) {
    if (transfer != NULL && transfer->target != NULL &&
        wchlink_target_ports_info(transfer->target).connected &&
        transfer->read_remaining != 0u) {
        // 地址帧只准备游标，Flash 0x0c 命令到达后才开放 data IN
        transfer->in_state = WCHLINK_TRANSFER_IN_READING;
    }
}

enum wchlink_transfer_io_request wchlink_transfer_next_io(
    const struct wchlink_transfer *transfer) {
    unsigned int request = WCHLINK_TRANSFER_IO_NONE;

    if (transfer == NULL) {
        return WCHLINK_TRANSFER_IO_NONE;
    }
    if (transfer->data_reply_pending ||
        transfer->in_state == WCHLINK_TRANSFER_IN_READING) {
        request |= WCHLINK_TRANSFER_IO_DATA_IN;
    }
    if (wchlink_transfer_accepts_data_out(transfer)) {
        request |= WCHLINK_TRANSFER_IO_DATA_OUT;
    }
    return (enum wchlink_transfer_io_request)request;
}

void wchlink_transfer_write_data(struct wchlink_transfer *transfer,
                                 const uint8_t *data, size_t length) {
    struct rvswd_target_info target_info;

    if (transfer == NULL || transfer->target == NULL || data == NULL ||
        length == 0u || !wchlink_transfer_accepts_data_out(transfer)) {
        return;
    }
    target_info = wchlink_target_ports_info(transfer->target);

    if (transfer->out_state == WCHLINK_TRANSFER_OUT_LOADER) {
        struct rvswd_target_result result;

        if (transfer->loader_error == 0u) {
            result = wchlink_target_ports_write_loader_code(
                transfer->target, transfer->loader_received, data,
                (uint32_t)length);
            if (!result.ok) {
                transfer->loader_error =
                    wchlink_transfer_target_error_code(result);
                transfer->loader_failure_dmi_status = result.dmi_status;
                transfer->loader_failure_address = result.address;
                transfer->loader_failure_abstractcs = result.abstractcs;
            }
        }

        // 固定长度 loader 收满后停止 OUT，CH5xx 等待后续结束命令
        if (!transfer->loader_variable_length &&
            transfer->loader_received + length >= transfer->loader_expected) {
            transfer->loader_received = transfer->loader_expected;
            transfer->out_state = WCHLINK_TRANSFER_OUT_IDLE;
            transfer->loader_ready = transfer->loader_error == 0u;
        } else {
            uint32_t remaining =
                transfer->loader_expected - transfer->loader_received;

            transfer->loader_received +=
                length > remaining ? remaining : (uint32_t)length;
        }
        if (!transfer->loader_variable_length && transfer->loader_error == 0u &&
            transfer->loader_received >= transfer->loader_expected) {
            transfer->loader_ready = true;
        }
        return;
    }

    if (transfer->out_state == WCHLINK_TRANSFER_OUT_PARTIAL_WRITE) {
        bool success;

        if (length != transfer->partial_write_length ||
            length > sizeof(transfer->partial_write_data)) {
            transfer->out_state = WCHLINK_TRANSFER_OUT_IDLE;
            wchlink_transfer_set_reply(transfer, 0x15u);
            return;
        }
        memcpy(transfer->partial_write_data, data, length);
        success = wchlink_transfer_rewrite_partial_page(transfer);
        transfer->out_state = WCHLINK_TRANSFER_OUT_IDLE;
        wchlink_transfer_set_reply(
            transfer, success ? WCHLINK_PARTIAL_WRITE_REPLY_OK
                              : WCHLINK_PARTIAL_WRITE_REPLY_FAILED);
        return;
    }

    if (transfer->out_state == WCHLINK_TRANSFER_OUT_FLASH &&
        length <= WCHLINK_TRANSFER_PACKET_CAPACITY && (length & 3u) == 0u &&
        transfer->write_remaining != 0u) {
        uint32_t transfer_length;
        uint32_t transfer_remaining;
        size_t write_length = 0u;

        if (transfer->flash_transfer_length == 0u) {
            if (transfer->flash_openocd_mode) {
                // 新版 OpenOCD 发送对齐段长，旧版仍发送固定 4096 字节
                transfer->flash_transfer_length =
                    (transfer->flash_chunk_length & 0xffu) == 0u
                        ? transfer->flash_chunk_length
                        : WCHLINK_TRANSFER_CHUNK_CAPACITY;
            } else {
                // MRS 和 wlink 以首个数据包长度对齐尾包
                transfer->flash_transfer_length =
                    ((transfer->flash_chunk_length + (uint32_t)length - 1u) /
                     (uint32_t)length) *
                    (uint32_t)length;
            }
        }
        transfer_length = transfer->flash_transfer_length;
        transfer_remaining =
            transfer_length - transfer->flash_transfer_received;
        if ((uint32_t)length > transfer_remaining) {
            // 主机包不能跨越本次数据块边界，超长包只消费边界内部分
            length = (size_t)transfer_remaining;
        }
        if (transfer->flash_transfer_received < transfer->flash_chunk_length) {
            write_length = transfer->flash_chunk_length -
                           transfer->flash_transfer_received;
            if (write_length > length) {
                write_length = length;
            }
            if (write_length != 0u) {
                if (target_info.memory_streaming) {
                    // 连续写入目标每个 4 KiB chunk 只建立一次 RVSWD 上下文
                    memcpy(&transfer->flash_chunk_data
                                [transfer->flash_transfer_received],
                           data, write_length);
                } else {
                    struct rvswd_target_result result =
                        wchlink_target_ports_write_loader_data(
                            transfer->target,
                            transfer->flash_transfer_received,
                            data, (uint32_t)write_length);

                    if (!result.ok) {
                        transfer->out_state = WCHLINK_TRANSFER_OUT_IDLE;
                        wchlink_transfer_set_reply(
                            transfer,
                            wchlink_transfer_target_error_code(result));
                        return;
                    }
                    wchlink_transfer_cache_range(
                        transfer,
                        transfer->write_address +
                            transfer->flash_transfer_received,
                        data, (uint32_t)write_length);
                }
                transfer->flash_checksum = wchlink_transfer_checksum_add(
                    transfer->flash_checksum, data, write_length);
                transfer->flash_data_received += (uint32_t)write_length;
            }
        }
        transfer->flash_transfer_received += (uint32_t)length;
        if (transfer->flash_transfer_received < transfer_length) {
            return;
        }

        if (target_info.memory_streaming &&
            transfer->flash_data_received != 0u) {
            struct rvswd_target_result result =
                wchlink_target_ports_write_loader_data(
                    transfer->target, 0u,
                    transfer->flash_chunk_data, transfer->flash_chunk_length);

            if (!result.ok) {
                transfer->out_state = WCHLINK_TRANSFER_OUT_IDLE;
                wchlink_transfer_set_reply(
                    transfer, wchlink_transfer_target_error_code(result));
                return;
            }
        }

        // loader 读取完整页，实际数据不足的尾部必须显式写入擦除态
        {
            uint32_t padded_length =
                wchlink_transfer_padded_data_length(transfer);
            struct rvswd_target_result result =
                rvswd_target_result_success();

            if (transfer->flash_data_received < padded_length) {
                result = wchlink_transfer_write_padding(
                    transfer, transfer->flash_data_received, padded_length);
            }
            if (!result.ok) {
                transfer->out_state = WCHLINK_TRANSFER_OUT_IDLE;
                wchlink_transfer_set_reply(
                    transfer, wchlink_transfer_target_error_code(result));
                return;
            }
            transfer->flash_data_received = padded_length;
        }
        {
            struct rvswd_target_loader_execute execute = {
                .mode = transfer->flash_loader_mode,
                .address = transfer->write_address,
                .length = transfer->flash_chunk_length,
                .checksum = transfer->flash_checksum,
                .write_checksum =
                    (transfer->flash_loader_mode &
                     target_info.loader_checksum_mode_mask) != 0u,
            };
            uint32_t target_value = 0xffffffffu;
            bool success;
            struct rvswd_target_result result;

            // 编程块越过 Code Flash 边界时前置拒绝，loader 在物理边界外会挂死
            if (target_info.code_flash_size != 0u &&
                (target_info.code_flash_base == 0u ||
                 execute.address < target_info.code_flash_base ||
                 execute.address >= target_info.code_flash_base +
                                        target_info.code_flash_size ||
                 execute.length > target_info.code_flash_size ||
                 execute.address - target_info.code_flash_base >
                     target_info.code_flash_size - execute.length)) {
                wchlink_transfer_set_reply(transfer, 0x03u);
                transfer->write_address = 0u;
                transfer->write_remaining = 0u;
                transfer->out_state = WCHLINK_TRANSFER_OUT_IDLE;
                return;
            }

            // target port 按锁定 profile 写入可选 checksum 并执行 loader
            result =
                wchlink_target_ports_execute_loader(transfer->target, &execute);
            success = result.ok;
            target_value = result.value;

            // LinkE 将 loader 的三个标准返回值转换为数据端点状态
            if (success && target_value == 0u) {
                wchlink_transfer_set_reply(transfer, 0x04u);
            } else if (success && target_value == 8u) {
                wchlink_transfer_set_reply(transfer, 0x03u);
            } else if (success && target_value == 16u) {
                wchlink_transfer_set_reply(transfer, 0x05u);
            } else {
                wchlink_transfer_set_reply(transfer, (uint8_t)target_value);
            }
            if (success &&
                transfer->write_remaining > transfer->flash_chunk_length) {
                transfer->write_address += transfer->flash_chunk_length;
                transfer->write_remaining -= transfer->flash_chunk_length;
                transfer->flash_data_received = 0u;
                transfer->flash_transfer_received = 0u;
                transfer->flash_transfer_length = 0u;
                transfer->flash_checksum = 0u;
                transfer->flash_chunk_length =
                    transfer->write_remaining > WCHLINK_TRANSFER_CHUNK_CAPACITY
                        ? WCHLINK_TRANSFER_CHUNK_CAPACITY
                        : transfer->write_remaining;
            } else {
                transfer->write_address = 0u;
                transfer->write_remaining = 0u;
                transfer->out_state = WCHLINK_TRANSFER_OUT_IDLE;
            }
        }
    }
}

bool wchlink_transfer_take_reply_status(struct wchlink_transfer *transfer,
                                        uint8_t *status) {
    if (transfer == NULL || status == NULL || !transfer->data_reply_pending) {
        return false;
    }
    *status = transfer->data_reply_status;
    transfer->data_reply_pending = false;
    return true;
}

size_t wchlink_transfer_read_data(struct wchlink_transfer *transfer,
                                  uint8_t *data, size_t capacity) {
    size_t produced = 0u;

    if (transfer == NULL || transfer->target == NULL || data == NULL ||
        capacity < 4u || transfer->in_state != WCHLINK_TRANSFER_IN_READING) {
        return 0u;
    }

    while (produced + 4u <= capacity && transfer->read_remaining >= 4u) {
        struct rvswd_target_result result =
            wchlink_target_ports_read_memory32(transfer->target,
                                               transfer->read_address);
        uint32_t value = result.value;

        if (!result.ok) {
            transfer->in_state = WCHLINK_TRANSFER_IN_IDLE;
            transfer->read_remaining = 0u;
            return 0u;
        }

        // WCH-Link 数据端点按大端字节发送，wlink 主机随后按字反转
        data[produced + 0u] = (uint8_t)(value >> 24u);
        data[produced + 1u] = (uint8_t)(value >> 16u);
        data[produced + 2u] = (uint8_t)(value >> 8u);
        data[produced + 3u] = (uint8_t)value;
        produced += 4u;
        transfer->read_address += 4u;
        transfer->read_remaining -= 4u;
    }

    if (transfer->read_remaining == 0u) {
        transfer->in_state = WCHLINK_TRANSFER_IN_IDLE;
    }
    return produced;
}
