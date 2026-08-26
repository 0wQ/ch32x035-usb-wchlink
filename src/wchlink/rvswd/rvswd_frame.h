#pragma once

#include <stdbool.h>
#include <stdint.h>

uint8_t rvswd_frame_xor_bits(uint32_t value);
void rvswd_frame_set_bit(uint8_t *buffer, uint8_t position, uint8_t value);
uint8_t rvswd_frame_get_bit(const uint8_t *buffer, uint8_t position);
void rvswd_frame_pack_write(uint8_t *frame, uint8_t address, uint32_t value);
void rvswd_frame_pack_read(uint8_t *frame, uint8_t address);
uint8_t rvswd_frame_unpack_handshake(const uint8_t *target);
bool rvswd_frame_status_is_ok(uint8_t status);
uint32_t rvswd_frame_unpack_data(const uint8_t *target);
