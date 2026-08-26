#include "rvswd_frame.h"

static const uint8_t rvswd_frame_status_ok = 1u;

uint8_t rvswd_frame_xor_bits(uint32_t value) {
    value ^= value >> 16u;
    value ^= value >> 8u;
    value ^= value >> 4u;
    value ^= value >> 2u;
    value ^= value >> 1u;
    return (uint8_t)(value & 1u);
}

void rvswd_frame_set_bit(uint8_t *buffer, uint8_t position, uint8_t value) {
    uint8_t mask = (uint8_t)(1u << (7u - (position & 7u)));

    if (value != 0u) {
        buffer[position >> 3u] |= mask;
    } else {
        buffer[position >> 3u] &= (uint8_t)~mask;
    }
}

uint8_t rvswd_frame_get_bit(const uint8_t *buffer, uint8_t position) {
    return (uint8_t)((buffer[position >> 3u] >> (7u - (position & 7u))) & 1u);
}

static void rvswd_frame_pack_common(uint8_t *frame, uint8_t address,
                                    uint8_t operation) {
    frame[0] = (uint8_t)((address & 0x7fu) << 1u | (operation & 1u));
    rvswd_frame_set_bit(frame, 8u,
                        (uint8_t)(rvswd_frame_xor_bits(address & 0x7fu) ^
                                  operation));
    rvswd_frame_set_bit(frame, 13u, 1u);
    rvswd_frame_set_bit(frame, 51u, 1u);
}

void rvswd_frame_pack_write(uint8_t *frame, uint8_t address, uint32_t value) {
    rvswd_frame_pack_common(frame, address, 1u);
    for (uint8_t bit = 0u; bit < 32u; ++bit) {
        rvswd_frame_set_bit(frame, (uint8_t)(14u + bit),
                            (uint8_t)((value >> (31u - bit)) & 1u));
    }
    rvswd_frame_set_bit(frame, 46u, rvswd_frame_xor_bits(value));
    rvswd_frame_set_bit(frame, 50u, 1u);
}

void rvswd_frame_pack_read(uint8_t *frame, uint8_t address) {
    rvswd_frame_pack_common(frame, address, 0u);
    rvswd_frame_set_bit(frame, 50u, 1u);
}

uint8_t rvswd_frame_unpack_handshake(const uint8_t *target) {
    return (uint8_t)(rvswd_frame_get_bit(target, 48u) << 1u |
                     rvswd_frame_get_bit(target, 49u));
}

bool rvswd_frame_status_is_ok(uint8_t status) {
    return status == rvswd_frame_status_ok;
}

uint32_t rvswd_frame_unpack_data(const uint8_t *target) {
    uint32_t value = 0u;

    for (uint8_t bit = 0u; bit < 32u; ++bit) {
        value = (value << 1u) |
                rvswd_frame_get_bit(target, (uint8_t)(14u + bit));
    }
    return value;
}
