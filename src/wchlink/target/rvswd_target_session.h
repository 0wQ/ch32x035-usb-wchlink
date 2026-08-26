#pragma once

#include "rvswd_target_info.h"
#include "rvswd_target_result.h"

#include <stdbool.h>
#include <stdint.h>

// 该结构只在 WCH-Link session 内分配，连接状态和目标信息不向 USB 层泄漏
struct rvswd_target_session {
    struct rvswd_target_info info;
    uint8_t family_hint;
    uint8_t connect_error;
};

void rvswd_target_session_init(struct rvswd_target_session *session);
void rvswd_target_session_disconnect(struct rvswd_target_session *session);
struct rvswd_target_result rvswd_target_session_connect(
    struct rvswd_target_session *session);
void rvswd_target_session_set_family_hint(
    struct rvswd_target_session *session, uint8_t family);
bool rvswd_target_session_is_connected(
    const struct rvswd_target_session *session);
const struct rvswd_target_info *rvswd_target_session_info(
    const struct rvswd_target_session *session);
bool rvswd_target_session_supports_memory_streaming(
    const struct rvswd_target_session *session);

struct rvswd_target_result rvswd_target_session_read_dmi(
    struct rvswd_target_session *session, uint8_t address);
struct rvswd_target_result rvswd_target_session_write_dmi(
    struct rvswd_target_session *session, uint8_t address, uint32_t value);
struct rvswd_target_result rvswd_target_session_read_memory32(
    struct rvswd_target_session *session, uint32_t address);
struct rvswd_target_result rvswd_target_session_write_memory32(
    struct rvswd_target_session *session, uint32_t address, uint32_t value);
struct rvswd_target_result rvswd_target_session_write_memory(
    struct rvswd_target_session *session, uint32_t address,
    const uint8_t *data, uint32_t length);
struct rvswd_target_result rvswd_target_session_write_register(
    struct rvswd_target_session *session, uint16_t regno, uint32_t value);
struct rvswd_target_result rvswd_target_session_read_register(
    struct rvswd_target_session *session, uint16_t regno);
struct rvswd_target_result rvswd_target_session_halt(
    struct rvswd_target_session *session);
struct rvswd_target_result rvswd_target_session_execute(
    struct rvswd_target_session *session, uint32_t entry, uint32_t stack_top,
    uint32_t mode, uint32_t address, uint32_t length, uint32_t data_address);
struct rvswd_target_result rvswd_target_session_reset_and_halt(
    struct rvswd_target_session *session);
struct rvswd_target_result rvswd_target_session_soft_reset_and_run(
    struct rvswd_target_session *session);
struct rvswd_target_result rvswd_target_session_reset_and_run(
    struct rvswd_target_session *session);
struct rvswd_target_result rvswd_target_session_flash_erase_all(
    struct rvswd_target_session *session);
struct rvswd_target_result rvswd_target_session_flash_rewrite_page(
    struct rvswd_target_session *session, uint32_t address,
    const uint8_t *data);
struct rvswd_target_result rvswd_target_session_flash_read_protected(
    struct rvswd_target_session *session);
struct rvswd_target_result rvswd_target_session_flash_write_protected(
    struct rvswd_target_session *session);
struct rvswd_target_result rvswd_target_session_flash_set_read_protected(
    struct rvswd_target_session *session, bool protected);
