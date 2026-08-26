#include "wchlink/protocol/wchlink_wire.h"

#include <string.h>

size_t wchlink_wire_ack(uint8_t *response, size_t capacity, uint8_t family) {
    if (response == NULL || capacity < 4u) {
        return 0u;
    }
    response[0] = WCHLINK_REPLY_PREFIX;
    response[1] = family;
    response[2] = 1u;
    response[3] = 0u;
    return 4u;
}

size_t wchlink_wire_unsupported(uint8_t *response, size_t capacity,
                                uint8_t family) {
    if (response == NULL || capacity < 4u) {
        return 0u;
    }
    response[0] = WCHLINK_COMMAND_PREFIX;
    response[1] = family;
    response[2] = 1u;
    response[3] = 2u;
    return 4u;
}

size_t wchlink_wire_family_error(uint8_t *response, size_t capacity,
                                 uint8_t family, uint32_t code) {
    if (response == NULL || capacity < 4u) {
        return 0u;
    }
    response[0] = WCHLINK_COMMAND_PREFIX;
    response[1] = family;
    response[2] = 1u;
    response[3] = (uint8_t)code;
    return 4u;
}

size_t wchlink_wire_target_error(uint8_t *response, size_t capacity,
                                 uint32_t code) {
    return wchlink_wire_family_error(response, capacity, 0x55u, code);
}

size_t wchlink_wire_command_reply(uint8_t *response, size_t capacity,
                                  uint8_t family, uint8_t command) {
    if (response == NULL || capacity < 4u) {
        return 0u;
    }
    response[0] = WCHLINK_REPLY_PREFIX;
    response[1] = family;
    response[2] = 1u;
    response[3] = command;
    return 4u;
}

size_t wchlink_wire_identity(uint8_t *response, size_t capacity) {
    if (response == NULL || capacity < 7u) {
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

size_t wchlink_wire_connect_reply(uint8_t *response, size_t capacity,
                                  bool connected, uint32_t error,
                                  uint8_t family, uint32_t chip_id) {
    bool unsupported_chip = connected && family == 0u;

    if (unsupported_chip) {
        connected = false;
    }
    if (!connected) {
        if (response == NULL || capacity < 4u) {
            return 0u;
        }
        response[0] = WCHLINK_COMMAND_PREFIX;
        response[1] = 0x55u;
        response[2] = 1u;
        response[3] = unsupported_chip ? 0x30u : (uint8_t)error;
        return 4u;
    }
    if (response == NULL || capacity < 8u) {
        return 0u;
    }
    response[0] = WCHLINK_REPLY_PREFIX;
    response[1] = WCHLINK_FAMILY_CONTROL;
    response[2] = 5u;
    response[3] = family;
    response[4] = (uint8_t)(chip_id >> 24u);
    response[5] = (uint8_t)(chip_id >> 16u);
    response[6] = (uint8_t)(chip_id >> 8u);
    response[7] = (uint8_t)chip_id;
    return 8u;
}

size_t wchlink_wire_chip_info(uint8_t *response, size_t capacity,
                              const struct wchlink_wire_chip_info *info) {
    if (response == NULL || info == NULL || capacity < 20u) {
        return 0u;
    }
    if (info->ch5xx) {
        // MRS 的 CH5xx FlashOperation 路径固定读取 20 字节，LinkE 将 ChipID 放在第 4 字节
        memset(response, 0, 20u);
        response[0] = WCHLINK_REPLY_PREFIX;
        response[1] = WCHLINK_FAMILY_CONTROL;
        response[2] = 1u;
        response[3] = 0xffu;
        response[4] = (uint8_t)(info->chip_id >> 24u);
        return 20u;
    }

    // 芯片信息查询使用无帧头的 20 字节原始回复
    response[0] = 0xffu;
    response[1] = 0xffu;
    response[2] = (uint8_t)(info->flash_size >> 8u);
    response[3] = (uint8_t)info->flash_size;
    response[4] = (uint8_t)(info->uid_low >> 24u);
    response[5] = (uint8_t)(info->uid_low >> 16u);
    response[6] = (uint8_t)(info->uid_low >> 8u);
    response[7] = (uint8_t)info->uid_low;
    response[8] = (uint8_t)(info->uid_high >> 24u);
    response[9] = (uint8_t)(info->uid_high >> 16u);
    response[10] = (uint8_t)(info->uid_high >> 8u);
    response[11] = (uint8_t)info->uid_high;
    response[12] = (uint8_t)(info->uid_tail >> 24u);
    response[13] = (uint8_t)(info->uid_tail >> 16u);
    response[14] = (uint8_t)(info->uid_tail >> 8u);
    response[15] = (uint8_t)info->uid_tail;
    response[16] = (uint8_t)(info->chip_id >> 24u);
    response[17] = (uint8_t)(info->chip_id >> 16u);
    response[18] = (uint8_t)(info->chip_id >> 8u);
    response[19] = (uint8_t)info->chip_id;
    return 20u;
}

size_t wchlink_wire_dmi_reply(uint8_t *response, size_t capacity,
                              uint8_t address, uint32_t data, bool success,
                              bool retryable) {
    if (response == NULL || capacity < 9u) {
        return 0u;
    }
    response[0] = WCHLINK_REPLY_PREFIX;
    response[1] = WCHLINK_FAMILY_DMI;
    response[2] = 6u;
    response[3] = address;
    response[4] = (uint8_t)(data >> 24u);
    response[5] = (uint8_t)(data >> 16u);
    response[6] = (uint8_t)(data >> 8u);
    response[7] = (uint8_t)data;
    response[8] = success ? 0u : (retryable ? 3u : 2u);
    return 9u;
}

size_t wchlink_wire_loader_error(uint8_t *response, size_t capacity,
                                 uint8_t family, uint8_t loader_error,
                                 uint8_t dmi_status, uint32_t address,
                                 uint32_t abstractcs) {
    if (response == NULL || capacity < 13u) {
        return 0u;
    }
    response[0] = WCHLINK_COMMAND_PREFIX;
    response[1] = family;
    response[2] = 10u;
    response[3] = loader_error;
    response[4] = dmi_status;
    response[5] = (uint8_t)(address >> 24u);
    response[6] = (uint8_t)(address >> 16u);
    response[7] = (uint8_t)(address >> 8u);
    response[8] = (uint8_t)address;
    response[9] = (uint8_t)(abstractcs >> 24u);
    response[10] = (uint8_t)(abstractcs >> 16u);
    response[11] = (uint8_t)(abstractcs >> 8u);
    response[12] = (uint8_t)abstractcs;
    return 13u;
}

size_t wchlink_wire_data_reply(uint8_t *response, size_t capacity,
                               uint8_t status) {
    if (response == NULL || capacity < 4u) {
        return 0u;
    }
    response[0] = 0x41u;
    response[1] = 0x01u;
    response[2] = 0x01u;
    response[3] = status;
    return 4u;
}
