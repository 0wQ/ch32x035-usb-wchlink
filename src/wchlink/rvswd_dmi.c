#include "rvswd_dmi.h"

#include "bsp/bsp_delay.h"
#include "rvswd_frame.h"
#include "rvswd_phy_gpio.h"

#include <stddef.h>

#include <ch32x035.h>

#define RVSWD_STATUS_OK 1u
#define RVSWD_STATUS_BUSY 3u
#define RVSWD_LONG_STATUS_OK 0u
#define RVSWD_LONG_STATUS_BUSY 3u
#define RVSWD_DMI_WRITE_RETRY_COUNT 16u
#define RVSWD_DMI_READ_RETRY_COUNT 64u
#define RVSWD_DMI_BUSY_DELAY_US 100u
#define RVSWD_DMI_ERROR_DELAY_US 50u
#define RVSWD_INTERFRAME_GUARD_US 0u
#define RVSWD_ABSTRACT_COMMAND_DELAY_US 100u

static enum rvswd_packet_mode rvswd_dmi_current_packet_mode =
    RVSWD_PACKET_SHORT;
static uint8_t rvswd_dmi_current_last_status;
static bool rvswd_dmi_current_failure_retryable;

void rvswd_dmi_reset(void) {
    rvswd_dmi_current_packet_mode = RVSWD_PACKET_SHORT;
    rvswd_dmi_current_last_status = 0u;
    rvswd_dmi_current_failure_retryable = false;
}

void rvswd_dmi_set_packet_mode(enum rvswd_packet_mode mode) {
    rvswd_dmi_current_packet_mode = mode;
}

enum rvswd_packet_mode rvswd_dmi_packet_mode(void) {
    return rvswd_dmi_current_packet_mode;
}

void rvswd_dmi_set_last_status(uint8_t status) {
    rvswd_dmi_current_last_status = status;
}

uint8_t rvswd_dmi_last_status(void) {
    return rvswd_dmi_current_last_status;
}

void rvswd_dmi_set_failure_retryable(bool retryable) {
    rvswd_dmi_current_failure_retryable = retryable;
}

bool rvswd_dmi_failure_retryable(void) {
    return rvswd_dmi_current_failure_retryable;
}

bool rvswd_dmi_transaction_long(uint8_t operation, uint8_t address,
                                uint32_t value, uint8_t host_parity,
                                uint8_t *target_address, uint32_t *result,
                                uint8_t *status) {
    uint32_t target_address_value;
    uint32_t target_data;
    uint32_t target_status;

    __disable_irq();
    rvswd_phy_gpio_start();
    rvswd_phy_gpio_drive_value(address & 0x7fu, 7u);
    rvswd_phy_gpio_drive_value(value, 32u);
    rvswd_phy_gpio_drive_value(operation, 2u);
    rvswd_phy_gpio_drive_value(host_parity, 1u);
    rvswd_phy_gpio_config_data_input();
    target_address_value = rvswd_phy_gpio_sample_value(7u);
    target_data = rvswd_phy_gpio_sample_value(32u);
    target_status = rvswd_phy_gpio_sample_value(2u);
    (void)rvswd_phy_gpio_sample_value(1u);
    rvswd_phy_gpio_config_data_output();
    rvswd_phy_gpio_stop();
    __enable_irq();
    bsp_delay_us(RVSWD_INTERFRAME_GUARD_US);

    if (result != NULL) {
        *result = target_data;
    }
    if (target_address != NULL) {
        *target_address = (uint8_t)target_address_value;
    }
    if (status != NULL) {
        *status = (uint8_t)target_status;
    }
    return true;
}

static bool rvswd_dmi_transaction(const uint8_t *host, uint8_t *target,
                                  bool read) {
    __disable_irq();
    rvswd_phy_gpio_start();
    rvswd_phy_gpio_drive_range(host, 0u, 9u);
    rvswd_phy_gpio_config_data_input();
    rvswd_phy_gpio_sample_range(target, 9u, 3u);
    rvswd_phy_gpio_config_data_output();
    rvswd_phy_gpio_drive_range(host, 12u, 2u);
    if (read) {
        rvswd_phy_gpio_config_data_input();
        rvswd_phy_gpio_sample_range(target, 14u, 36u);
        rvswd_phy_gpio_config_data_output();
        rvswd_phy_gpio_drive_range(host, 50u, 2u);
    } else {
        rvswd_phy_gpio_drive_range(host, 14u, 33u);
        rvswd_phy_gpio_config_data_input();
        rvswd_phy_gpio_sample_range(target, 47u, 3u);
        rvswd_phy_gpio_config_data_output();
        rvswd_phy_gpio_drive_range(host, 50u, 2u);
    }
    rvswd_phy_gpio_stop();
    __enable_irq();
    bsp_delay_us(RVSWD_INTERFRAME_GUARD_US);
    return true;
}

bool rvswd_dmi_write(uint8_t address, uint32_t value) {
    if (rvswd_dmi_packet_mode() == RVSWD_PACKET_LONG) {
        uint8_t status;

        rvswd_dmi_set_failure_retryable(false);
        for (uint8_t retry = 0u; retry < RVSWD_DMI_WRITE_RETRY_COUNT; ++retry) {
            if (!rvswd_dmi_transaction_long(2u, address, value, 0u, NULL,
                                             NULL, &status)) {
                rvswd_dmi_set_failure_retryable(true);
                bsp_delay_us(RVSWD_DMI_ERROR_DELAY_US);
                continue;
            }
            rvswd_dmi_set_last_status(status);
            if (status == RVSWD_LONG_STATUS_OK) {
                if (address == 0x17u) {
                    bsp_delay_us(RVSWD_ABSTRACT_COMMAND_DELAY_US);
                }
                return true;
            }
            if (status == RVSWD_LONG_STATUS_BUSY) {
                rvswd_dmi_set_failure_retryable(true);
                bsp_delay_us(RVSWD_DMI_BUSY_DELAY_US);
            } else {
                rvswd_dmi_set_failure_retryable(false);
                bsp_delay_us(RVSWD_DMI_ERROR_DELAY_US);
            }
        }
        return false;
    }

    uint8_t frame[7] = {0};
    uint8_t target[7] = {0};
    uint8_t status;

    rvswd_frame_pack_write(frame, address, value);
    rvswd_dmi_set_failure_retryable(false);
    for (uint8_t retry = 0u; retry < RVSWD_DMI_WRITE_RETRY_COUNT; ++retry) {
        if (!rvswd_dmi_transaction(frame, target, false)) {
            return false;
        }
        status = rvswd_frame_unpack_handshake(target);
        rvswd_dmi_set_last_status(status);
        if (rvswd_frame_status_is_ok(status)) {
            if (address == 0x17u) {
                bsp_delay_us(RVSWD_ABSTRACT_COMMAND_DELAY_US);
            }
            return true;
        }
        if (status == RVSWD_STATUS_BUSY) {
            rvswd_dmi_set_failure_retryable(true);
            bsp_delay_us(RVSWD_DMI_BUSY_DELAY_US);
        } else {
            rvswd_dmi_set_failure_retryable(false);
            bsp_delay_us(RVSWD_DMI_ERROR_DELAY_US);
        }
    }
    return false;
}

bool rvswd_dmi_read(uint8_t address, uint32_t *value) {
    if (value == NULL) {
        return false;
    }
    if (rvswd_dmi_packet_mode() == RVSWD_PACKET_LONG) {
        uint8_t status;
        uint32_t data;

        rvswd_dmi_set_failure_retryable(false);
        for (uint8_t retry = 0u; retry < RVSWD_DMI_READ_RETRY_COUNT; ++retry) {
            if (!rvswd_dmi_transaction_long(1u, address, 0u, 0u, NULL, &data,
                                             &status)) {
                rvswd_dmi_set_failure_retryable(true);
                bsp_delay_us(RVSWD_DMI_ERROR_DELAY_US);
                continue;
            }
            rvswd_dmi_set_last_status(status);
            if (status == RVSWD_LONG_STATUS_OK) {
                *value = data;
                return true;
            }
            if (status == RVSWD_LONG_STATUS_BUSY) {
                rvswd_dmi_set_failure_retryable(true);
                bsp_delay_us(RVSWD_DMI_BUSY_DELAY_US);
            } else {
                rvswd_dmi_set_failure_retryable(false);
                bsp_delay_us(RVSWD_DMI_ERROR_DELAY_US);
            }
        }
        return false;
    }

    uint8_t frame[7] = {0};
    uint8_t target[7] = {0};
    uint8_t status;

    rvswd_frame_pack_read(frame, address);
    rvswd_dmi_set_failure_retryable(false);
    for (uint8_t retry = 0u; retry < RVSWD_DMI_READ_RETRY_COUNT; ++retry) {
        if (!rvswd_dmi_transaction(frame, target, true)) {
            return false;
        }
        status = rvswd_frame_unpack_handshake(target);
        rvswd_dmi_set_last_status(status);
        if (status == RVSWD_STATUS_BUSY) {
            rvswd_dmi_set_failure_retryable(true);
            bsp_delay_us(RVSWD_DMI_BUSY_DELAY_US);
            continue;
        }
        if (!rvswd_frame_status_is_ok(status)) {
            rvswd_dmi_set_failure_retryable(false);
            bsp_delay_us(RVSWD_DMI_ERROR_DELAY_US);
            continue;
        }
        *value = rvswd_frame_unpack_data(target);
        if (rvswd_frame_get_bit(target, 46u) !=
            rvswd_frame_xor_bits(*value)) {
            rvswd_dmi_set_failure_retryable(true);
            bsp_delay_us(RVSWD_DMI_ERROR_DELAY_US);
            continue;
        }
        return true;
    }
    return false;
}
