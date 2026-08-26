#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void wchlink_protocol_reset(void);
size_t wchlink_protocol_process(const uint8_t *request, size_t request_length, uint8_t *response, size_t response_capacity);
bool wchlink_protocol_take_isp_request(void);
bool wchlink_protocol_is_connected(void);
void wchlink_protocol_begin_data_read(void);
size_t wchlink_protocol_read_data(uint8_t *data, size_t capacity);
bool wchlink_protocol_data_read_active(void);
bool wchlink_protocol_data_write_active(void);
void wchlink_protocol_write_data(const uint8_t *data, size_t length);
bool wchlink_protocol_take_data_reply(uint8_t *data, size_t capacity);
