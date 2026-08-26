#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// WCH-Link USB wire family 编号
#define WCHLINK_FAMILY_RESET         0x0bu
#define WCHLINK_FAMILY_SPEED         0x0cu
#define WCHLINK_FAMILY_CONTROL       0x0du
#define WCHLINK_FAMILY_INFO          0x11u
#define WCHLINK_FAMILY_DMI           0x08u
#define WCHLINK_FAMILY_PARTIAL_WRITE 0x0au
#define WCHLINK_FAMILY_CONFIG        0x06u
#define WCHLINK_FAMILY_DEVICE_MODE   0x0fu

#define WCHLINK_COMMAND_PREFIX 0x81u
#define WCHLINK_REPLY_PREFIX   0x82u

#define WCHLINK_PARTIAL_WRITE_REPLY_OK     0x02u
#define WCHLINK_PARTIAL_WRITE_REPLY_FAILED 0x15u

#define WCHLINK_CONFIG_READ_PROTECTION    0x01u
#define WCHLINK_CONFIG_DISABLE_PROTECTION 0x02u
#define WCHLINK_CONFIG_ENABLE_PROTECTION  0x03u
#define WCHLINK_CONFIG_WRITE_PROTECTION   0x04u
#define WCHLINK_CONFIG_READ_PROTECTED     0x01u
#define WCHLINK_CONFIG_READ_UNPROTECTED   0x02u
#define WCHLINK_CONFIG_WRITE_PROTECTED    0x11u
#define WCHLINK_CONFIG_WRITE_UNPROTECTED  0x00u

#define WCHLINK_CONTROL_IDENTIFY           0x01u
#define WCHLINK_CONTROL_CONNECT            0x02u
#define WCHLINK_CONTROL_HOLD               0x03u
#define WCHLINK_CONTROL_SET_CHIP_TYPE      0x04u
#define WCHLINK_CONTROL_CLEAR_CODE_FLASH   0x08u
#define WCHLINK_CONTROL_CLEAR_CODE_FLASH_B 0x0fu
#define WCHLINK_CONTROL_POWER_3V3_ON       0x09u
#define WCHLINK_CONTROL_POWER_3V3_OFF      0x0au
#define WCHLINK_CONTROL_POWER_5V_ON        0x0bu
#define WCHLINK_CONTROL_POWER_5V_OFF       0x0cu
#define WCHLINK_CONTROL_RESET_LOW          0x13u
#define WCHLINK_CONTROL_STOP               0xffu

#define WCHLINK_RESET_MRS_RUN 0x06u
#define WCHLINK_RESET_SOFT    0x01u
#define WCHLINK_RESET_NORMAL  0x03u

#define WCHLINK_DEVICE_MODE_IAP   0x01u
#define WCHLINK_DEVICE_MODE_QUERY 0x02u

// Loader 和 Flash data transfer 的 wire 尺寸
#define WCHLINK_FLASH_PACKET_SIZE             256u
#define WCHLINK_FLASH_CHUNK_SIZE              4096u
#define WCHLINK_LOADER_DEFAULT_SIZE           512u
#define WCHLINK_CH5XX_LOADER_MAX_SIZE         2048u
#define WCHLINK_CH5XX_LOADER_PAGE_SIZE        256u
#define WCHLINK_CH5XX_LOADER_CHECKSUM_ADDRESS 0x20006010u
#define WCHLINK_L103_LOADER_CHECKSUM_ADDRESS  0x20002010u

struct wchlink_wire_chip_info {
    bool ch5xx;
    uint32_t flash_size;
    uint32_t uid_low;
    uint32_t uid_high;
    uint32_t uid_tail;
    uint32_t chip_id;
};

// Wire encoder 只写入调用者提供的 buffer，容量不足或参数无效时返回 0
size_t wchlink_wire_ack(uint8_t *response, size_t capacity, uint8_t family);
size_t wchlink_wire_unsupported(uint8_t *response, size_t capacity,
                                uint8_t family);
size_t wchlink_wire_family_error(uint8_t *response, size_t capacity,
                                 uint8_t family, uint32_t code);
size_t wchlink_wire_target_error(uint8_t *response, size_t capacity,
                                 uint32_t code);
size_t wchlink_wire_command_reply(uint8_t *response, size_t capacity,
                                  uint8_t family, uint8_t command);
size_t wchlink_wire_identity(uint8_t *response, size_t capacity);
size_t wchlink_wire_connect_reply(uint8_t *response, size_t capacity,
                                  bool connected, uint32_t error,
                                  uint8_t family, uint32_t chip_id);
size_t wchlink_wire_chip_info(uint8_t *response, size_t capacity,
                              const struct wchlink_wire_chip_info *info);
size_t wchlink_wire_dmi_reply(uint8_t *response, size_t capacity,
                              uint8_t address, uint32_t data, bool success,
                              bool retryable);
size_t wchlink_wire_loader_error(uint8_t *response, size_t capacity,
                                 uint8_t family, uint8_t loader_error,
                                 uint8_t dmi_status, uint32_t address,
                                 uint32_t abstractcs);
size_t wchlink_wire_data_reply(uint8_t *response, size_t capacity,
                               uint8_t status);
