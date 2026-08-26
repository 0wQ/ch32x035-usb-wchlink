#include "rvswd_transport.h"

#include "bsp/bsp_delay.h"
#include "rvswd_frame.h"
#include "rvswd_phy_gpio.h"

#include <stddef.h>

#include <ch32x035.h>

#define RVSWD_STATUS_OK                 1u
#define RVSWD_STATUS_BUSY               3u
#define RVSWD_LONG_STATUS_OK            0u
#define RVSWD_LONG_STATUS_BUSY          3u
#define RVSWD_DMI_WRITE_RETRY_COUNT     16u
#define RVSWD_DMI_READ_RETRY_COUNT      64u
#define RVSWD_DMI_BUSY_DELAY_US         100u
#define RVSWD_DMI_ERROR_DELAY_US        50u
#define RVSWD_INTERFRAME_GUARD_US       0u
#define RVSWD_ABSTRACT_COMMAND_DELAY_US 100u

void rvswd_transport_init(struct rvswd_transport *transport) {
    if (transport == NULL) {
        return;
    }
    transport->packet_mode = RVSWD_PACKET_SHORT;
    transport->last_status = 0u;
    transport->failure_retryable = false;
    transport->fast_timing = false;
    rvswd_phy_gpio_init();
}

void rvswd_transport_disconnect(struct rvswd_transport *transport) {
    (void)transport;
    rvswd_phy_gpio_disconnect();
}

void rvswd_transport_set_packet_mode(struct rvswd_transport *transport,
                                     enum rvswd_packet_mode mode) {
    if (transport != NULL) {
        transport->packet_mode = mode;
    }
}

enum rvswd_packet_mode rvswd_transport_packet_mode(
    const struct rvswd_transport *transport) {
    return transport == NULL ? RVSWD_PACKET_SHORT : transport->packet_mode;
}

void rvswd_transport_set_fast_timing(struct rvswd_transport *transport,
                                     bool enabled) {
    if (transport != NULL) {
        transport->fast_timing = enabled;
    }
}

void rvswd_transport_wakeup(struct rvswd_transport *transport,
                            bool stop_condition) {
    if (transport != NULL) {
        rvswd_phy_gpio_wakeup(transport->fast_timing, stop_condition);
    }
}

uint8_t rvswd_transport_last_status(
    const struct rvswd_transport *transport) {
    return transport == NULL ? 0u : transport->last_status;
}

bool rvswd_transport_failure_retryable(
    const struct rvswd_transport *transport) {
    return transport != NULL && transport->failure_retryable;
}

struct rvswd_transport_probe_result rvswd_transport_probe_long(
    struct rvswd_transport *transport, uint8_t operation, uint8_t address,
    uint32_t value, uint8_t host_parity) {
    struct rvswd_transport_probe_result result = {0};

    if (transport == NULL) {
        return result;
    }

    __disable_irq();
    rvswd_phy_gpio_start(transport->fast_timing);
    rvswd_phy_gpio_drive_value(transport->fast_timing, address & 0x7fu, 7u);
    rvswd_phy_gpio_drive_value(transport->fast_timing, value, 32u);
    rvswd_phy_gpio_drive_value(transport->fast_timing, operation, 2u);
    rvswd_phy_gpio_drive_value(transport->fast_timing, host_parity, 1u);
    rvswd_phy_gpio_config_data_input();
    result.address = (uint8_t)
        rvswd_phy_gpio_sample_value(transport->fast_timing, 7u);
    result.value = rvswd_phy_gpio_sample_value(transport->fast_timing, 32u);
    result.status = (uint8_t)
        rvswd_phy_gpio_sample_value(transport->fast_timing, 2u);
    (void)rvswd_phy_gpio_sample_value(transport->fast_timing, 1u);
    rvswd_phy_gpio_config_data_output();
    rvswd_phy_gpio_stop(transport->fast_timing);
    __enable_irq();
    bsp_delay_us(RVSWD_INTERFRAME_GUARD_US);

    return result;
}

static bool rvswd_transport_transaction(struct rvswd_transport *transport,
                                        const uint8_t *host, uint8_t *target,
                                        bool read) {
    __disable_irq();
    rvswd_phy_gpio_start(transport->fast_timing);
    rvswd_phy_gpio_drive_range(transport->fast_timing, host, 0u, 9u);
    rvswd_phy_gpio_config_data_input();
    rvswd_phy_gpio_sample_range(transport->fast_timing, target, 9u, 3u);
    rvswd_phy_gpio_config_data_output();
    rvswd_phy_gpio_drive_range(transport->fast_timing, host, 12u, 2u);
    if (read) {
        rvswd_phy_gpio_config_data_input();
        rvswd_phy_gpio_sample_range(transport->fast_timing, target, 14u, 36u);
        rvswd_phy_gpio_config_data_output();
        rvswd_phy_gpio_drive_range(transport->fast_timing, host, 50u, 2u);
    } else {
        rvswd_phy_gpio_drive_range(transport->fast_timing, host, 14u, 33u);
        rvswd_phy_gpio_config_data_input();
        rvswd_phy_gpio_sample_range(transport->fast_timing, target, 47u, 3u);
        rvswd_phy_gpio_config_data_output();
        rvswd_phy_gpio_drive_range(transport->fast_timing, host, 50u, 2u);
    }
    rvswd_phy_gpio_stop(transport->fast_timing);
    __enable_irq();
    bsp_delay_us(RVSWD_INTERFRAME_GUARD_US);
    return true;
}

struct rvswd_transport_result rvswd_transport_write(
    struct rvswd_transport *transport, uint8_t address, uint32_t value) {
    struct rvswd_transport_result result = {0};

    if (transport == NULL) {
        return result;
    }
    if (rvswd_transport_packet_mode(transport) == RVSWD_PACKET_LONG) {
        transport->failure_retryable = false;
        for (uint8_t retry = 0u; retry < RVSWD_DMI_WRITE_RETRY_COUNT; ++retry) {
            struct rvswd_transport_probe_result probe =
                rvswd_transport_probe_long(transport, 2u, address, value, 0u);

            result.status = probe.status;
            transport->last_status = result.status;
            if (probe.status == RVSWD_LONG_STATUS_OK) {
                if (address == 0x17u) {
                    bsp_delay_us(RVSWD_ABSTRACT_COMMAND_DELAY_US);
                }
                result.ok = true;
                return result;
            }
            if (probe.status == RVSWD_LONG_STATUS_BUSY) {
                result.retryable = true;
                transport->failure_retryable = true;
                bsp_delay_us(RVSWD_DMI_BUSY_DELAY_US);
            } else {
                result.retryable = false;
                transport->failure_retryable = false;
                bsp_delay_us(RVSWD_DMI_ERROR_DELAY_US);
            }
        }
        return result;
    }

    uint8_t frame[7] = {0};
    uint8_t target[7] = {0};

    rvswd_frame_pack_write(frame, address, value);
    transport->failure_retryable = false;
    for (uint8_t retry = 0u; retry < RVSWD_DMI_WRITE_RETRY_COUNT; ++retry) {
        if (!rvswd_transport_transaction(transport, frame, target, false)) {
            return result;
        }
        result.status = rvswd_frame_unpack_handshake(target);
        transport->last_status = result.status;
        if (rvswd_frame_status_is_ok(result.status)) {
            if (address == 0x17u) {
                bsp_delay_us(RVSWD_ABSTRACT_COMMAND_DELAY_US);
            }
            result.ok = true;
            return result;
        }
        if (result.status == RVSWD_STATUS_BUSY) {
            result.retryable = true;
            transport->failure_retryable = true;
            bsp_delay_us(RVSWD_DMI_BUSY_DELAY_US);
        } else {
            result.retryable = false;
            transport->failure_retryable = false;
            bsp_delay_us(RVSWD_DMI_ERROR_DELAY_US);
        }
    }
    return result;
}

struct rvswd_transport_result rvswd_transport_read(
    struct rvswd_transport *transport, uint8_t address) {
    struct rvswd_transport_result result = {0};

    if (transport == NULL) {
        return result;
    }
    if (rvswd_transport_packet_mode(transport) == RVSWD_PACKET_LONG) {
        transport->failure_retryable = false;
        for (uint8_t retry = 0u; retry < RVSWD_DMI_READ_RETRY_COUNT; ++retry) {
            struct rvswd_transport_probe_result probe =
                rvswd_transport_probe_long(transport, 1u, address, 0u, 0u);

            result.status = probe.status;
            transport->last_status = result.status;
            if (probe.status == RVSWD_LONG_STATUS_OK) {
                result.ok = true;
                result.value = probe.value;
                return result;
            }
            if (probe.status == RVSWD_LONG_STATUS_BUSY) {
                result.retryable = true;
                transport->failure_retryable = true;
                bsp_delay_us(RVSWD_DMI_BUSY_DELAY_US);
            } else {
                result.retryable = false;
                transport->failure_retryable = false;
                bsp_delay_us(RVSWD_DMI_ERROR_DELAY_US);
            }
        }
        return result;
    }

    uint8_t frame[7] = {0};
    uint8_t target[7] = {0};

    rvswd_frame_pack_read(frame, address);
    transport->failure_retryable = false;
    for (uint8_t retry = 0u; retry < RVSWD_DMI_READ_RETRY_COUNT; ++retry) {
        if (!rvswd_transport_transaction(transport, frame, target, true)) {
            return result;
        }
        result.status = rvswd_frame_unpack_handshake(target);
        transport->last_status = result.status;
        if (result.status == RVSWD_STATUS_BUSY) {
            result.retryable = true;
            transport->failure_retryable = true;
            bsp_delay_us(RVSWD_DMI_BUSY_DELAY_US);
            continue;
        }
        if (!rvswd_frame_status_is_ok(result.status)) {
            result.retryable = false;
            transport->failure_retryable = false;
            bsp_delay_us(RVSWD_DMI_ERROR_DELAY_US);
            continue;
        }
        result.value = rvswd_frame_unpack_data(target);
        if (rvswd_frame_get_bit(target, 46u) !=
            rvswd_frame_xor_bits(result.value)) {
            result.retryable = true;
            bsp_delay_us(RVSWD_DMI_ERROR_DELAY_US);
            continue;
        }
        result.ok = true;
        return result;
    }
    return result;
}
