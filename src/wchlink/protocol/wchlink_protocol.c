#include "wchlink_protocol.h"

#include "bsp/bsp_delay.h"
#include "drv/drv_dp_pullup.h"
#include "drv/drv_power_switch.h"
#include "rvswd_gpio.h"
#include "wchlink_family.h"
#include "wchlink_wire.h"

#include <string.h>

struct wchlink_loader_layout {
    uint32_t entry;
    uint32_t data;
    uint32_t stack_top;
};

static const struct wchlink_loader_layout wchlink_loader_layout_default = {
    .entry = 0x20000000u,
    .data = 0x20001000u,
    .stack_top = 0x20005000u,
};

static const struct wchlink_loader_layout wchlink_loader_layout_ch59x = {
    .entry = 0x20004000u,
    .data = 0x20005000u,
    .stack_top = 0x20007000u,
};

static bool wchlink_connected;
static bool wchlink_ch5xx_info_query_seen;
static bool wchlink_isp_request_pending;
static uint32_t wchlink_read_address;
static uint32_t wchlink_read_remaining;
static bool wchlink_read_active;
static uint32_t wchlink_write_address;
static uint32_t wchlink_write_remaining;
static uint8_t wchlink_write_mode;
static uint32_t wchlink_loader_received;
static uint32_t wchlink_loader_expected = WCHLINK_LOADER_DEFAULT_SIZE;
static bool wchlink_loader_variable_length;
static uint8_t wchlink_loader_error;
static uint32_t wchlink_flash_data_received;
static uint32_t wchlink_flash_transfer_received;
static uint32_t wchlink_flash_transfer_length;
static uint32_t wchlink_flash_chunk_length;
static uint32_t wchlink_flash_loader_mode;
static uint32_t wchlink_flash_checksum;
static bool wchlink_loader_ready;
static bool wchlink_flash_openocd_mode;
static bool wchlink_flash_prepare_seen;
static uint32_t wchlink_partial_write_address;
static uint8_t wchlink_partial_write_length;
static uint8_t wchlink_partial_write_data[WCHLINK_FLASH_PACKET_SIZE];
static uint8_t wchlink_partial_write_page[WCHLINK_FLASH_PACKET_SIZE];
static uint8_t wchlink_partial_cache[WCHLINK_FLASH_CHUNK_SIZE];
static uint8_t wchlink_flash_chunk_data[WCHLINK_FLASH_CHUNK_SIZE];
static bool wchlink_partial_cache_valid;
static bool wchlink_data_reply_pending;
static uint8_t wchlink_data_reply_status;
static uint8_t wchlink_loader_failure_dmi_status;
static uint32_t wchlink_loader_failure_address;
static uint32_t wchlink_loader_failure_abstractcs;
static uint8_t wchlink_flash_padding[WCHLINK_FLASH_PACKET_SIZE];

static uint32_t wchlink_checksum_add(uint32_t checksum, const uint8_t *data,
                                     size_t length) {
    for (size_t offset = 0u; offset < length; offset += 4u) {
        checksum += (uint32_t)data[offset + 0u] |
                    ((uint32_t)data[offset + 1u] << 8u) |
                    ((uint32_t)data[offset + 2u] << 16u) |
                    ((uint32_t)data[offset + 3u] << 24u);
    }
    return checksum;
}

static bool wchlink_target_uses_ch5xx_loader(void) {
    uint8_t family = rvswd_gpio_target_wchlink_family();
    return family == WCHLINK_TARGET_FAMILY_CH58X ||
           family == WCHLINK_TARGET_FAMILY_CH59X;
}

static bool wchlink_target_uses_l103_loader(void) {
    return rvswd_gpio_target_wchlink_family() == WCHLINK_TARGET_FAMILY_L103;
}

static bool wchlink_target_supports_memory_streaming(void) {
    return rvswd_gpio_target_supports_memory_streaming();
}

static void wchlink_clear_transfer_state(void) {
    wchlink_read_address = 0u;
    wchlink_read_remaining = 0u;
    wchlink_read_active = false;
    wchlink_write_address = 0u;
    wchlink_write_remaining = 0u;
    wchlink_write_mode = 0u;
    wchlink_loader_received = 0u;
    wchlink_loader_expected = WCHLINK_LOADER_DEFAULT_SIZE;
    wchlink_loader_variable_length = false;
    wchlink_loader_error = 0u;
    wchlink_flash_data_received = 0u;
    wchlink_flash_transfer_received = 0u;
    wchlink_flash_transfer_length = 0u;
    wchlink_flash_chunk_length = 0u;
    wchlink_flash_loader_mode = 0u;
    wchlink_flash_checksum = 0u;
    wchlink_loader_ready = false;
    wchlink_flash_openocd_mode = false;
    wchlink_partial_write_address = 0u;
    wchlink_partial_write_length = 0u;
    memset(wchlink_partial_write_data, 0, sizeof(wchlink_partial_write_data));
    wchlink_data_reply_pending = false;
    wchlink_data_reply_status = 0u;
    wchlink_loader_failure_dmi_status = 0u;
    wchlink_loader_failure_address = 0u;
    wchlink_loader_failure_abstractcs = 0u;
}

static const struct wchlink_loader_layout *wchlink_target_loader_layout(void) {
    if (wchlink_target_uses_ch5xx_loader()) {
        return &wchlink_loader_layout_ch59x;
    }
    return &wchlink_loader_layout_default;
}

static uint32_t wchlink_flash_padded_data_length(void) {
    uint32_t length = wchlink_flash_chunk_length;

    // CH5xx loader 以完整 256 字节页参与校验，尾页需要保持擦除态
    if (wchlink_target_uses_ch5xx_loader()) {
        length = (length + (WCHLINK_CH5XX_LOADER_PAGE_SIZE - 1u)) &
                 ~(WCHLINK_CH5XX_LOADER_PAGE_SIZE - 1u);
    }
    return length;
}

static bool wchlink_flash_write_padding(const struct wchlink_loader_layout *layout,
                                        uint32_t from, uint32_t to) {
    while (from < to) {
        uint32_t length = to - from;

        if (length > sizeof(wchlink_flash_padding)) {
            length = sizeof(wchlink_flash_padding);
        }
        memset(wchlink_flash_padding, 0xff, length);
        if (!rvswd_gpio_write_memory(layout->data + from, wchlink_flash_padding,
                                     length)) {
            return false;
        }
        wchlink_flash_checksum =
            wchlink_checksum_add(wchlink_flash_checksum, wchlink_flash_padding,
                                 length);
        from += length;
    }
    return true;
}

static void wchlink_partial_cache_range(uint32_t address, const uint8_t *data,
                                        uint32_t length) {
    uint32_t start = address;
    uint32_t end = address + length;

    if (start >= WCHLINK_FLASH_CHUNK_SIZE || end <= start) {
        return;
    }
    if (end > WCHLINK_FLASH_CHUNK_SIZE) {
        end = WCHLINK_FLASH_CHUNK_SIZE;
    }
    memcpy(&wchlink_partial_cache[start], data, end - start);
}

static bool wchlink_partial_write_flash_page(void) {
    uint32_t page_address = wchlink_partial_write_address &
                            ~(WCHLINK_FLASH_PACKET_SIZE - 1u);
    uint32_t sector_address = wchlink_partial_write_address &
                              ~(WCHLINK_FLASH_CHUNK_SIZE - 1u);
    uint32_t page_offset = wchlink_partial_write_address &
                           (WCHLINK_FLASH_PACKET_SIZE - 1u);

    if (!wchlink_target_uses_ch5xx_loader() ||
        page_offset + wchlink_partial_write_length > WCHLINK_FLASH_PACKET_SIZE) {
        return false;
    }

    if (wchlink_partial_cache_valid && sector_address == 0u) {
        uint32_t cache_offset = page_address - sector_address;

        memcpy(wchlink_partial_write_page,
               &wchlink_partial_cache[cache_offset],
               WCHLINK_FLASH_PACKET_SIZE);
    } else {
        // 没有下载缓存时只回读目标页，不扩展为整个 4 KiB 扇区
        for (uint32_t offset = 0u; offset < WCHLINK_FLASH_PACKET_SIZE;
             offset += 4u) {
            uint32_t value;

            if (!rvswd_gpio_read_memory32(page_address + offset, &value)) {
                return false;
            }
            wchlink_partial_write_page[offset + 0u] = (uint8_t)value;
            wchlink_partial_write_page[offset + 1u] = (uint8_t)(value >> 8u);
            wchlink_partial_write_page[offset + 2u] = (uint8_t)(value >> 16u);
            wchlink_partial_write_page[offset + 3u] = (uint8_t)(value >> 24u);
        }
    }
    memcpy(&wchlink_partial_write_page[page_offset],
           wchlink_partial_write_data, wchlink_partial_write_length);

    // CH5xx 只能把 1 写成 0，软件断点替换指令前必须整页读改写
    if (!rvswd_gpio_flash_rewrite_page(page_address, wchlink_partial_write_page)) {
        return false;
    }

    if (wchlink_partial_cache_valid && sector_address == 0u) {
        uint32_t cache_offset = page_address - sector_address;

        memcpy(&wchlink_partial_cache[cache_offset],
               wchlink_partial_write_page,
               WCHLINK_FLASH_PACKET_SIZE);
    }
    return true;
}

static size_t wchlink_ack(uint8_t *response, size_t capacity, uint8_t family) {
    if (capacity < 4u) {
        return 0u;
    }
    response[0] = WCHLINK_REPLY_PREFIX;
    response[1] = family;
    response[2] = 1u;
    response[3] = 0u;
    return 4u;
}

static size_t wchlink_unsupported(uint8_t *response, size_t capacity, uint8_t family) {
    if (capacity < 4u) {
        return 0u;
    }
    response[0] = WCHLINK_COMMAND_PREFIX;
    response[1] = family;
    response[2] = 1u;
    response[3] = 2u;
    return 4u;
}

static size_t wchlink_target_error(uint8_t *response, size_t capacity) {
    if (capacity < 4u) {
        return 0u;
    }
    response[0] = WCHLINK_COMMAND_PREFIX;
    response[1] = 0x55u;
    response[2] = 1u;
    response[3] = (uint8_t)rvswd_gpio_flash_last_error();
    return 4u;
}

static size_t wchlink_command_reply(uint8_t *response, size_t capacity,
                                    uint8_t family, uint8_t command) {
    if (capacity < 4u) {
        return 0u;
    }
    response[0] = WCHLINK_REPLY_PREFIX;
    response[1] = family;
    response[2] = 1u;
    response[3] = command;
    return 4u;
}

static size_t wchlink_identity(uint8_t *response, size_t capacity) {
    if (capacity < 7u) {
        return 0u;
    }
    response[0] = WCHLINK_REPLY_PREFIX;
    response[1] = WCHLINK_FAMILY_CONTROL;
    response[2] = 4u;
    // 官方 LinkE 与 MRS 版本检查使用 3.3 身份，CH592 不改变 Link 固件版本
    response[3] = 3u;
    response[4] = 3u;
    response[5] = 0x12u;
    response[6] = 0u;
    return 7u;
}

static size_t wchlink_connect_reply(uint8_t *response, size_t capacity, bool connected) {
    uint32_t chip_id = rvswd_gpio_target_chip_id();
    uint8_t chip_family = rvswd_gpio_target_wchlink_family();
    bool unsupported_chip = connected && chip_family == 0u;

    if (unsupported_chip) {
        connected = false;
    }
    if (!connected) {
        if (capacity < 4u) {
            return 0u;
        }
        response[0] = WCHLINK_COMMAND_PREFIX;
        response[1] = 0x55u;
        response[2] = 1u;
        response[3] = unsupported_chip ? 0x30u : rvswd_gpio_connect_last_error();
        return 4u;
    }
    if (capacity < 8u) {
        return 0u;
    }
    response[0] = WCHLINK_REPLY_PREFIX;
    response[1] = WCHLINK_FAMILY_CONTROL;
    response[2] = 5u;
    response[3] = chip_family;
    response[4] = (uint8_t)(chip_id >> 24u);
    response[5] = (uint8_t)(chip_id >> 16u);
    response[6] = (uint8_t)(chip_id >> 8u);
    response[7] = (uint8_t)chip_id;
    return 8u;
}

static size_t wchlink_chip_info(uint8_t *response, size_t capacity) {
    uint32_t flash_size = 0u;
    uint32_t uid_low = 0u;
    uint32_t uid_high = 0u;
    uint32_t uid_tail = 0u;
    uint32_t chip_id = rvswd_gpio_target_chip_id();

    if (capacity < 20u) {
        return 0u;
    }

    if (wchlink_target_uses_ch5xx_loader()) {
        // MRS 的 CH5xx FlashOperation 路径固定读取 20 字节，LinkE 将 ChipID 放在第 4 字节
        memset(response, 0, 20u);
        response[0] = WCHLINK_REPLY_PREFIX;
        response[1] = WCHLINK_FAMILY_CONTROL;
        response[2] = 1u;
        response[3] = 0xffu;
        response[4] = (uint8_t)(chip_id >> 24u);
        return 20u;
    }

    if (!rvswd_gpio_read_memory32(0x1ffff7e0u, &flash_size) ||
        !rvswd_gpio_read_memory32(0x1ffff7e8u, &uid_low) ||
        !rvswd_gpio_read_memory32(0x1ffff7ecu, &uid_high) ||
        !rvswd_gpio_read_memory32(0x1ffff7f0u, &uid_tail)) {
        return 0u;
    }

    // 芯片信息查询使用无帧头的 20 字节原始回复
    response[0] = 0xffu;
    response[1] = 0xffu;
    response[2] = (uint8_t)(flash_size >> 8u);
    response[3] = (uint8_t)flash_size;
    response[4] = (uint8_t)(uid_low >> 24u);
    response[5] = (uint8_t)(uid_low >> 16u);
    response[6] = (uint8_t)(uid_low >> 8u);
    response[7] = (uint8_t)uid_low;
    response[8] = (uint8_t)(uid_high >> 24u);
    response[9] = (uint8_t)(uid_high >> 16u);
    response[10] = (uint8_t)(uid_high >> 8u);
    response[11] = (uint8_t)uid_high;
    response[12] = (uint8_t)(uid_tail >> 24u);
    response[13] = (uint8_t)(uid_tail >> 16u);
    response[14] = (uint8_t)(uid_tail >> 8u);
    response[15] = (uint8_t)uid_tail;
    response[16] = (uint8_t)(chip_id >> 24u);
    response[17] = (uint8_t)(chip_id >> 16u);
    response[18] = (uint8_t)(chip_id >> 8u);
    response[19] = (uint8_t)chip_id;
    return 20u;
}

static size_t wchlink_dmi(const uint8_t *request, uint8_t *response, size_t capacity) {
    uint8_t address;
    uint32_t data;
    bool success;

    if (capacity < 9u) {
        return 0u;
    }
    address = request[3];
    data = ((uint32_t)request[4] << 24u) | ((uint32_t)request[5] << 16u) |
           ((uint32_t)request[6] << 8u) | request[7];
    if (request[8] == 1u) {
        success = rvswd_gpio_read_dmi(address, &data);
    } else if (request[8] == 2u) {
        success = rvswd_gpio_write_dmi(address, data);
    } else {
        success = false;
    }

    response[0] = WCHLINK_REPLY_PREFIX;
    response[1] = WCHLINK_FAMILY_DMI;
    response[2] = 6u;
    response[3] = address;
    response[4] = (uint8_t)(data >> 24u);
    response[5] = (uint8_t)(data >> 16u);
    response[6] = (uint8_t)(data >> 8u);
    response[7] = (uint8_t)data;
    response[8] = success ? 0u : (rvswd_gpio_dmi_failure_retryable() ? 3u : 2u);
    return 9u;
}

static size_t wchlink_config(const uint8_t *request, size_t request_length,
                             uint8_t *response, size_t capacity) {
    bool protected;
    uint8_t result;

    if (request_length < 4u || capacity < 4u || !wchlink_connected) {
        return wchlink_unsupported(response, capacity, WCHLINK_FAMILY_CONFIG);
    }

    switch (request[3]) {
        case WCHLINK_CONFIG_READ_PROTECTION:
            if (wchlink_target_uses_ch5xx_loader()) {
                // CH5xx 不使用 CH32 Option Byte，LinkE 将保护查询报告为未保护
                result = WCHLINK_CONFIG_READ_UNPROTECTED;
                break;
            }
            if (!rvswd_gpio_flash_read_protected(&protected)) {
                return wchlink_target_error(response, capacity);
            }
            result = protected ? WCHLINK_CONFIG_READ_PROTECTED
                               : WCHLINK_CONFIG_READ_UNPROTECTED;
            break;
        case WCHLINK_CONFIG_DISABLE_PROTECTION:
        case WCHLINK_CONFIG_ENABLE_PROTECTION:
            // 扩展帧还包含 USER 和 WRP 配置，不能按基础保护命令处理
            if (request_length != 4u ||
                !rvswd_gpio_flash_set_read_protected(
                    request[3] == WCHLINK_CONFIG_ENABLE_PROTECTION)) {
                return request_length == 4u
                           ? wchlink_target_error(response, capacity)
                           : wchlink_unsupported(response, capacity,
                                                 WCHLINK_FAMILY_CONFIG);
            }
            result = request[3];
            break;
        case WCHLINK_CONFIG_WRITE_PROTECTION:
            if (!rvswd_gpio_flash_write_protected(&protected)) {
                return wchlink_target_error(response, capacity);
            }
            result = protected ? WCHLINK_CONFIG_WRITE_PROTECTED
                               : WCHLINK_CONFIG_WRITE_UNPROTECTED;
            break;
        default:
            return wchlink_unsupported(response, capacity, WCHLINK_FAMILY_CONFIG);
    }

    response[0] = WCHLINK_REPLY_PREFIX;
    response[1] = WCHLINK_FAMILY_CONFIG;
    response[2] = 1u;
    response[3] = result;
    return 4u;
}

void wchlink_protocol_reset(void) {
    // 失败连接也会配置调试引脚，所有会话复位都必须释放总线
    rvswd_gpio_disconnect();
    wchlink_connected = false;
    wchlink_ch5xx_info_query_seen = false;
    wchlink_isp_request_pending = false;
    wchlink_flash_prepare_seen = false;
    wchlink_clear_transfer_state();
}

bool wchlink_protocol_take_isp_request(void) {
    bool pending = wchlink_isp_request_pending;

    wchlink_isp_request_pending = false;
    return pending;
}

bool wchlink_protocol_is_connected(void) {
    return wchlink_connected;
}

void wchlink_protocol_begin_data_read(void) {
    if (wchlink_connected && wchlink_read_remaining != 0u) {
        wchlink_read_active = true;
    }
}

bool wchlink_protocol_data_read_active(void) {
    return wchlink_read_active;
}

bool wchlink_protocol_data_write_active(void) {
    return wchlink_write_mode != 0u;
}

void wchlink_protocol_write_data(const uint8_t *data, size_t length) {
    const struct wchlink_loader_layout *layout;

    if (data == NULL || length == 0u || wchlink_write_mode == 0u) {
        return;
    }

    if (wchlink_write_mode == 1u) {
        layout = wchlink_target_loader_layout();
        if (wchlink_loader_error == 0u) {
            if (length > WCHLINK_FLASH_PACKET_SIZE ||
                length > wchlink_loader_expected - wchlink_loader_received) {
                // 长度错误属于 USB 会话状态异常，不读取陈旧的 RVSWD 诊断信息
                wchlink_loader_error = 0xefu;
                wchlink_loader_failure_address =
                    layout->entry + wchlink_loader_received;
                wchlink_loader_failure_abstractcs = 0xffffffffu;
            } else if (!rvswd_gpio_write_memory(
                           layout->entry + wchlink_loader_received,
                           data, (uint32_t)length)) {
                wchlink_loader_error = rvswd_gpio_memory_last_error();
                wchlink_loader_failure_dmi_status =
                    rvswd_gpio_memory_failure_dmi_status();
                wchlink_loader_failure_address =
                    rvswd_gpio_memory_failure_address();
                wchlink_loader_failure_abstractcs =
                    rvswd_gpio_memory_failure_abstractcs();
                if (wchlink_loader_error == 0u) {
                    wchlink_loader_error = 0x15u;
                }
            }
        }

        // CH59x loader 的实际长度随上位机实现变化，结束标志是后续的 0x07 命令
        if (!wchlink_loader_variable_length &&
            wchlink_loader_received + length >= wchlink_loader_expected) {
            wchlink_loader_received = wchlink_loader_expected;
            wchlink_write_mode = 0u;
            wchlink_loader_ready = wchlink_loader_error == 0u;
        } else {
            uint32_t remaining = wchlink_loader_expected - wchlink_loader_received;

            wchlink_loader_received +=
                length > remaining ? remaining : (uint32_t)length;
        }
        if (!wchlink_loader_variable_length && wchlink_loader_error == 0u &&
            wchlink_loader_received >= wchlink_loader_expected) {
            wchlink_loader_ready = true;
        }
        return;
    }

    if (wchlink_write_mode == 3u) {
        bool success;

        if (length != wchlink_partial_write_length ||
            length > sizeof(wchlink_partial_write_data)) {
            wchlink_write_mode = 0u;
            wchlink_data_reply_status = 0x15u;
            wchlink_data_reply_pending = true;
            return;
        }
        memcpy(wchlink_partial_write_data, data, length);
        success = wchlink_partial_write_flash_page();
        wchlink_write_mode = 0u;
        wchlink_data_reply_status = success ? WCHLINK_PARTIAL_WRITE_REPLY_OK
                                            : WCHLINK_PARTIAL_WRITE_REPLY_FAILED;
        wchlink_data_reply_pending = true;
        return;
    }

    if (wchlink_write_mode == 2u && length <= WCHLINK_FLASH_PACKET_SIZE &&
        (length & 3u) == 0u && wchlink_write_remaining != 0u) {
        uint32_t transfer_length;
        uint32_t transfer_remaining;
        size_t write_length = 0u;

        layout = wchlink_target_loader_layout();
        if (wchlink_flash_transfer_length == 0u) {
            if (wchlink_flash_openocd_mode) {
                // MRS 内置 OpenOCD 先把段长对齐到 256 字节，再按该长度发送
                // 旧版 OpenOCD 仍声明原始段长，此时保留其固定 4096 字节传输
                wchlink_flash_transfer_length =
                    (wchlink_flash_chunk_length & 0xffu) == 0u
                        ? wchlink_flash_chunk_length
                        : WCHLINK_FLASH_CHUNK_SIZE;
            } else {
                // MRS 和 wlink 以数据端点包长对齐尾包，首包长度就是本次包长
                wchlink_flash_transfer_length =
                    ((wchlink_flash_chunk_length + (uint32_t)length - 1u) /
                     (uint32_t)length) *
                    (uint32_t)length;
            }
        }
        transfer_length = wchlink_flash_transfer_length;
        transfer_remaining = transfer_length - wchlink_flash_transfer_received;
        if ((uint32_t)length > transfer_remaining) {
            // 主机包不能跨越本次数据块边界，超长包只消费边界内部分
            length = (size_t)transfer_remaining;
        }
        if (wchlink_flash_transfer_received < wchlink_flash_chunk_length) {
            write_length = wchlink_flash_chunk_length -
                           wchlink_flash_transfer_received;
            if (write_length > length) {
                write_length = length;
            }
            if (write_length != 0u) {
                if (wchlink_target_supports_memory_streaming()) {
                    // 支持连续写入的目标每个 4 KiB chunk 只建立一次 RVSWD 上下文
                    memcpy(&wchlink_flash_chunk_data[wchlink_flash_transfer_received],
                           data, write_length);
                } else {
                    if (!rvswd_gpio_write_memory(
                            layout->data + wchlink_flash_transfer_received,
                            data, (uint32_t)write_length)) {
                        wchlink_write_mode = 0u;
                        wchlink_data_reply_status =
                            rvswd_gpio_memory_last_error();
                        if (wchlink_data_reply_status == 0u) {
                            wchlink_data_reply_status = 0x15u;
                        }
                        wchlink_data_reply_pending = true;
                        return;
                    }
                    wchlink_partial_cache_range(
                        wchlink_write_address + wchlink_flash_transfer_received,
                        data, (uint32_t)write_length);
                }
                wchlink_flash_checksum =
                    wchlink_checksum_add(wchlink_flash_checksum, data,
                                         write_length);
                wchlink_flash_data_received += (uint32_t)write_length;
            }
        }
        wchlink_flash_transfer_received += (uint32_t)length;
        if (wchlink_flash_transfer_received < transfer_length) {
            return;
        }

        if (wchlink_target_supports_memory_streaming() &&
            wchlink_flash_data_received != 0u &&
            !rvswd_gpio_write_memory(layout->data,
                                     wchlink_flash_chunk_data,
                                     wchlink_flash_chunk_length)) {
            wchlink_write_mode = 0u;
            wchlink_data_reply_status = rvswd_gpio_memory_last_error();
            if (wchlink_data_reply_status == 0u) {
                wchlink_data_reply_status = 0x15u;
            }
            wchlink_data_reply_pending = true;
            return;
        }

        // loader 读取完整页，实际数据不足的尾部必须显式写入擦除态
        {
            uint32_t padded_length = wchlink_flash_padded_data_length();

            if (wchlink_flash_data_received < padded_length &&
                !wchlink_flash_write_padding(layout,
                                             wchlink_flash_data_received,
                                             padded_length)) {
                wchlink_write_mode = 0u;
                wchlink_data_reply_status = rvswd_gpio_memory_last_error();
                if (wchlink_data_reply_status == 0u) {
                    wchlink_data_reply_status = 0x15u;
                }
                wchlink_data_reply_pending = true;
                return;
            }
            wchlink_flash_data_received = padded_length;
        }
        {
            uint32_t result = 0xffffffffu;
            uint32_t checksum_address = 0u;
            bool success;

            if ((wchlink_flash_loader_mode & 0x10u) != 0u) {
                // L103 和 CH5xx loader 从目标 RAM 读取主机计算的校验和
                if (wchlink_target_uses_ch5xx_loader()) {
                    checksum_address = WCHLINK_CH5XX_LOADER_CHECKSUM_ADDRESS;
                } else if (wchlink_target_uses_l103_loader()) {
                    checksum_address = WCHLINK_L103_LOADER_CHECKSUM_ADDRESS;
                }
                if (checksum_address != 0u &&
                    !rvswd_gpio_write_memory32(checksum_address,
                                               wchlink_flash_checksum)) {
                    wchlink_write_mode = 0u;
                    wchlink_data_reply_status = 0x15u;
                    wchlink_data_reply_pending = true;
                    return;
                }
            }
            success = rvswd_gpio_execute(layout->entry, layout->stack_top,
                                         wchlink_flash_loader_mode,
                                         wchlink_write_address,
                                         wchlink_flash_chunk_length, layout->data,
                                         &result);

            // LinkE 将 loader 的三个标准返回值转换为数据端点状态
            if (success && result == 0u) {
                wchlink_data_reply_status = 0x04u;
            } else if (success && result == 8u) {
                wchlink_data_reply_status = 0x03u;
            } else if (success && result == 16u) {
                wchlink_data_reply_status = 0x05u;
            } else {
                wchlink_data_reply_status = (uint8_t)result;
            }
            wchlink_data_reply_pending = true;
            if (success && wchlink_write_remaining > wchlink_flash_chunk_length) {
                wchlink_write_address += wchlink_flash_chunk_length;
                wchlink_write_remaining -= wchlink_flash_chunk_length;
                wchlink_flash_data_received = 0u;
                wchlink_flash_transfer_received = 0u;
                wchlink_flash_transfer_length = 0u;
                wchlink_flash_checksum = 0u;
                wchlink_flash_chunk_length =
                    wchlink_write_remaining > WCHLINK_FLASH_CHUNK_SIZE
                        ? WCHLINK_FLASH_CHUNK_SIZE
                        : wchlink_write_remaining;
            } else {
                wchlink_write_address = 0u;
                wchlink_write_remaining = 0u;
                wchlink_write_mode = 0u;
            }
        }
    }
}

bool wchlink_protocol_take_data_reply(uint8_t *data, size_t capacity) {
    if (data == NULL || capacity < 4u || !wchlink_data_reply_pending) {
        return false;
    }
    data[0] = 0x41u;
    data[1] = 0x01u;
    data[2] = 0x01u;
    data[3] = wchlink_data_reply_status;
    wchlink_data_reply_pending = false;
    return true;
}

size_t wchlink_protocol_read_data(uint8_t *data, size_t capacity) {
    size_t produced = 0u;

    if (data == NULL || capacity < 4u || !wchlink_read_active) {
        return 0u;
    }

    while (produced + 4u <= capacity && wchlink_read_remaining >= 4u) {
        uint32_t value;

        if (!rvswd_gpio_read_memory32(wchlink_read_address, &value)) {
            wchlink_read_active = false;
            wchlink_read_remaining = 0u;
            return 0u;
        }

        // WCH-Link 数据端点按大端字节发送，wlink 主机随后按字反转
        data[produced + 0u] = (uint8_t)(value >> 24u);
        data[produced + 1u] = (uint8_t)(value >> 16u);
        data[produced + 2u] = (uint8_t)(value >> 8u);
        data[produced + 3u] = (uint8_t)value;
        produced += 4u;
        wchlink_read_address += 4u;
        wchlink_read_remaining -= 4u;
    }

    if (wchlink_read_remaining == 0u) {
        wchlink_read_active = false;
    }
    return produced;
}

size_t wchlink_protocol_process(const uint8_t *request, size_t request_length,
                                uint8_t *response, size_t response_capacity) {
    uint8_t family;
    size_t response_length;

    if (request == NULL || response == NULL || request_length < 2u || request[0] != WCHLINK_COMMAND_PREFIX) {
        return wchlink_ack(response, response_capacity, 0u);
    }

    family = request[1];
    if (family == WCHLINK_FAMILY_DMI && request_length >= 9u) {
        return wchlink_dmi(request, response, response_capacity);
    }
    if (family == WCHLINK_FAMILY_CONFIG) {
        return wchlink_config(request, request_length, response, response_capacity);
    }
    if (family == WCHLINK_FAMILY_DEVICE_MODE && request_length >= 4u) {
        if (request[3] == WCHLINK_DEVICE_MODE_QUERY) {
            // MRS 通过第四字节 2 识别 Link 当前处于 RISC-V 调试模式
            return wchlink_command_reply(response, response_capacity,
                                         WCHLINK_FAMILY_DEVICE_MODE,
                                         WCHLINK_DEVICE_MODE_QUERY);
        }
        if (request[3] == WCHLINK_DEVICE_MODE_IAP) {
            // 保留官方 SetIAPMode 的无响应语义，由 USB 层切换到本探针的维护 ISP
            wchlink_isp_request_pending = true;
            return SIZE_MAX;
        }
        return wchlink_unsupported(response, response_capacity,
                                   WCHLINK_FAMILY_DEVICE_MODE);
    }
    if (family == WCHLINK_FAMILY_PARTIAL_WRITE && request_length >= 8u) {
        uint32_t address = ((uint32_t)request[3] << 24u) |
                           ((uint32_t)request[4] << 16u) |
                           ((uint32_t)request[5] << 8u) | request[6];

        if (!wchlink_connected || request[7] == 0u ||
            request[7] > sizeof(wchlink_partial_write_data)) {
            return wchlink_unsupported(response, response_capacity,
                                       WCHLINK_FAMILY_PARTIAL_WRITE);
        }
        wchlink_partial_write_address = address;
        wchlink_partial_write_length = request[7];
        wchlink_write_mode = 3u;
        return wchlink_command_reply(response, response_capacity,
                                     WCHLINK_FAMILY_PARTIAL_WRITE, request[2]);
    }
    if (family == 0x01u && request_length >= 11u) {
        uint32_t first = ((uint32_t)request[3] << 24u) |
                         ((uint32_t)request[4] << 16u) |
                         ((uint32_t)request[5] << 8u) | request[6];
        uint32_t second = ((uint32_t)request[7] << 24u) |
                          ((uint32_t)request[8] << 16u) |
                          ((uint32_t)request[9] << 8u) | request[10];

        wchlink_clear_transfer_state();
        memset(wchlink_partial_cache, 0xff, sizeof(wchlink_partial_cache));
        wchlink_partial_cache_valid = true;
        wchlink_write_address = first;
        wchlink_write_remaining = second;
        wchlink_flash_chunk_length = wchlink_write_remaining > WCHLINK_FLASH_CHUNK_SIZE
                                         ? WCHLINK_FLASH_CHUNK_SIZE
                                         : wchlink_write_remaining;
        return wchlink_command_reply(response, response_capacity, family, 0x01u);
    }
    if (family == 0x03u && request_length >= 11u) {
        wchlink_clear_transfer_state();
        wchlink_read_address = ((uint32_t)request[3] << 24u) |
                               ((uint32_t)request[4] << 16u) |
                               ((uint32_t)request[5] << 8u) | request[6];
        wchlink_read_remaining = ((uint32_t)request[7] << 24u) |
                                 ((uint32_t)request[8] << 16u) |
                                 ((uint32_t)request[9] << 8u) | request[10];
        wchlink_read_active = false;
        return wchlink_ack(response, response_capacity, family);
    }
    if (family == 0x02u && request_length >= 4u) {
        switch (request[3]) {
            case 0x01u:
                wchlink_clear_transfer_state();
                if (!wchlink_connected) {
                    // MRS 版本预检会先发送 STOP，基础全擦必须重新建立目标会话
                    rvswd_gpio_init();
                    wchlink_connected = rvswd_gpio_connect();
                }
                if (!wchlink_connected || !rvswd_gpio_flash_erase_all()) {
                    if (response_capacity >= 4u) {
                        response[0] = WCHLINK_COMMAND_PREFIX;
                        response[1] = family;
                        response[2] = 1u;
                        response[3] = (uint8_t)rvswd_gpio_flash_last_error();
                        return 4u;
                    }
                    return wchlink_unsupported(response, response_capacity, family);
                }
                response_length = wchlink_ack(response, response_capacity, family);
                if (response_length != 0u) {
                    // MRS 的基础全擦命令要求应答状态为 1
                    response[3] = 0x01u;
                }
                return response_length;
            case 0x05u:
                if (!wchlink_connected || wchlink_write_remaining == 0u) {
                    return wchlink_unsupported(response, response_capacity, family);
                }
                wchlink_write_mode = 1u;
                wchlink_loader_received = 0u;
                wchlink_loader_error = 0u;
                wchlink_loader_failure_dmi_status = 0u;
                wchlink_loader_failure_address = 0u;
                wchlink_loader_failure_abstractcs = 0u;
                wchlink_loader_ready = false;
                // CH58x 和 CH59x 的 loader 长度由主机分包决定，LinkE 以 0x07 作为结束命令
                wchlink_loader_variable_length = wchlink_target_uses_ch5xx_loader();
                wchlink_loader_expected = wchlink_loader_variable_length
                                              ? WCHLINK_CH5XX_LOADER_MAX_SIZE
                                              : WCHLINK_LOADER_DEFAULT_SIZE;
                return wchlink_command_reply(response, response_capacity, family,
                                             request[3]);
            case 0x06u:
                // 官方 OpenOCD 在地址设置前发送 Prepare，状态必须跨过地址帧保留
                wchlink_flash_prepare_seen = true;
                return wchlink_command_reply(response, response_capacity, family,
                                             request[3]);
            case 0x07u:
            case 0x0bu: {
                const struct wchlink_loader_layout *layout =
                    wchlink_target_loader_layout();
                uint32_t result = 0xffffffffu;
                bool success;

                // 0x07 是 loader 数据阶段的结束边界，成功后立即切换到 Flash 数据接收
                wchlink_write_mode = 0u;
                if (wchlink_loader_error != 0u) {
                    if (response_capacity < 13u) {
                        return 0u;
                    }
                    response[0] = WCHLINK_COMMAND_PREFIX;
                    response[1] = family;
                    response[2] = 10u;
                    response[3] = wchlink_loader_error;
                    response[4] = wchlink_loader_failure_dmi_status;
                    response[5] = (uint8_t)(wchlink_loader_failure_address >> 24u);
                    response[6] = (uint8_t)(wchlink_loader_failure_address >> 16u);
                    response[7] = (uint8_t)(wchlink_loader_failure_address >> 8u);
                    response[8] = (uint8_t)wchlink_loader_failure_address;
                    response[9] = (uint8_t)(wchlink_loader_failure_abstractcs >> 24u);
                    response[10] = (uint8_t)(wchlink_loader_failure_abstractcs >> 16u);
                    response[11] = (uint8_t)(wchlink_loader_failure_abstractcs >> 8u);
                    response[12] = (uint8_t)wchlink_loader_failure_abstractcs;
                    return 13u;
                }
                if ((!wchlink_loader_variable_length && !wchlink_loader_ready) ||
                    (wchlink_loader_variable_length && wchlink_loader_received == 0u)) {
                    return wchlink_unsupported(response, response_capacity, family);
                }
                // LinkE 固件连续两次以 mode 1 初始化 loader
                success = rvswd_gpio_execute(layout->entry, layout->stack_top, 0x01u,
                                             0u, 0u, layout->data, &result) &&
                          result == 0u;
                if (success) {
                    success = rvswd_gpio_execute(layout->entry, layout->stack_top,
                                                 (wchlink_flash_prepare_seen &&
                                                  !wchlink_target_uses_ch5xx_loader())
                                                     ? 0x03u
                                                     : 0x01u,
                                                 0u, 0u, layout->data,
                                                 &result) &&
                              result == 0u;
                }
                if (success &&
                    response_capacity >= 4u) {
                    wchlink_loader_ready = true;
                    // CH5xx 的 OpenOCD 路径只执行编程，V30x Prepare 路径附带校验
                    wchlink_flash_loader_mode = request[3] == 0x0bu
                                                    ? 0x10u
                                                : wchlink_target_uses_ch5xx_loader()
                                                    ? 0x08u
                                                    : (wchlink_flash_prepare_seen
                                                           ? 0x18u
                                                           : 0x08u);
                    // WCH OpenOCD 在 0x07 回复后直接发送固定 4096 字节数据
                    wchlink_write_mode = 2u;
                    wchlink_flash_openocd_mode = true;
                    wchlink_flash_data_received = 0u;
                    wchlink_flash_transfer_received = 0u;
                    wchlink_flash_transfer_length = 0u;
                    wchlink_flash_checksum = 0u;
                    wchlink_flash_prepare_seen = false;
                    return wchlink_command_reply(response, response_capacity, family,
                                                 request[3]);
                }
                if (response_capacity >= 4u) {
                    wchlink_loader_ready = false;
                    response[0] = WCHLINK_COMMAND_PREFIX;
                    response[1] = family;
                    response[2] = 1u;
                    response[3] = (uint8_t)result;
                    return 4u;
                }
                return 0u;
            }
            case 0x02u:
            case 0x03u:
            case 0x04u:
                if (!wchlink_loader_ready || wchlink_write_remaining == 0u) {
                    return wchlink_unsupported(response, response_capacity, family);
                }
                wchlink_write_mode = 2u;
                wchlink_flash_data_received = 0u;
                wchlink_flash_transfer_received = 0u;
                wchlink_flash_transfer_length = 0u;
                wchlink_flash_checksum = 0u;
                wchlink_flash_openocd_mode = false;
                // LinkE 用命令位组合选择 loader 的编程、校验和组合模式
                wchlink_flash_loader_mode = request[3] == 0x02u ? 0x08u : request[3] == 0x03u ? 0x10u
                                                                                              : 0x18u;
                return wchlink_command_reply(response, response_capacity, family,
                                             request[3]);
            case 0x08u:
                wchlink_flash_prepare_seen = false;
                wchlink_clear_transfer_state();
                return wchlink_ack(response, response_capacity, family);
            case 0x0cu:
                wchlink_protocol_begin_data_read();
                return wchlink_ack(response, response_capacity, family);
            default:
                return wchlink_ack(response, response_capacity, family);
        }
    }
    if (family == WCHLINK_FAMILY_INFO) {
        // MRS 会在 STOP 前读取扩展信息，只有该会话出现过查询才返回 20 字节
        if (wchlink_target_uses_ch5xx_loader()) {
            wchlink_ch5xx_info_query_seen = true;
        }
        return wchlink_chip_info(response, response_capacity);
    }
    if (family == WCHLINK_FAMILY_SPEED) {
        if (request_length < 5u) {
            return wchlink_unsupported(response, response_capacity, family);
        }
        rvswd_gpio_set_target_wchlink_family_hint(request[3]);
        response_length = wchlink_ack(response, response_capacity, family);
        if (response_length != 0u) {
            response[3] = 1u;
        }
        return response_length;
    }
    if (family == WCHLINK_FAMILY_RESET) {
        if (request_length >= 4u && request[3] == WCHLINK_RESET_SOFT &&
            wchlink_connected) {
            if (!rvswd_gpio_soft_reset_and_run()) {
                return wchlink_unsupported(response, response_capacity, family);
            }
            return wchlink_command_reply(response, response_capacity, family,
                                         request[3]);
        }
        if (request_length >= 4u && request[3] == WCHLINK_RESET_MRS_RUN &&
            wchlink_connected) {
            if (!rvswd_gpio_reset_and_run()) {
                return wchlink_unsupported(response, response_capacity, family);
            }
            return wchlink_command_reply(response, response_capacity, family,
                                         request[3]);
        }
        if (request_length >= 4u && request[3] == WCHLINK_RESET_NORMAL &&
            wchlink_connected &&
            !rvswd_gpio_reset_and_halt()) {
            return wchlink_unsupported(response, response_capacity, family);
        }
        return wchlink_ack(response, response_capacity, family);
    }
    if (family != WCHLINK_FAMILY_CONTROL || request_length < 4u) {
        return wchlink_ack(response, response_capacity, family);
    }

    switch (request[3]) {
        case WCHLINK_CONTROL_IDENTIFY:
            wchlink_protocol_reset();
            return wchlink_identity(response, response_capacity);
        case WCHLINK_CONTROL_CONNECT:
            wchlink_protocol_reset();
            rvswd_gpio_init();
            wchlink_connected = rvswd_gpio_connect();
            return wchlink_connect_reply(response, response_capacity, wchlink_connected);
        case WCHLINK_CONTROL_STOP: {
            bool return_ch5xx_info =
                wchlink_ch5xx_info_query_seen && wchlink_target_uses_ch5xx_loader();

            wchlink_protocol_reset();
            if (return_ch5xx_info) {
                // MRS 在 CH5xx 设置芯片阶段从 STOP 命令读取 20 字节目标信息
                return wchlink_chip_info(response, response_capacity);
            }
            return wchlink_ack(response, response_capacity, family);
        }
        case WCHLINK_CONTROL_SET_CHIP_TYPE:
            // MRS 将设置目标型号命令作为首次目标连接入口
            if (wchlink_connected) {
                // wlink 在已连接会话中使用同一子命令查询 ROM/RAM 分割
                return wchlink_ack(response, response_capacity, family);
            }
            // MRS 在设置两线速度后立即发起连接，目标调试模块需要短暂稳定时间
            bsp_delay_ms(20u);
            rvswd_gpio_init();
            wchlink_connected = rvswd_gpio_connect();
            return wchlink_connect_reply(response, response_capacity, wchlink_connected);
        case WCHLINK_CONTROL_CLEAR_CODE_FLASH:
        case WCHLINK_CONTROL_CLEAR_CODE_FLASH_B:
            if (request_length < 5u) {
                return wchlink_unsupported(response, response_capacity, family);
            }
            if (!wchlink_connected) {
                // MRS 直接通过清擦除命令建立目标会话，末字节携带目标 family
                rvswd_gpio_set_target_wchlink_family_hint(request[4]);
                rvswd_gpio_init();
                wchlink_connected = rvswd_gpio_connect();
            }
            if (!wchlink_connected || !rvswd_gpio_flash_erase_all()) {
                return wchlink_target_error(response, response_capacity);
            }
            wchlink_clear_transfer_state();
            response_length = wchlink_ack(response, response_capacity, family);
            if (response_length != 0u) {
                // MRS 的全擦命令要求回显原子命令
                response[3] = request[3];
            }
            return response_length;
        case WCHLINK_CONTROL_POWER_3V3_ON:
            drv_dp_pullup_set_enabled(true);
            return wchlink_ack(response, response_capacity, family);
        case WCHLINK_CONTROL_POWER_3V3_OFF:
            drv_dp_pullup_set_enabled(false);
            return wchlink_ack(response, response_capacity, family);
        case WCHLINK_CONTROL_POWER_5V_ON:
            drv_power_switch_set_enabled(true);
            return wchlink_ack(response, response_capacity, family);
        case WCHLINK_CONTROL_POWER_5V_OFF:
            wchlink_partial_cache_valid = false;
            wchlink_protocol_reset();
            drv_power_switch_set_enabled(false);
            return wchlink_ack(response, response_capacity, family);
        case WCHLINK_CONTROL_HOLD:
        case WCHLINK_CONTROL_RESET_LOW:
            return wchlink_ack(response, response_capacity, family);
        default:
            return wchlink_ack(response, response_capacity, family);
    }
}
