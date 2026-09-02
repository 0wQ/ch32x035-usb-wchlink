#include "wchlink/transport/rvswd_transport.h"

#include "bsp/bsp_delay.h"
#include "wchlink/transport/rvswd_frame.h"
#include "wchlink/transport/rvswd_phy_gpio.h"

#include <stddef.h>

#include <ch32x035.h>

// retry 和 delay 由 transport 统一执行，frame 和 PHY 不保存策略副本
static const uint8_t rvswd_transport_status_busy = 3u;
static const uint8_t rvswd_transport_long_status_ok = 0u;
static const uint8_t rvswd_transport_long_status_busy = 3u;
static const uint8_t rvswd_transport_write_retry_count = 16u;
static const uint8_t rvswd_transport_read_retry_count = 64u;
static const uint32_t rvswd_transport_busy_delay_us = 100u;
static const uint32_t rvswd_transport_error_delay_us = 50u;
static const uint32_t rvswd_transport_interframe_guard_us = 0u;
static const uint32_t rvswd_transport_abstract_command_delay_us = 100u;

void rvswd_transport_init(struct rvswd_transport *transport) {
    if (transport == NULL) {
        return;
    }
    transport->packet_mode = RVSWD_PACKET_SHORT;
    transport->fast_timing = false;
    rvswd_phy_gpio_init();
}

void rvswd_transport_disconnect(struct rvswd_transport *transport) {
    (void)transport;
    rvswd_phy_gpio_disconnect();
}

void rvswd_transport_set_packet_mode(struct rvswd_transport *transport, enum rvswd_packet_mode mode) {
    if (transport != NULL) {
        transport->packet_mode = mode;
    }
}

enum rvswd_packet_mode rvswd_transport_packet_mode(const struct rvswd_transport *transport) {
    return transport == NULL ? RVSWD_PACKET_SHORT : transport->packet_mode;
}

void rvswd_transport_set_fast_timing(struct rvswd_transport *transport, bool enabled) {
    if (transport != NULL) {
        transport->fast_timing = enabled;
    }
}

void rvswd_transport_wakeup(struct rvswd_transport *transport, bool stop_condition) {
    if (transport != NULL) {
        rvswd_phy_gpio_wakeup(transport->fast_timing, stop_condition);
        bsp_delay_us(rvswd_transport_interframe_guard_us);
    }
}

struct rvswd_transport_probe_result rvswd_transport_probe_long(struct rvswd_transport *transport,
                                                               uint8_t operation, uint8_t address,
                                                               uint32_t value, uint8_t host_parity) {
    struct rvswd_transport_probe_result result = {0};
    bool fast_timing;

    if (transport == NULL) {
        return result;
    }

    // 在进入不可打断的长帧前固定本次会话的时序档位，保证整帧不会跨档位执行
    fast_timing = transport->fast_timing;
    __disable_irq();
    rvswd_phy_gpio_start(fast_timing);
    rvswd_phy_gpio_drive_value(fast_timing, address & 0x7fu, 7u);
    rvswd_phy_gpio_drive_value(fast_timing, value, 32u);
    rvswd_phy_gpio_drive_value(fast_timing, operation, 2u);
    rvswd_phy_gpio_drive_value(fast_timing, host_parity, 1u);
    rvswd_phy_gpio_config_data_input();
    result.address = (uint8_t)rvswd_phy_gpio_sample_value(fast_timing, 7u);
    result.value = rvswd_phy_gpio_sample_value(fast_timing, 32u);
    result.status = (uint8_t)rvswd_phy_gpio_sample_value(fast_timing, 2u);
    (void)rvswd_phy_gpio_sample_value(fast_timing, 1u);
    rvswd_phy_gpio_config_data_output();
    rvswd_phy_gpio_stop(fast_timing);
    __enable_irq();
    bsp_delay_us(rvswd_transport_interframe_guard_us);

    return result;
}

static bool rvswd_transport_transaction(struct rvswd_transport *transport,
                                        const uint8_t *host, uint8_t *target, bool read) {
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
    bsp_delay_us(rvswd_transport_interframe_guard_us);
    return true;
}

struct rvswd_transport_result rvswd_transport_probe_short(struct rvswd_transport *transport,
                                                          bool read, uint8_t address, uint32_t value) {
    struct rvswd_transport_result result = {0};
    uint8_t frame[7] = {0};
    uint8_t target[7] = {0};

    if (transport == NULL ||
        rvswd_transport_packet_mode(transport) != RVSWD_PACKET_SHORT) {
        return result;
    }
    if (read) {
        rvswd_frame_pack_read(frame, address);
    } else {
        rvswd_frame_pack_write(frame, address, value);
    }
    if (!rvswd_transport_transaction(transport, frame, target, read)) {
        return result;
    }

    result.status = rvswd_frame_unpack_handshake(target);
    if (result.status == rvswd_transport_status_busy) {
        result.retryable = true;
        return result;
    }
    if (!rvswd_frame_status_is_ok(result.status)) {
        return result;
    }
    if (!read) {
        result.ok = true;
        return result;
    }
    result.value = rvswd_frame_unpack_data(target);
    if (rvswd_frame_get_bit(target, 46u) != rvswd_frame_xor_bits(result.value)) {
        result.retryable = true;
        return result;
    }
    result.ok = true;
    return result;
}

struct rvswd_transport_result rvswd_transport_write(struct rvswd_transport *transport,
                                                    uint8_t address, uint32_t value) {
    struct rvswd_transport_result result = {0};

    if (transport == NULL) {
        return result;
    }
    if (rvswd_transport_packet_mode(transport) == RVSWD_PACKET_LONG) {
        for (uint8_t retry = 0u; retry < rvswd_transport_write_retry_count; ++retry) {
            struct rvswd_transport_probe_result probe = rvswd_transport_probe_long(transport, 2u, address, value, 0u);

            result.status = probe.status;
            if (probe.status == rvswd_transport_long_status_ok) {
                if (address == RVSWD_DMI_COMMAND) {
                    bsp_delay_us(rvswd_transport_abstract_command_delay_us);
                }
                result.ok = true;
                return result;
            }
            if (probe.status == rvswd_transport_long_status_busy) {
                result.retryable = true;
                bsp_delay_us(rvswd_transport_busy_delay_us);
            } else {
                result.retryable = false;
                bsp_delay_us(rvswd_transport_error_delay_us);
            }
        }
        return result;
    }

    uint8_t frame[7] = {0};
    uint8_t target[7] = {0};

    rvswd_frame_pack_write(frame, address, value);
    for (uint8_t retry = 0u; retry < rvswd_transport_write_retry_count; ++retry) {
        if (!rvswd_transport_transaction(transport, frame, target, false)) {
            return result;
        }
        result.status = rvswd_frame_unpack_handshake(target);
        if (rvswd_frame_status_is_ok(result.status)) {
            if (address == RVSWD_DMI_COMMAND) {
                bsp_delay_us(rvswd_transport_abstract_command_delay_us);
            }
            result.ok = true;
            return result;
        }
        if (result.status == rvswd_transport_status_busy) {
            result.retryable = true;
            bsp_delay_us(rvswd_transport_busy_delay_us);
        } else {
            result.retryable = false;
            bsp_delay_us(rvswd_transport_error_delay_us);
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
        for (uint8_t retry = 0u; retry < rvswd_transport_read_retry_count; ++retry) {
            struct rvswd_transport_probe_result probe = rvswd_transport_probe_long(transport, 1u, address, 0u, 0u);

            result.status = probe.status;
            if (probe.status == rvswd_transport_long_status_ok) {
                result.ok = true;
                result.value = probe.value;
                return result;
            }
            if (probe.status == rvswd_transport_long_status_busy) {
                result.retryable = true;
                bsp_delay_us(rvswd_transport_busy_delay_us);
            } else {
                result.retryable = false;
                bsp_delay_us(rvswd_transport_error_delay_us);
            }
        }
        return result;
    }

    uint8_t frame[7] = {0};
    uint8_t target[7] = {0};

    rvswd_frame_pack_read(frame, address);
    for (uint8_t retry = 0u; retry < rvswd_transport_read_retry_count; ++retry) {
        if (!rvswd_transport_transaction(transport, frame, target, true)) {
            return result;
        }
        result.status = rvswd_frame_unpack_handshake(target);
        if (result.status == rvswd_transport_status_busy) {
            result.retryable = true;
            bsp_delay_us(rvswd_transport_busy_delay_us);
            continue;
        }
        if (!rvswd_frame_status_is_ok(result.status)) {
            result.retryable = false;
            bsp_delay_us(rvswd_transport_error_delay_us);
            continue;
        }
        result.value = rvswd_frame_unpack_data(target);
        if (rvswd_frame_get_bit(target, 46u) != rvswd_frame_xor_bits(result.value)) {
            result.retryable = true;
            bsp_delay_us(rvswd_transport_error_delay_us);
            continue;
        }
        result.ok = true;
        return result;
    }
    return result;
}
