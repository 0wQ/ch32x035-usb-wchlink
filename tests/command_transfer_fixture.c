#include "in_memory_target.h"
#include "wchlink/protocol/wchlink_family.h"
#include "wchlink/protocol/wchlink_wire.h"
#include "wchlink/session/wchlink_command_internal.h"
#include "wchlink/session/wchlink_transfer_internal.h"

#include <assert.h>
#include <string.h>

struct wchlink_test_fixture {
    struct wchlink_target_ports target;
    struct wchlink_transfer transfer;
    struct wchlink_command_context command;
};

enum wchlink_test_loader_kind {
    WCHLINK_TEST_LOADER_DEFAULT,
    WCHLINK_TEST_LOADER_L103,
    WCHLINK_TEST_LOADER_LEGACY,
};

static uint32_t wchlink_test_last_delay_ms;
static bool wchlink_test_dp_pullup_enabled;
static bool wchlink_test_power_switch_enabled;

void bsp_delay_ms(uint32_t ms) {
    wchlink_test_last_delay_ms = ms;
}

void drv_dp_pullup_set_enabled(bool enabled) {
    wchlink_test_dp_pullup_enabled = enabled;
}

void drv_power_switch_set_enabled(bool enabled) {
    wchlink_test_power_switch_enabled = enabled;
}

static struct rvswd_target_info wchlink_test_info(
    uint32_t chip_id, uint8_t family, enum wchlink_test_loader_kind loader,
    bool memory_streaming) {
    bool legacy_loader = loader == WCHLINK_TEST_LOADER_LEGACY;
    bool x03x = family == WCHLINK_TARGET_FAMILY_X03X;

    return (struct rvswd_target_info){
        .chip_id = chip_id,
        .family = family,
        .loader_download_limit = legacy_loader ? 2048u : 512u,
        .loader_data_page_size = legacy_loader ? 256u : 1u,
        .loader_initialize_mode = 0x01u,
        .loader_prepared_mode = x03x || legacy_loader ? 0x01u : 0x03u,
        .loader_program_mode = x03x ? 0x0cu : 0x08u,
        .loader_verify_mode = 0x10u,
        .loader_program_verify_mode = x03x ? 0x1cu : legacy_loader ? 0x08u : 0x18u,
        .loader_checksum_mode_mask = 0x10u,
        .loader_length_mode_mask = x03x ? 0x08u : 0u,
        .loader_repeat_initialize = !x03x,
        .partial_write_supported = legacy_loader,
        .code_flash_size =
            x03x ? 0xf800u : 0u,
        .code_flash_base = 0x08000000u,
        .connected = true,
        .memory_streaming = memory_streaming,
        .loader_variable_length = legacy_loader,
    };
}

static void wchlink_test_fixture_init(
    struct wchlink_test_fixture *fixture, struct rvswd_target_info info,
    bool connected) {
    wchlink_test_target_reset(&fixture->target, info, connected);
    wchlink_transfer_init(&fixture->transfer, &fixture->target);
    fixture->command = (struct wchlink_command_context){
        .target = &fixture->target,
        .transfer = &fixture->transfer,
    };
    wchlink_test_last_delay_ms = 0u;
    wchlink_test_dp_pullup_enabled = false;
    wchlink_test_power_switch_enabled = false;
}

static void wchlink_test_expect_bytes(const uint8_t *actual,
                                      size_t actual_length,
                                      const uint8_t *expected,
                                      size_t expected_length) {
    assert(actual_length == expected_length);
    assert(memcmp(actual, expected, expected_length) == 0);
}

static uint8_t wchlink_test_take_status(
    struct wchlink_transfer *transfer) {
    uint8_t status = 0xffu;

    assert(wchlink_transfer_take_reply_status(transfer, &status));
    return status;
}

static void wchlink_test_command_connect_and_dmi(void) {
    const struct rvswd_target_info info = wchlink_test_info(
        0x10320710u, WCHLINK_TARGET_FAMILY_CH32L10X,
        WCHLINK_TEST_LOADER_L103, true);
    struct wchlink_test_fixture fixture;
    uint8_t response[32];
    struct wchlink_session_command_result result;
    const uint8_t short_request[] = {0x81u, 0x0du};
    const uint8_t connect_request[] = {0x81u, 0x0du, 0x01u, 0x02u};
    const uint8_t connect_reply[] = {
        0x82u,
        0x0du,
        0x05u,
        0x0eu,
        0x10u,
        0x32u,
        0x07u,
        0x10u,
    };
    const uint8_t info_request[] = {0x81u, 0x11u};
    const uint8_t info_reply[] = {
        0xffu,
        0xffu,
        0x12u,
        0x34u,
        0x01u,
        0x02u,
        0x03u,
        0x04u,
        0x11u,
        0x22u,
        0x33u,
        0x44u,
        0xaau,
        0xbbu,
        0xccu,
        0xddu,
        0x10u,
        0x32u,
        0x07u,
        0x10u,
    };
    const uint8_t flash_size[] = {0x34u, 0x12u, 0u, 0u};
    const uint8_t uid_low[] = {0x04u, 0x03u, 0x02u, 0x01u};
    const uint8_t uid_high[] = {0x44u, 0x33u, 0x22u, 0x11u};
    const uint8_t uid_tail[] = {0xddu, 0xccu, 0xbbu, 0xaau};
    const uint8_t dmi_request[] = {
        0x81u,
        0x08u,
        0x06u,
        0x11u,
        0u,
        0u,
        0u,
        0u,
        0x01u,
    };
    const uint8_t dmi_reply[] = {
        0x82u,
        0x08u,
        0x06u,
        0x11u,
        0x12u,
        0x34u,
        0x56u,
        0x78u,
        0u,
    };
    const uint8_t dmi_retry_reply[] = {
        0x82u,
        0x08u,
        0x06u,
        0x11u,
        0u,
        0u,
        0u,
        0u,
        0x03u,
    };
    const uint8_t stop_request[] = {0x81u, 0x0du, 0x01u, 0xffu};
    const uint8_t stop_reply[] = {0x82u, 0x0du, 0x01u, 0u};
    const uint8_t malformed_reply[] = {0x82u, 0x0du, 0x01u, 0u};

    // 同一 command seam 覆盖畸形帧、连接、DMI retry 和会话结束回复
    wchlink_test_fixture_init(&fixture, info, false);
    result = wchlink_command_process(
        &fixture.command, short_request, sizeof(short_request), response,
        sizeof(response));
    assert(result.status == WCHLINK_SESSION_COMMAND_MALFORMED);
    wchlink_test_expect_bytes(response, result.response_length,
                              malformed_reply, sizeof(malformed_reply));

    result = wchlink_command_process(
        &fixture.command, connect_request, sizeof(connect_request), response,
        sizeof(response));
    assert(result.status == WCHLINK_SESSION_COMMAND_COMPLETED);
    wchlink_test_expect_bytes(response, result.response_length, connect_reply,
                              sizeof(connect_reply));

    assert(wchlink_test_target_store(&fixture.target, 0x1ffff7e0u,
                                     flash_size, sizeof(flash_size)));
    assert(wchlink_test_target_store(&fixture.target, 0x1ffff7e8u, uid_low,
                                     sizeof(uid_low)));
    assert(wchlink_test_target_store(&fixture.target, 0x1ffff7ecu, uid_high,
                                     sizeof(uid_high)));
    assert(wchlink_test_target_store(&fixture.target, 0x1ffff7f0u, uid_tail,
                                     sizeof(uid_tail)));
    result = wchlink_command_process(&fixture.command, info_request,
                                     sizeof(info_request), response,
                                     sizeof(response));
    assert(result.status == WCHLINK_SESSION_COMMAND_COMPLETED);
    wchlink_test_expect_bytes(response, result.response_length, info_reply,
                              sizeof(info_reply));

    wchlink_test_target_fail_next(
        &fixture.target, WCHLINK_TEST_TARGET_READ_MEMORY,
        rvswd_target_result_failure(RVSWD_TARGET_RESULT_MEMORY, 0x15u,
                                    false));
    result = wchlink_command_process(&fixture.command, info_request,
                                     sizeof(info_request), response,
                                     sizeof(response));
    assert(result.status == WCHLINK_SESSION_COMMAND_TARGET_FAILED);
    assert(result.response_length == 0u);

    wchlink_test_target_set_dmi(&fixture.target, 0x11u, 0x12345678u);
    result = wchlink_command_process(
        &fixture.command, dmi_request, sizeof(dmi_request), response,
        sizeof(response));
    assert(result.status == WCHLINK_SESSION_COMMAND_COMPLETED);
    wchlink_test_expect_bytes(response, result.response_length, dmi_reply,
                              sizeof(dmi_reply));

    wchlink_test_target_fail_next(
        &fixture.target, WCHLINK_TEST_TARGET_READ_DMI,
        rvswd_target_result_failure(RVSWD_TARGET_RESULT_DMI, 0x03u, true));
    result = wchlink_command_process(
        &fixture.command, dmi_request, sizeof(dmi_request), response,
        sizeof(response));
    assert(result.status == WCHLINK_SESSION_COMMAND_BUSY);
    wchlink_test_expect_bytes(response, result.response_length,
                              dmi_retry_reply, sizeof(dmi_retry_reply));

    result = wchlink_command_process(&fixture.command, stop_request,
                                     sizeof(stop_request), response,
                                     sizeof(response));
    assert(result.status == WCHLINK_SESSION_COMMAND_COMPLETED);
    assert(result.response_policy == WCHLINK_SESSION_RESPONSE_SESSION_END);
    wchlink_test_expect_bytes(response, result.response_length, stop_reply,
                              sizeof(stop_reply));
    assert(!wchlink_target_ports_info(&fixture.target).connected);
}

static void wchlink_test_command_direct_dmi_resume_completion(void) {
    const struct rvswd_target_info info = wchlink_test_info(
        0x30520528u, WCHLINK_TARGET_FAMILY_CH32V30X,
        WCHLINK_TEST_LOADER_DEFAULT, true);
    struct wchlink_test_fixture fixture;
    uint8_t response[9];
    const uint8_t halt_with_ack[] = {
        0x81u, 0x08u, 0x06u, 0x10u, 0xa0u, 0u, 0u, 0x01u, 0x02u};
    const uint8_t halt[] = {
        0x81u, 0x08u, 0x06u, 0x10u, 0x80u, 0u, 0u, 0x01u, 0x02u};
    const uint8_t clear_halt[] = {
        0x81u, 0x08u, 0x06u, 0x10u, 0u, 0u, 0u, 0x01u, 0x02u};
    const uint8_t resume[] = {
        0x81u, 0x08u, 0x06u, 0x10u, 0x40u, 0u, 0u, 0x01u, 0x02u};
    const uint8_t read_status[] = {
        0x81u, 0x08u, 0x06u, 0x11u, 0u, 0u, 0u, 0u, 0x01u};
    const uint8_t resume_with_hartreset[] = {
        0x81u, 0x08u, 0x06u, 0x10u, 0x60u, 0u, 0u, 0x01u, 0x02u};
    const uint8_t speed_request[] = {0x81u, 0x0cu, 0x02u, 0x06u, 0x03u};
    const uint8_t running_with_resume_ack_reply[] = {
        0x82u, 0x08u, 0x06u, 0x11u, 0u, 0x0fu, 0x0cu, 0x82u, 0u};
    struct wchlink_session_command_result result;

    wchlink_test_fixture_init(&fixture, info, true);

    // 单次 resume 必须完成真实握手，不能用 reset-and-run 替代继续执行
    wchlink_test_target_set_dmi(&fixture.target, 0x11u, 0x000c0c82u);
    result = wchlink_command_process(&fixture.command, resume, sizeof(resume),
                                     response, sizeof(response));
    assert(result.status == WCHLINK_SESSION_COMMAND_COMPLETED);
    assert(fixture.target.dmi[0x10u] == 0x00000001u);
    assert(wchlink_test_target_operation_count(
               &fixture.target, WCHLINK_TEST_TARGET_SOFT_RESET_AND_RUN) == 0u);
    assert(wchlink_test_target_operation_count(
               &fixture.target, WCHLINK_TEST_TARGET_RESUME_DMI) == 1u);

    result = wchlink_command_process(&fixture.command, read_status,
                                     sizeof(read_status), response,
                                     sizeof(response));
    wchlink_test_expect_bytes(response, result.response_length,
                              running_with_resume_ack_reply,
                              sizeof(running_with_resume_ack_reply));

    // halt 和 clear 仍逐笔透传，最终 resume 只完成调试握手，不重置目标
    (void)wchlink_command_process(&fixture.command, halt_with_ack,
                                  sizeof(halt_with_ack), response,
                                  sizeof(response));
    (void)wchlink_command_process(&fixture.command, read_status,
                                  sizeof(read_status), response,
                                  sizeof(response));
    (void)wchlink_command_process(&fixture.command, halt, sizeof(halt),
                                  response, sizeof(response));
    (void)wchlink_command_process(&fixture.command, halt, sizeof(halt),
                                  response, sizeof(response));
    (void)wchlink_command_process(&fixture.command, clear_halt,
                                  sizeof(clear_halt), response,
                                  sizeof(response));
    result = wchlink_command_process(&fixture.command, resume, sizeof(resume),
                                     response, sizeof(response));

    assert(result.status == WCHLINK_SESSION_COMMAND_COMPLETED);
    assert(wchlink_test_target_operation_count(
               &fixture.target, WCHLINK_TEST_TARGET_SOFT_RESET_AND_RUN) == 0u);
    assert(wchlink_test_target_operation_count(
               &fixture.target, WCHLINK_TEST_TARGET_RESUME_DMI) == 2u);

    // 已验证状态只交付一次，后续读取必须重新访问目标
    result = wchlink_command_process(&fixture.command, read_status,
                                     sizeof(read_status), response,
                                     sizeof(response));
    wchlink_test_expect_bytes(response, result.response_length,
                              running_with_resume_ack_reply,
                              sizeof(running_with_resume_ack_reply));

    wchlink_test_target_set_dmi(&fixture.target, 0x11u, 0x00000382u);
    result = wchlink_command_process(&fixture.command, read_status,
                                     sizeof(read_status), response,
                                     sizeof(response));
    assert(response[4] == 0u && response[5] == 0u && response[6] == 0x03u &&
           response[7] == 0x82u);

    // 目标未进入 running 时，resumereq 写入本身必须向主机报告失败
    result = wchlink_command_process(&fixture.command, resume, sizeof(resume),
                                     response, sizeof(response));
    assert(result.status == WCHLINK_SESSION_COMMAND_TARGET_FAILED);
    assert(wchlink_test_target_operation_count(
               &fixture.target, WCHLINK_TEST_TARGET_RESUME_DMI) == 3u);
    assert(wchlink_test_target_operation_count(
               &fixture.target, WCHLINK_TEST_TARGET_SOFT_RESET_AND_RUN) == 0u);
    assert(fixture.target.dmi[0x10u] == 0x00000001u);

    result = wchlink_command_process(&fixture.command, read_status,
                                     sizeof(read_status), response,
                                     sizeof(response));
    assert(response[4] == 0u && response[5] == 0u && response[6] == 0x03u &&
           response[7] == 0x82u);

    // 额外 reset 语义必须按普通 DMI 写入透传，不能误入 resume completion
    result = wchlink_command_process(
        &fixture.command, resume_with_hartreset,
        sizeof(resume_with_hartreset), response, sizeof(response));
    assert(result.status == WCHLINK_SESSION_COMMAND_COMPLETED);
    assert(fixture.target.dmi[0x10u] == 0x60000001u);
    assert(wchlink_test_target_operation_count(
               &fixture.target, WCHLINK_TEST_TARGET_RESUME_DMI) == 3u);

    // 任意插入命令都使一次性快照失效，后续 DMSTATUS 必须访问真实目标
    wchlink_test_target_set_dmi(&fixture.target, 0x11u, 0x000c0c82u);
    result = wchlink_command_process(&fixture.command, resume, sizeof(resume),
                                     response, sizeof(response));
    assert(result.status == WCHLINK_SESSION_COMMAND_COMPLETED);
    assert(wchlink_test_target_operation_count(
               &fixture.target, WCHLINK_TEST_TARGET_RESUME_DMI) == 4u);
    result = wchlink_command_process(&fixture.command, speed_request,
                                     sizeof(speed_request), response,
                                     sizeof(response));
    assert(result.status == WCHLINK_SESSION_COMMAND_COMPLETED);
    wchlink_test_target_set_dmi(&fixture.target, 0x11u, 0x00000382u);
    result = wchlink_command_process(&fixture.command, read_status,
                                     sizeof(read_status), response,
                                     sizeof(response));
    assert(response[4] == 0u && response[5] == 0u && response[6] == 0x03u &&
           response[7] == 0x82u);
}

static void wchlink_test_command_connect_failure(void) {
    const struct rvswd_target_info info = wchlink_test_info(
        0x03510611u, WCHLINK_TARGET_FAMILY_X03X,
        WCHLINK_TEST_LOADER_DEFAULT, true);
    struct wchlink_test_fixture fixture;
    uint8_t response[8];
    const uint8_t request[] = {0x81u, 0x0du, 0x01u, 0x02u};
    const uint8_t expected[] = {0x81u, 0x55u, 0x01u, 0x12u};
    struct wchlink_session_command_result result;

    wchlink_test_fixture_init(&fixture, info, false);
    wchlink_test_target_fail_next(
        &fixture.target, WCHLINK_TEST_TARGET_CONNECT,
        rvswd_target_result_failure(RVSWD_TARGET_RESULT_CONNECT, 0x12u,
                                    true));
    result = wchlink_command_process(&fixture.command, request,
                                     sizeof(request), response,
                                     sizeof(response));
    assert(result.status == WCHLINK_SESSION_COMMAND_TARGET_FAILED);
    wchlink_test_expect_bytes(response, result.response_length, expected,
                              sizeof(expected));
}

static void wchlink_test_command_config_and_reset(void) {
    const struct rvswd_target_info info = wchlink_test_info(
        0x03510611u, WCHLINK_TARGET_FAMILY_X03X,
        WCHLINK_TEST_LOADER_DEFAULT, true);
    struct wchlink_test_fixture fixture;
    uint8_t response[8];
    const uint8_t read_protection[] = {
        0x81u, WCHLINK_FAMILY_CONFIG, 0x01u,
        WCHLINK_CONFIG_READ_PROTECTION};
    const uint8_t enable_protection[] = {
        0x81u, WCHLINK_FAMILY_CONFIG, 0x01u,
        WCHLINK_CONFIG_ENABLE_PROTECTION};
    const uint8_t disable_protection[] = {
        0x81u, WCHLINK_FAMILY_CONFIG, 0x01u,
        WCHLINK_CONFIG_DISABLE_PROTECTION};
    const uint8_t write_protection[] = {
        0x81u, WCHLINK_FAMILY_CONFIG, 0x01u,
        WCHLINK_CONFIG_WRITE_PROTECTION};
    const uint8_t extended_disable_protection[] = {
        0x81u, WCHLINK_FAMILY_CONFIG, 0x08u,
        WCHLINK_CONFIG_DISABLE_PROTECTION,
        0xffu, 0xfeu, 0xfdu, 0xfcu, 0xfbu, 0xfau, 0xf9u};
    const uint8_t extended_enable_protection[] = {
        0x81u, WCHLINK_FAMILY_CONFIG, 0x02u,
        WCHLINK_CONFIG_ENABLE_PROTECTION, 0x00u};
    const uint8_t soft_reset[] = {
        0x81u, WCHLINK_FAMILY_RESET, 0x01u, WCHLINK_RESET_SOFT};
    const uint8_t normal_reset[] = {
        0x81u, WCHLINK_FAMILY_RESET, 0x01u, WCHLINK_RESET_NORMAL};
    const uint8_t run_reset[] = {
        0x81u, WCHLINK_FAMILY_RESET, 0x01u, WCHLINK_RESET_MRS_RUN};
    const uint8_t config_unprotected_reply[] = {
        0x82u, WCHLINK_FAMILY_CONFIG, 0x01u,
        WCHLINK_CONFIG_READ_UNPROTECTED};
    const uint8_t config_protected_reply[] = {
        0x82u, WCHLINK_FAMILY_CONFIG, 0x01u,
        WCHLINK_CONFIG_READ_PROTECTED};
    const uint8_t config_write_protected_reply[] = {
        0x82u, WCHLINK_FAMILY_CONFIG, 0x01u,
        WCHLINK_CONFIG_WRITE_PROTECTED};
    const uint8_t config_enable_reply[] = {
        0x82u, WCHLINK_FAMILY_CONFIG, 0x01u,
        WCHLINK_CONFIG_ENABLE_PROTECTION};
    const uint8_t config_disable_reply[] = {
        0x82u, WCHLINK_FAMILY_CONFIG, 0x01u,
        WCHLINK_CONFIG_DISABLE_PROTECTION};
    const uint8_t config_unsupported_reply[] = {
        0x81u, WCHLINK_FAMILY_CONFIG, 0x01u, 0x02u};
    const uint8_t soft_reset_reply[] = {
        0x82u, WCHLINK_FAMILY_RESET, 0x01u, WCHLINK_RESET_SOFT};
    const uint8_t normal_reset_reply[] = {
        0x82u, WCHLINK_FAMILY_RESET, 0x01u, 0x00u};
    const uint8_t run_reset_reply[] = {
        0x82u, WCHLINK_FAMILY_RESET, 0x01u, WCHLINK_RESET_MRS_RUN};
    const uint8_t reset_error_reply[] = {
        0x81u, WCHLINK_FAMILY_RESET, 0x01u, 0x02u};
    struct wchlink_session_command_result result;

    // Protection command 通过 target port 返回状态，扩展解除帧完整传递 Option Bytes
    wchlink_test_fixture_init(&fixture, info, true);
    result = wchlink_command_process(
        &fixture.command, read_protection, sizeof(read_protection), response,
        sizeof(response));
    assert(result.status == WCHLINK_SESSION_COMMAND_COMPLETED);
    wchlink_test_expect_bytes(response, result.response_length,
                              config_unprotected_reply,
                              sizeof(config_unprotected_reply));

    result = wchlink_command_process(
        &fixture.command, enable_protection, sizeof(enable_protection), response,
        sizeof(response));
    assert(result.status == WCHLINK_SESSION_COMMAND_COMPLETED);
    wchlink_test_expect_bytes(response, result.response_length,
                              config_enable_reply,
                              sizeof(config_enable_reply));

    result = wchlink_command_process(
        &fixture.command, read_protection, sizeof(read_protection), response,
        sizeof(response));
    wchlink_test_expect_bytes(response, result.response_length,
                              config_protected_reply,
                              sizeof(config_protected_reply));

    result = wchlink_command_process(
        &fixture.command, write_protection, sizeof(write_protection), response,
        sizeof(response));
    wchlink_test_expect_bytes(response, result.response_length,
                              config_write_protected_reply,
                              sizeof(config_write_protected_reply));

    result = wchlink_command_process(
        &fixture.command, disable_protection, sizeof(disable_protection),
        response, sizeof(response));
    wchlink_test_expect_bytes(response, result.response_length,
                              config_disable_reply,
                              sizeof(config_disable_reply));
    result = wchlink_command_process(
        &fixture.command, read_protection, sizeof(read_protection), response,
        sizeof(response));
    wchlink_test_expect_bytes(response, result.response_length,
                              config_unprotected_reply,
                              sizeof(config_unprotected_reply));

    result = wchlink_command_process(
        &fixture.command, extended_disable_protection,
        sizeof(extended_disable_protection), response, sizeof(response));
    assert(result.status == WCHLINK_SESSION_COMMAND_COMPLETED);
    wchlink_test_expect_bytes(response, result.response_length,
                              config_disable_reply,
                              sizeof(config_disable_reply));
    wchlink_test_expect_bytes(
        fixture.target.option_bytes, sizeof(fixture.target.option_bytes),
        &extended_disable_protection[4],
        sizeof(extended_disable_protection) - 4u);

    result = wchlink_command_process(
        &fixture.command, extended_enable_protection,
        sizeof(extended_enable_protection),
        response, sizeof(response));
    assert(result.status == WCHLINK_SESSION_COMMAND_MALFORMED);
    wchlink_test_expect_bytes(response, result.response_length,
                              config_unsupported_reply,
                              sizeof(config_unsupported_reply));

    // 三种 reset 子命令保持各自 reply，target 失败统一映射为 unsupported
    result = wchlink_command_process(&fixture.command, soft_reset,
                                     sizeof(soft_reset), response,
                                     sizeof(response));
    wchlink_test_expect_bytes(response, result.response_length,
                              soft_reset_reply, sizeof(soft_reset_reply));
    result = wchlink_command_process(&fixture.command, normal_reset,
                                     sizeof(normal_reset), response,
                                     sizeof(response));
    wchlink_test_expect_bytes(response, result.response_length,
                              normal_reset_reply, sizeof(normal_reset_reply));
    result = wchlink_command_process(&fixture.command, run_reset,
                                     sizeof(run_reset), response,
                                     sizeof(response));
    wchlink_test_expect_bytes(response, result.response_length, run_reset_reply,
                              sizeof(run_reset_reply));

    wchlink_test_target_fail_next(
        &fixture.target, WCHLINK_TEST_TARGET_SOFT_RESET_AND_RUN,
        rvswd_target_result_failure(RVSWD_TARGET_RESULT_RESET, 0x15u, false));
    result = wchlink_command_process(&fixture.command, soft_reset,
                                     sizeof(soft_reset), response,
                                     sizeof(response));
    assert(result.status == WCHLINK_SESSION_COMMAND_TARGET_FAILED);
    wchlink_test_expect_bytes(response, result.response_length,
                              reset_error_reply, sizeof(reset_error_reply));
}

static void wchlink_test_command_control_and_device_mode(void) {
    const struct rvswd_target_info info = wchlink_test_info(
        0x03510611u, WCHLINK_TARGET_FAMILY_X03X,
        WCHLINK_TEST_LOADER_DEFAULT, true);
    struct wchlink_test_fixture fixture;
    uint8_t response[16];
    uint8_t programmed[] = {0x00u, 0x11u, 0x22u, 0x33u};
    uint8_t erased[sizeof(programmed)];
    const uint8_t speed_request[] = {
        0x81u, WCHLINK_FAMILY_SPEED, 0x02u, WCHLINK_TARGET_FAMILY_X03X, 0x01u};
    const uint8_t set_chip_type[] = {
        0x81u, WCHLINK_FAMILY_CONTROL, 0x01u,
        WCHLINK_CONTROL_SET_CHIP_TYPE};
    const uint8_t erase_request[] = {
        0x81u, WCHLINK_FAMILY_CONTROL, 0x02u,
        WCHLINK_CONTROL_CLEAR_CODE_FLASH, WCHLINK_TARGET_FAMILY_X03X};
    const uint8_t power_3v3_on[] = {
        0x81u, WCHLINK_FAMILY_CONTROL, 0x01u,
        WCHLINK_CONTROL_POWER_3V3_ON};
    const uint8_t power_3v3_off[] = {
        0x81u, WCHLINK_FAMILY_CONTROL, 0x01u,
        WCHLINK_CONTROL_POWER_3V3_OFF};
    const uint8_t power_5v_on[] = {
        0x81u, WCHLINK_FAMILY_CONTROL, 0x01u,
        WCHLINK_CONTROL_POWER_5V_ON};
    const uint8_t power_5v_off[] = {
        0x81u, WCHLINK_FAMILY_CONTROL, 0x01u,
        WCHLINK_CONTROL_POWER_5V_OFF};
    const uint8_t check_qe[] = {
        0x81u, WCHLINK_FAMILY_CONTROL, 0x01u, WCHLINK_CONTROL_CHECK_QE};
    const uint8_t enable_qe[] = {
        0x81u, WCHLINK_FAMILY_CONTROL, 0x01u, WCHLINK_CONTROL_ENABLE_QE};
    const uint8_t identify_request[] = {
        0x81u, WCHLINK_FAMILY_CONTROL, 0x01u, WCHLINK_CONTROL_IDENTIFY};
    const uint8_t mode_query[] = {
        0x81u, WCHLINK_FAMILY_DEVICE_MODE, 0x01u,
        WCHLINK_DEVICE_MODE_QUERY};
    const uint8_t mode_iap[] = {
        0x81u, WCHLINK_FAMILY_DEVICE_MODE, 0x01u, WCHLINK_DEVICE_MODE_IAP};
    const uint8_t speed_reply[] = {
        0x82u, WCHLINK_FAMILY_SPEED, 0x01u, 0x01u};
    const uint8_t connect_reply[] = {
        0x82u, WCHLINK_FAMILY_CONTROL, 0x05u, WCHLINK_TARGET_FAMILY_X03X,
        0x03u, 0x51u, 0x06u, 0x11u};
    const uint8_t erase_reply[] = {
        0x82u, WCHLINK_FAMILY_CONTROL, 0x01u,
        WCHLINK_CONTROL_CLEAR_CODE_FLASH};
    const uint8_t power_3v3_on_reply[] = {
        0x82u, WCHLINK_FAMILY_CONTROL, 0x01u,
        WCHLINK_CONTROL_POWER_3V3_ON};
    const uint8_t power_3v3_off_reply[] = {
        0x82u, WCHLINK_FAMILY_CONTROL, 0x01u,
        WCHLINK_CONTROL_POWER_3V3_OFF};
    const uint8_t power_5v_on_reply[] = {
        0x82u, WCHLINK_FAMILY_CONTROL, 0x01u,
        WCHLINK_CONTROL_POWER_5V_ON};
    const uint8_t power_5v_off_reply[] = {
        0x82u, WCHLINK_FAMILY_CONTROL, 0x01u,
        WCHLINK_CONTROL_POWER_5V_OFF};
    const uint8_t qe_enabled_reply[] = {
        0x82u, WCHLINK_FAMILY_CONTROL, 0x01u, 0x00u};
    const uint8_t qe_enable_reply[] = {
        0x82u, WCHLINK_FAMILY_CONTROL, 0x01u,
        WCHLINK_CONTROL_ENABLE_QE};
    const uint8_t identity_reply[] = {
        0x82u, WCHLINK_FAMILY_CONTROL, 0x04u, 0x03u,
        0x03u, 0x12u, 0x00u};
    const uint8_t mode_query_reply[] = {
        0x82u, WCHLINK_FAMILY_DEVICE_MODE, 0x01u,
        WCHLINK_DEVICE_MODE_QUERY};
    struct wchlink_session_command_result result;

    // Disconnected control 路径保留 family hint、20 ms 稳定延时和连接回复
    wchlink_test_fixture_init(&fixture, info, false);
    result = wchlink_command_process(&fixture.command, speed_request,
                                     sizeof(speed_request), response,
                                     sizeof(response));
    assert(wchlink_test_target_has_family_hint(
        &fixture.target, WCHLINK_TARGET_FAMILY_X03X));
    wchlink_test_expect_bytes(response, result.response_length, speed_reply,
                              sizeof(speed_reply));
    result = wchlink_command_process(&fixture.command, set_chip_type,
                                     sizeof(set_chip_type), response,
                                     sizeof(response));
    assert(wchlink_test_last_delay_ms == 20u);
    assert(wchlink_target_ports_info(&fixture.target).connected);
    assert(fixture.target.requested_speed == 0x01u);
    wchlink_test_expect_bytes(response, result.response_length, connect_reply,
                              sizeof(connect_reply));

    assert(wchlink_test_target_store(&fixture.target, 0u, programmed,
                                     sizeof(programmed)));
    result = wchlink_command_process(&fixture.command, erase_request,
                                     sizeof(erase_request), response,
                                     sizeof(response));
    assert(result.status == WCHLINK_SESSION_COMMAND_COMPLETED);
    wchlink_test_expect_bytes(response, result.response_length, erase_reply,
                              sizeof(erase_reply));
    assert(wchlink_test_target_load(&fixture.target, 0u, erased,
                                    sizeof(erased)));
    for (size_t i = 0u; i < sizeof(erased); ++i) {
        assert(erased[i] == 0xffu);
    }

    // Power command 的驱动调用和 identify 的总线释放均从 command seam 观察
    result = wchlink_command_process(&fixture.command, power_3v3_on,
                                     sizeof(power_3v3_on), response,
                                     sizeof(response));
    assert(result.status == WCHLINK_SESSION_COMMAND_COMPLETED);
    assert(!wchlink_test_dp_pullup_enabled);
    assert(wchlink_test_power_switch_enabled);
    wchlink_test_expect_bytes(response, result.response_length,
                              power_3v3_on_reply,
                              sizeof(power_3v3_on_reply));
    result = wchlink_command_process(&fixture.command, power_3v3_off,
                                     sizeof(power_3v3_off), response,
                                     sizeof(response));
    assert(result.status == WCHLINK_SESSION_COMMAND_COMPLETED);
    assert(!wchlink_test_dp_pullup_enabled);
    assert(!wchlink_test_power_switch_enabled);
    wchlink_test_expect_bytes(response, result.response_length,
                              power_3v3_off_reply,
                              sizeof(power_3v3_off_reply));
    assert(!wchlink_target_ports_info(&fixture.target).connected);
    result = wchlink_command_process(&fixture.command, power_5v_on,
                                     sizeof(power_5v_on), response,
                                     sizeof(response));
    assert(result.status == WCHLINK_SESSION_COMMAND_COMPLETED);
    assert(wchlink_test_power_switch_enabled);
    wchlink_test_expect_bytes(response, result.response_length,
                              power_5v_on_reply, sizeof(power_5v_on_reply));
    result = wchlink_command_process(&fixture.command, power_5v_off,
                                     sizeof(power_5v_off), response,
                                     sizeof(response));
    assert(result.status == WCHLINK_SESSION_COMMAND_COMPLETED);
    assert(!wchlink_test_power_switch_enabled);
    wchlink_test_expect_bytes(response, result.response_length,
                              power_5v_off_reply,
                              sizeof(power_5v_off_reply));

    // 内置存储的 QE 查询和启用是 Link 层幂等操作，不依赖目标连接状态
    result = wchlink_command_process(&fixture.command, check_qe,
                                     sizeof(check_qe), response,
                                     sizeof(response));
    assert(result.status == WCHLINK_SESSION_COMMAND_COMPLETED);
    wchlink_test_expect_bytes(response, result.response_length,
                              qe_enabled_reply, sizeof(qe_enabled_reply));
    result = wchlink_command_process(&fixture.command, enable_qe,
                                     sizeof(enable_qe), response,
                                     sizeof(response));
    assert(result.status == WCHLINK_SESSION_COMMAND_COMPLETED);
    wchlink_test_expect_bytes(response, result.response_length, qe_enable_reply,
                              sizeof(qe_enable_reply));

    wchlink_test_fixture_init(&fixture, info, true);
    result = wchlink_command_process(&fixture.command, identify_request,
                                     sizeof(identify_request), response,
                                     sizeof(response));
    assert(!wchlink_target_ports_info(&fixture.target).connected);
    wchlink_test_expect_bytes(response, result.response_length, identity_reply,
                              sizeof(identity_reply));

    result = wchlink_command_process(&fixture.command, mode_query,
                                     sizeof(mode_query), response,
                                     sizeof(response));
    wchlink_test_expect_bytes(response, result.response_length,
                              mode_query_reply, sizeof(mode_query_reply));
    result = wchlink_command_process(&fixture.command, mode_iap,
                                     sizeof(mode_iap), response,
                                     sizeof(response));
    assert(result.status == WCHLINK_SESSION_COMMAND_NO_RESPONSE);
    assert(result.action == WCHLINK_SESSION_ACTION_ENTER_ISP);
    assert(result.response_length == 0u);
}

static void wchlink_test_command_memory_type(void) {
    const struct rvswd_target_info info = wchlink_test_info(
        0x30700528u, WCHLINK_TARGET_FAMILY_CH32V30X,
        WCHLINK_TEST_LOADER_DEFAULT, true);
    struct wchlink_test_fixture fixture;
    uint8_t response[8];
    const uint8_t old_query[] = {
        0x81u, WCHLINK_FAMILY_CONTROL, 0x01u,
        WCHLINK_CONTROL_GET_ROMRAM_OLD};
    const uint8_t old_set[] = {
        0x81u, WCHLINK_FAMILY_CONTROL, 0x02u,
        WCHLINK_CONTROL_SET_ROMRAM_OLD, 0x01u};
    const uint8_t new_query[] = {
        0x81u, WCHLINK_FAMILY_CONTROL, 0x01u,
        WCHLINK_CONTROL_GET_ROMRAM_NEW};
    const uint8_t new_set[] = {
        0x81u, WCHLINK_FAMILY_CONTROL, 0x02u,
        WCHLINK_CONTROL_SET_ROMRAM_NEW, 0x07u};
    const uint8_t invalid_new_set[] = {
        0x81u, WCHLINK_FAMILY_CONTROL, 0x02u,
        WCHLINK_CONTROL_SET_ROMRAM_NEW, 0x02u};
    const uint8_t old_query_initial_reply[] = {
        0x82u, WCHLINK_FAMILY_CONTROL, 0x01u, 0x02u};
    const uint8_t old_set_reply[] = {
        0x82u, WCHLINK_FAMILY_CONTROL, 0x01u,
        WCHLINK_CONTROL_SET_ROMRAM_OLD};
    const uint8_t new_query_after_old_set_reply[] = {
        0x82u, WCHLINK_FAMILY_CONTROL, 0x01u, 0x03u};
    const uint8_t new_set_reply[] = {
        0x82u, WCHLINK_FAMILY_CONTROL, 0x01u,
        WCHLINK_CONTROL_SET_ROMRAM_NEW};
    const uint8_t new_query_final_reply[] = {
        0x82u, WCHLINK_FAMILY_CONTROL, 0x01u, 0x07u};
    const uint8_t invalid_reply[] = {0x81u, 0x55u, 0x01u, 0x4bu};
    struct wchlink_session_command_result result;

    // 旧查询读取 USER[7:6]，新查询读取 USER[7:5]
    wchlink_test_fixture_init(&fixture, info, true);
    result = wchlink_command_process(&fixture.command, old_query,
                                     sizeof(old_query), response,
                                     sizeof(response));
    wchlink_test_expect_bytes(response, result.response_length,
                              old_query_initial_reply,
                              sizeof(old_query_initial_reply));
    result = wchlink_command_process(&fixture.command, old_set, sizeof(old_set),
                                     response, sizeof(response));
    wchlink_test_expect_bytes(response, result.response_length, old_set_reply,
                              sizeof(old_set_reply));
    assert(fixture.target.option_bytes[0] == 0x7fu);
    result = wchlink_command_process(&fixture.command, new_query,
                                     sizeof(new_query), response,
                                     sizeof(response));
    wchlink_test_expect_bytes(response, result.response_length,
                              new_query_after_old_set_reply,
                              sizeof(new_query_after_old_set_reply));

    result = wchlink_command_process(&fixture.command, new_set, sizeof(new_set),
                                     response, sizeof(response));
    wchlink_test_expect_bytes(response, result.response_length, new_set_reply,
                              sizeof(new_set_reply));
    assert(fixture.target.option_bytes[0] == 0xffu);
    result = wchlink_command_process(&fixture.command, new_query,
                                     sizeof(new_query), response,
                                     sizeof(response));
    wchlink_test_expect_bytes(response, result.response_length,
                              new_query_final_reply,
                              sizeof(new_query_final_reply));

    result = wchlink_command_process(&fixture.command, invalid_new_set,
                                     sizeof(invalid_new_set), response,
                                     sizeof(response));
    assert(result.status == WCHLINK_SESSION_COMMAND_TARGET_FAILED);
    wchlink_test_expect_bytes(response, result.response_length, invalid_reply,
                              sizeof(invalid_reply));
}

static void wchlink_test_command_ch58x_59x_info_stop(void) {
    const struct rvswd_target_info info = wchlink_test_info(
        0x82000000u, WCHLINK_TARGET_FAMILY_CH58X,
        WCHLINK_TEST_LOADER_LEGACY, false);
    struct wchlink_test_fixture fixture;
    uint8_t response[20];
    const uint8_t info_request[] = {0x81u, WCHLINK_FAMILY_INFO};
    const uint8_t stop_request[] = {
        0x81u, WCHLINK_FAMILY_CONTROL, 0x01u, WCHLINK_CONTROL_STOP};
    const uint8_t expected[] = {
        0x82u, WCHLINK_FAMILY_CONTROL, 0x01u, 0xffu, 0x82u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
    struct wchlink_session_command_result result;

    // CH58X/CH59X 的 INFO 查询使随后 STOP 返回目标信息，同时结束 response 生命周期
    wchlink_test_fixture_init(&fixture, info, true);
    result = wchlink_command_process(&fixture.command, info_request,
                                     sizeof(info_request), response,
                                     sizeof(response));
    wchlink_test_expect_bytes(response, result.response_length, expected,
                              sizeof(expected));
    result = wchlink_command_process(&fixture.command, stop_request,
                                     sizeof(stop_request), response,
                                     sizeof(response));
    assert(result.status == WCHLINK_SESSION_COMMAND_COMPLETED);
    assert(result.response_policy == WCHLINK_SESSION_RESPONSE_SESSION_END);
    assert(!wchlink_target_ports_info(&fixture.target).connected);
    wchlink_test_expect_bytes(response, result.response_length, expected,
                              sizeof(expected));
}

static void wchlink_test_command_repeat_and_abort(void) {
    const struct rvswd_target_info info = wchlink_test_info(
        0x03510611u, WCHLINK_TARGET_FAMILY_X03X,
        WCHLINK_TEST_LOADER_DEFAULT, true);
    struct wchlink_test_fixture fixture;
    uint8_t response[8];
    const uint8_t write_request[] = {
        0x81u,
        0x01u,
        0x08u,
        0x08u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0x04u,
    };
    const uint8_t loader_request[] = {0x81u, 0x02u, 0x01u, 0x05u};
    const uint8_t abort_request[] = {0x81u, 0x02u, 0x01u, 0x08u};
    struct wchlink_session_command_result result;

    wchlink_test_fixture_init(&fixture, info, true);
    result = wchlink_command_process(&fixture.command, write_request,
                                     sizeof(write_request), response,
                                     sizeof(response));
    assert(result.status == WCHLINK_SESSION_COMMAND_COMPLETED);
    result = wchlink_command_process(&fixture.command, loader_request,
                                     sizeof(loader_request), response,
                                     sizeof(response));
    assert(result.status == WCHLINK_SESSION_COMMAND_COMPLETED);
    assert(wchlink_transfer_next_io(&fixture.transfer) ==
           WCHLINK_TRANSFER_IO_DATA_OUT);

    // 重复开始沿用 LinkE 行为，重新建立同一 loader 接收阶段
    result = wchlink_command_process(&fixture.command, loader_request,
                                     sizeof(loader_request), response,
                                     sizeof(response));
    assert(result.status == WCHLINK_SESSION_COMMAND_COMPLETED);
    assert(wchlink_transfer_next_io(&fixture.transfer) ==
           WCHLINK_TRANSFER_IO_DATA_OUT);

    result = wchlink_command_process(&fixture.command, abort_request,
                                     sizeof(abort_request), response,
                                     sizeof(response));
    assert(result.status == WCHLINK_SESSION_COMMAND_COMPLETED);
    assert(wchlink_transfer_next_io(&fixture.transfer) ==
           WCHLINK_TRANSFER_IO_NONE);
}

static void wchlink_test_command_program_end_resets_and_halts(void) {
    const struct rvswd_target_info info = wchlink_test_info(
        0x03510611u, WCHLINK_TARGET_FAMILY_X03X,
        WCHLINK_TEST_LOADER_DEFAULT, true);
    struct wchlink_test_fixture fixture;
    uint8_t response[8];
    const uint8_t end_request[] = {0x81u, 0x02u, 0x01u, 0x08u};
    const uint8_t expected[] = {0x81u, 0x02u, 0x01u, 0x5au};
    struct rvswd_target_result failure = rvswd_target_result_failure(
        RVSWD_TARGET_RESULT_RESET, 0x5au, false);
    struct wchlink_session_command_result result;

    wchlink_test_fixture_init(&fixture, info, true);
    wchlink_test_target_fail_next(
        &fixture.target, WCHLINK_TEST_TARGET_RESET_AND_HALT, failure);

    // Program End 必须离开 loader 的 ebreak 上下文，失败不能静默返回成功
    result = wchlink_command_process(&fixture.command, end_request,
                                     sizeof(end_request), response,
                                     sizeof(response));
    assert(result.status == WCHLINK_SESSION_COMMAND_TARGET_FAILED);
    wchlink_test_expect_bytes(response, result.response_length, expected,
                              sizeof(expected));
}

static void wchlink_test_transfer_read(void) {
    const struct rvswd_target_info info = wchlink_test_info(
        0x03510611u, WCHLINK_TARGET_FAMILY_X03X,
        WCHLINK_TEST_LOADER_DEFAULT, true);
    struct wchlink_test_fixture fixture;
    const uint8_t memory[] = {
        0x44u,
        0x33u,
        0x22u,
        0x11u,
        0x88u,
        0x77u,
        0x66u,
        0x55u,
    };
    const uint8_t expected[] = {
        0x11u,
        0x22u,
        0x33u,
        0x44u,
        0x55u,
        0x66u,
        0x77u,
        0x88u,
    };
    uint8_t output[8];
    struct rvswd_target_result failure;

    // data IN 按 32 位大端输出，容量不足一个 word 时保留剩余游标
    wchlink_test_fixture_init(&fixture, info, true);
    assert(wchlink_test_target_store(&fixture.target, 0x100u, memory,
                                     sizeof(memory)));
    wchlink_transfer_prepare_read(&fixture.transfer, 0x100u, sizeof(memory));
    assert(wchlink_transfer_next_io(&fixture.transfer) ==
           WCHLINK_TRANSFER_IO_NONE);
    wchlink_transfer_begin_read(&fixture.transfer);
    assert(wchlink_transfer_next_io(&fixture.transfer) ==
           WCHLINK_TRANSFER_IO_DATA_IN);
    assert(wchlink_transfer_read_data(&fixture.transfer, output, 6u) == 4u);
    assert(memcmp(output, expected, 4u) == 0);
    assert(wchlink_transfer_read_data(&fixture.transfer, &output[4], 4u) ==
           4u);
    assert(memcmp(output, expected, sizeof(expected)) == 0);
    assert(wchlink_transfer_next_io(&fixture.transfer) ==
           WCHLINK_TRANSFER_IO_NONE);

    failure = rvswd_target_result_failure(RVSWD_TARGET_RESULT_MEMORY, 0x15u,
                                          false);
    failure.address = 0x100u;
    wchlink_transfer_prepare_read(&fixture.transfer, 0x100u, 4u);
    wchlink_transfer_begin_read(&fixture.transfer);
    wchlink_test_target_fail_next(
        &fixture.target, WCHLINK_TEST_TARGET_READ_MEMORY, failure);
    assert(wchlink_transfer_read_data(&fixture.transfer, output,
                                      sizeof(output)) == 0u);
    assert(wchlink_transfer_next_io(&fixture.transfer) ==
           WCHLINK_TRANSFER_IO_NONE);
}

static void wchlink_test_command_official_memory_reads(void) {
    const struct rvswd_target_info info = wchlink_test_info(
        0x30700528u, WCHLINK_TARGET_FAMILY_CH32V30X,
        WCHLINK_TEST_LOADER_DEFAULT, true);
    const uint8_t memory[] = {
        0x10u,
        0x21u,
        0x32u,
        0x43u,
        0x54u,
        0x65u,
        0x76u,
        0x87u,
    };
    const uint8_t expected_reply[] = {0x82u, 0x03u, 0x01u, 0x01u};
    const uint8_t expected_flash_reply[] = {0x82u, 0x02u, 0x01u, 0x0cu};
    const uint8_t windows_region[] = {
        0x81u,
        0x03u,
        0x08u,
        0x00u,
        0x00u,
        0x01u,
        0x00u,
        0x00u,
        0x00u,
        0x00u,
        0x08u,
    };
    const uint8_t windows_read[] = {0x81u, 0x02u, 0x01u, 0x0cu};
    struct wchlink_test_fixture fixture;
    struct wchlink_session_command_result result;
    uint8_t response[16];
    uint8_t output[8];

    wchlink_test_fixture_init(&fixture, info, true);
    assert(wchlink_test_target_store(&fixture.target, 0x100u, memory,
                                     sizeof(memory)));

    // Windows WCH-LinkUtility 的普通读路径使用独立的读 family 和 0x0c 命令
    result = wchlink_command_process(
        &fixture.command, windows_region, sizeof(windows_region), response,
        sizeof(response));
    wchlink_test_expect_bytes(response, result.response_length,
                              expected_reply, sizeof(expected_reply));
    result = wchlink_command_process(&fixture.command, windows_read,
                                     sizeof(windows_read), response,
                                     sizeof(response));
    wchlink_test_expect_bytes(response, result.response_length,
                              expected_flash_reply,
                              sizeof(expected_flash_reply));
    assert(wchlink_transfer_next_io(&fixture.transfer) ==
           WCHLINK_TRANSFER_IO_DATA_IN);
    assert(wchlink_transfer_read_data(&fixture.transfer, output,
                                      sizeof(output)) == sizeof(output));
    {
        const uint8_t expected_data[] = {
            0x43u,
            0x32u,
            0x21u,
            0x10u,
            0x87u,
            0x76u,
            0x65u,
            0x54u,
        };

        assert(memcmp(output, expected_data, sizeof(output)) == 0);
    }
}

static struct wchlink_transfer_finish_result
wchlink_test_finish_default_loader(struct wchlink_test_fixture *fixture,
                                   uint32_t address, uint32_t length) {
    uint8_t loader[WCHLINK_TRANSFER_PACKET_CAPACITY];

    memset(loader, 0x5au, sizeof(loader));
    wchlink_transfer_prepare_write(&fixture->transfer, address, length);
    assert(wchlink_transfer_start_loader(&fixture->transfer));
    wchlink_transfer_write_data(&fixture->transfer, loader, sizeof(loader));
    wchlink_transfer_write_data(&fixture->transfer, loader, sizeof(loader));
    return wchlink_transfer_finish_loader(&fixture->transfer, 0x07u);
}

static void wchlink_test_transfer_chunk_boundary(void) {
    const struct rvswd_target_info info = wchlink_test_info(
        0x03510611u, WCHLINK_TARGET_FAMILY_X03X,
        WCHLINK_TEST_LOADER_DEFAULT, true);
    struct wchlink_test_fixture fixture;
    struct wchlink_transfer_finish_result finish;
    struct wchlink_test_execute execute;
    uint8_t packet[WCHLINK_TRANSFER_PACKET_CAPACITY];
    const uint8_t tail[] = {0x11u, 0x22u, 0x33u, 0x44u};

    // 4096 字节完成后继续保留 OUT，尾块完成后只留下最终 IN reply
    wchlink_test_fixture_init(&fixture, info, true);
    finish = wchlink_test_finish_default_loader(
        &fixture, 0x08000000u, WCHLINK_TRANSFER_CHUNK_CAPACITY + sizeof(tail));
    assert(finish.status == WCHLINK_TRANSFER_FINISH_READY);
    assert(wchlink_transfer_start_flash(&fixture.transfer, 0x02u));

    memset(packet, 0x3cu, sizeof(packet));
    for (size_t offset = 0u; offset < WCHLINK_TRANSFER_CHUNK_CAPACITY;
         offset += sizeof(packet)) {
        wchlink_transfer_write_data(&fixture.transfer, packet,
                                    sizeof(packet));
    }
    assert(wchlink_test_take_status(&fixture.transfer) == 0x04u);
    assert(wchlink_test_target_last_execute(&fixture.target, &execute));
    assert(execute.mode == 0x0cu);
    assert(execute.address == 0x08000000u);
    assert(execute.length == WCHLINK_TRANSFER_CHUNK_CAPACITY);
    assert(execute.data_address == 0x20001000u);
    assert(wchlink_transfer_next_io(&fixture.transfer) ==
           WCHLINK_TRANSFER_IO_DATA_OUT);

    wchlink_test_target_set_execute_value(&fixture.target, 8u);
    wchlink_transfer_write_data(&fixture.transfer, tail, sizeof(tail));
    assert(wchlink_test_take_status(&fixture.transfer) == 0x03u);
    assert(wchlink_test_target_last_execute(&fixture.target, &execute));
    assert(execute.address == 0x08001000u);
    assert(execute.length == sizeof(tail));
    assert(wchlink_transfer_next_io(&fixture.transfer) ==
           WCHLINK_TRANSFER_IO_NONE);
}

static void wchlink_test_transfer_ch58x_59x_padding(void) {
    const struct rvswd_target_info info = wchlink_test_info(
        0x82000000u, WCHLINK_TARGET_FAMILY_CH58X,
        WCHLINK_TEST_LOADER_LEGACY, false);
    struct wchlink_test_fixture fixture;
    struct wchlink_transfer_finish_result finish;
    struct wchlink_test_execute execute;
    uint8_t packet[WCHLINK_TRANSFER_PACKET_CAPACITY];
    uint8_t target_page[WCHLINK_TRANSFER_PACKET_CAPACITY];
    uint8_t checksum[4];
    const uint8_t loader[] = {0x13u, 0x37u, 0x42u, 0x24u};

    // CH58X/CH59X OpenOCD 路径接收完整 4 KiB 窗口，loader 只消费实际数据和页尾 0xff
    wchlink_test_fixture_init(&fixture, info, true);
    wchlink_transfer_prepare_write(&fixture.transfer, 0x100u, 4u);
    assert(wchlink_transfer_start_loader(&fixture.transfer));
    wchlink_transfer_write_data(&fixture.transfer, loader, sizeof(loader));
    finish = wchlink_transfer_finish_loader(&fixture.transfer, 0x0bu);
    assert(finish.status == WCHLINK_TRANSFER_FINISH_READY);

    memset(packet, 0xa5, sizeof(packet));
    packet[0] = 0x01u;
    packet[1] = 0x02u;
    packet[2] = 0x03u;
    packet[3] = 0x04u;
    for (size_t offset = 0u; offset < WCHLINK_TRANSFER_CHUNK_CAPACITY;
         offset += sizeof(packet)) {
        wchlink_transfer_write_data(&fixture.transfer, packet,
                                    sizeof(packet));
    }
    assert(wchlink_test_take_status(&fixture.transfer) == 0x04u);
    assert(wchlink_test_target_load(&fixture.target, 0x20005000u, target_page,
                                    sizeof(target_page)));
    assert(memcmp(target_page, packet, 4u) == 0);
    for (size_t offset = 4u; offset < sizeof(target_page); ++offset) {
        assert(target_page[offset] == 0xffu);
    }
    assert(wchlink_test_target_load(&fixture.target, 0x20006010u, checksum,
                                    sizeof(checksum)));
    {
        const uint8_t expected_checksum[] = {0xc2u, 0x01u, 0x03u, 0x04u};

        assert(memcmp(checksum, expected_checksum,
                      sizeof(expected_checksum)) == 0);
    }
    assert(wchlink_test_target_last_execute(&fixture.target, &execute));
    assert(execute.mode == 0x10u);
    assert(execute.address == 0x100u);
    assert(execute.length == 4u);
    assert(execute.data_address == 0x20005000u);
}

static void wchlink_test_transfer_l103_checksum(void) {
    const struct rvswd_target_info info = wchlink_test_info(
        0x10300500u, WCHLINK_TARGET_FAMILY_CH32L10X,
        WCHLINK_TEST_LOADER_L103, true);
    const uint8_t packet[] = {0x01u, 0x02u, 0x03u, 0x04u};
    struct wchlink_test_fixture fixture;
    struct wchlink_transfer_finish_result finish;
    uint8_t checksum[sizeof(packet)];

    wchlink_test_fixture_init(&fixture, info, true);
    finish = wchlink_test_finish_default_loader(&fixture, 0x08000000u,
                                                sizeof(packet));
    assert(finish.status == WCHLINK_TRANSFER_FINISH_READY);
    assert(wchlink_transfer_start_flash(&fixture.transfer, 0x03u));
    wchlink_transfer_write_data(&fixture.transfer, packet, sizeof(packet));
    assert(wchlink_test_take_status(&fixture.transfer) == 0x04u);
    assert(wchlink_test_target_load(&fixture.target, 0x20002010u, checksum,
                                    sizeof(checksum)));
    assert(memcmp(checksum, packet, sizeof(packet)) == 0);
}

static void wchlink_test_transfer_v30x_checksum(void) {
    const struct rvswd_target_info info = wchlink_test_info(
        0x30700528u, WCHLINK_TARGET_FAMILY_CH32V30X,
        WCHLINK_TEST_LOADER_DEFAULT, true);
    const uint8_t packet[] = {0x01u, 0x02u, 0x03u, 0x04u};
    struct wchlink_test_fixture fixture;
    struct wchlink_transfer_finish_result finish;
    uint8_t checksum[sizeof(packet)];

    // V30X 的 flash_op307 在编程加校验前需要写入 checksum mailbox
    wchlink_test_fixture_init(&fixture, info, true);
    finish = wchlink_test_finish_default_loader(&fixture, 0x08000000u,
                                                sizeof(packet));
    assert(finish.status == WCHLINK_TRANSFER_FINISH_READY);
    assert(wchlink_transfer_start_flash(&fixture.transfer, 0x04u));
    wchlink_transfer_write_data(&fixture.transfer, packet, sizeof(packet));
    assert(wchlink_test_take_status(&fixture.transfer) == 0x04u);
    assert(wchlink_test_target_load(&fixture.target, 0x20002010u, checksum,
                                    sizeof(checksum)));
    assert(memcmp(checksum, packet, sizeof(packet)) == 0);
}

static void wchlink_test_transfer_x035_checksum(void) {
    const struct rvswd_target_info info = wchlink_test_info(
        0x03510611u, WCHLINK_TARGET_FAMILY_X03X,
        WCHLINK_TEST_LOADER_DEFAULT, true);
    const uint8_t packet[] = {0x01u, 0x02u, 0x03u, 0x04u};
    struct wchlink_test_fixture fixture;
    struct wchlink_transfer_finish_result finish;
    struct wchlink_test_execute execute;
    uint8_t checksum[sizeof(packet)];

    // X035 的 flash_op643 在编程加校验前需要写入 checksum mailbox
    wchlink_test_fixture_init(&fixture, info, true);
    finish = wchlink_test_finish_default_loader(&fixture, 0x08000000u,
                                                sizeof(packet));
    assert(finish.status == WCHLINK_TRANSFER_FINISH_READY);
    assert(wchlink_test_target_operation_count(
               &fixture.target, WCHLINK_TEST_TARGET_EXECUTE) == 1u);
    assert(wchlink_transfer_start_flash(&fixture.transfer, 0x04u));
    wchlink_transfer_write_data(&fixture.transfer, packet, sizeof(packet));
    assert(wchlink_test_take_status(&fixture.transfer) == 0x04u);
    assert(wchlink_test_target_load(&fixture.target, 0x20002010u, checksum,
                                    sizeof(checksum)));
    assert(memcmp(checksum, packet, sizeof(packet)) == 0);
    assert(wchlink_test_target_last_execute(&fixture.target, &execute));
    assert(execute.mode == 0x1cu);
    assert(execute.address == 0x08000000u);
    assert(execute.length == sizeof(packet));
}

static void wchlink_test_transfer_partial_write(void) {
    const struct rvswd_target_info info = wchlink_test_info(
        0x82000000u, WCHLINK_TARGET_FAMILY_CH58X,
        WCHLINK_TEST_LOADER_LEGACY, false);
    struct wchlink_test_fixture fixture;
    uint8_t page[WCHLINK_TRANSFER_PACKET_CAPACITY];
    const uint8_t patch[] = {0x12u, 0x34u};
    const uint8_t short_patch[] = {0x56u};

    // 无下载缓存时从目标回读完整页，合并短写后交给 Flash rewrite port
    wchlink_test_fixture_init(&fixture, info, true);
    memset(page, 0xaau, sizeof(page));
    assert(wchlink_test_target_store(&fixture.target, 0x100u, page,
                                     sizeof(page)));
    assert(wchlink_transfer_start_partial_write(
        &fixture.transfer, 0x104u, (uint8_t)sizeof(patch)));
    wchlink_transfer_write_data(&fixture.transfer, patch, sizeof(patch));
    assert(wchlink_test_take_status(&fixture.transfer) ==
           WCHLINK_PARTIAL_WRITE_REPLY_OK);
    assert(wchlink_test_target_load(&fixture.target, 0x100u, page,
                                    sizeof(page)));
    assert(page[3] == 0xaau);
    assert(page[4] == patch[0]);
    assert(page[5] == patch[1]);
    assert(page[6] == 0xaau);

    assert(wchlink_transfer_start_partial_write(
        &fixture.transfer, 0x108u, (uint8_t)sizeof(patch)));
    wchlink_transfer_write_data(&fixture.transfer, short_patch,
                                sizeof(short_patch));
    assert(wchlink_test_take_status(&fixture.transfer) ==
           WCHLINK_PARTIAL_WRITE_REPLY_FAILED);
}

static void wchlink_test_transfer_bidirectional_activity(void) {
    const struct rvswd_target_info info = wchlink_test_info(
        0x82000000u, WCHLINK_TARGET_FAMILY_CH58X,
        WCHLINK_TEST_LOADER_LEGACY, false);
    struct wchlink_test_fixture fixture;
    uint8_t page[WCHLINK_TRANSFER_PACKET_CAPACITY];
    uint8_t read_data[4];
    const uint8_t patch[] = {0x12u, 0x34u};

    wchlink_test_fixture_init(&fixture, info, true);
    memset(page, 0xaau, sizeof(page));
    assert(wchlink_test_target_store(&fixture.target, 0x100u, page,
                                     sizeof(page)));

    // 独立 USB 方向允许读游标和 partial-write OUT 同时处于活动状态
    wchlink_transfer_prepare_read(&fixture.transfer, 0x100u, 4u);
    wchlink_transfer_begin_read(&fixture.transfer);
    assert(wchlink_transfer_start_partial_write(
        &fixture.transfer, 0x104u, (uint8_t)sizeof(patch)));
    assert(wchlink_transfer_next_io(&fixture.transfer) ==
           (WCHLINK_TRANSFER_IO_DATA_IN | WCHLINK_TRANSFER_IO_DATA_OUT));

    wchlink_transfer_write_data(&fixture.transfer, patch, sizeof(patch));
    assert(wchlink_test_take_status(&fixture.transfer) ==
           WCHLINK_PARTIAL_WRITE_REPLY_OK);
    assert(wchlink_transfer_next_io(&fixture.transfer) ==
           WCHLINK_TRANSFER_IO_DATA_IN);
    assert(wchlink_transfer_read_data(&fixture.transfer, read_data,
                                      sizeof(read_data)) == sizeof(read_data));
    assert(wchlink_transfer_next_io(&fixture.transfer) ==
           WCHLINK_TRANSFER_IO_NONE);
}

static void wchlink_test_transfer_error_repeat_and_abort(void) {
    const struct rvswd_target_info info = wchlink_test_info(
        0x03510611u, WCHLINK_TARGET_FAMILY_X03X,
        WCHLINK_TEST_LOADER_DEFAULT, true);
    struct wchlink_test_fixture fixture;
    struct wchlink_transfer_finish_result finish;
    struct rvswd_target_result failure;
    uint8_t first[] = {0x01u, 0x02u, 0x03u, 0x04u};
    uint8_t loader[WCHLINK_TRANSFER_PACKET_CAPACITY];
    uint8_t oversized_loader[WCHLINK_TRANSFER_PACKET_CAPACITY + 1u];
    uint8_t loaded[4];

    // 重复开始丢弃旧接收游标，abort 清空 I/O，target 诊断按值返回
    wchlink_test_fixture_init(&fixture, info, true);
    wchlink_transfer_prepare_write(&fixture.transfer, 0x08000000u, 4u);
    assert(wchlink_transfer_start_loader(&fixture.transfer));
    wchlink_transfer_write_data(&fixture.transfer, first, sizeof(first));
    assert(wchlink_transfer_start_loader(&fixture.transfer));
    memset(loader, 0x6bu, sizeof(loader));
    wchlink_transfer_write_data(&fixture.transfer, loader, sizeof(loader));
    memset(loader, 0x7cu, sizeof(loader));
    wchlink_transfer_write_data(&fixture.transfer, loader, sizeof(loader));
    finish = wchlink_transfer_finish_loader(&fixture.transfer, 0x07u);
    assert(finish.status == WCHLINK_TRANSFER_FINISH_READY);
    assert(wchlink_test_target_load(&fixture.target, 0x20000000u, loaded,
                                    sizeof(loaded)));
    assert(loaded[0] == 0x6bu);
    wchlink_transfer_abort(&fixture.transfer);
    assert(wchlink_transfer_next_io(&fixture.transfer) ==
           WCHLINK_TRANSFER_IO_NONE);

    wchlink_transfer_prepare_write(&fixture.transfer, 0x08000000u, 4u);
    assert(wchlink_transfer_start_loader(&fixture.transfer));
    failure = rvswd_target_result_failure(RVSWD_TARGET_RESULT_MEMORY, 0x07u,
                                          false);
    failure.address = 0x20000000u;
    failure.dmi_status = 0x03u;
    failure.abstractcs = 0x00000700u;
    wchlink_test_target_fail_next(
        &fixture.target, WCHLINK_TEST_TARGET_WRITE_MEMORY, failure);
    wchlink_transfer_write_data(&fixture.transfer, first, sizeof(first));
    finish = wchlink_transfer_finish_loader(&fixture.transfer, 0x07u);
    assert(finish.status == WCHLINK_TRANSFER_FINISH_LOADER_ERROR);
    assert(finish.loader_error == 0x07u);
    assert(finish.dmi_status == 0x03u);
    assert(finish.address == 0x20000000u);
    assert(finish.abstractcs == 0x00000700u);

    wchlink_transfer_prepare_write(&fixture.transfer, 0x08000000u, 4u);
    assert(wchlink_transfer_start_loader(&fixture.transfer));
    memset(oversized_loader, 0xa5, sizeof(oversized_loader));
    wchlink_transfer_write_data(&fixture.transfer, oversized_loader,
                                sizeof(oversized_loader));
    finish = wchlink_transfer_finish_loader(&fixture.transfer, 0x07u);
    assert(finish.status == WCHLINK_TRANSFER_FINISH_LOADER_ERROR);
    assert(finish.loader_error == 0xefu);
    assert(finish.address == 0x20000000u);
    assert(finish.abstractcs == 0xffffffffu);
}

static void wchlink_test_transfer_flash_address_bounds(void) {
    const struct rvswd_target_info info = wchlink_test_info(
        0x03510611u, WCHLINK_TARGET_FAMILY_X03X,
        WCHLINK_TEST_LOADER_DEFAULT, true);
    const uint8_t packet[] = {0x01u, 0x02u, 0x03u, 0x04u};
    const uint32_t valid_end = 0x08000000u + 0xf800u;
    const uint32_t addresses[] = {
        0x07ffffffu,
        valid_end - sizeof(packet),
        valid_end,
        0xfffffffcu,
    };
    struct wchlink_transfer_finish_result finish;
    struct wchlink_test_execute execute;
    struct wchlink_test_fixture fixture;

    // 低地址、尾端刚好对齐和高地址溢出都必须在 loader 执行前判定
    for (size_t index = 0u; index < sizeof(addresses) / sizeof(addresses[0]);
         ++index) {
        wchlink_test_fixture_init(&fixture, info, true);
        wchlink_transfer_prepare_write(&fixture.transfer, addresses[index],
                                       sizeof(packet));
        finish = wchlink_test_finish_default_loader(
            &fixture, addresses[index], sizeof(packet));
        assert(finish.status == WCHLINK_TRANSFER_FINISH_READY);
        assert(wchlink_transfer_start_flash(&fixture.transfer, 0x02u));
        wchlink_transfer_write_data(&fixture.transfer, packet, sizeof(packet));
        assert(wchlink_test_take_status(&fixture.transfer) ==
               (index == 1u ? 0x04u : 0x03u));
        assert(wchlink_test_target_last_execute(&fixture.target, &execute));
        assert(execute.mode == (index == 1u ? 0x0cu : 0x01u));
        assert(execute.address == (index == 1u ? addresses[index] : 0u));
    }
}

static void wchlink_test_transfer_abort_pending_data_in(void) {
    const struct rvswd_target_info info = wchlink_test_info(
        0x03510611u, WCHLINK_TARGET_FAMILY_X03X,
        WCHLINK_TEST_LOADER_DEFAULT, true);
    struct wchlink_test_fixture fixture;
    uint8_t patch[] = {0x12u, 0x34u, 0x56u, 0x78u};
    uint8_t status;

    wchlink_test_fixture_init(&fixture, info, true);

    // 放弃回读后不得继续生成旧 data IN 包
    wchlink_transfer_prepare_read(&fixture.transfer, 0x100u, 64u);
    wchlink_transfer_begin_read(&fixture.transfer);
    assert(wchlink_transfer_next_io(&fixture.transfer) ==
           WCHLINK_TRANSFER_IO_DATA_IN);
    wchlink_transfer_abort(&fixture.transfer);
    assert(wchlink_transfer_next_io(&fixture.transfer) ==
           WCHLINK_TRANSFER_IO_NONE);

    // 已生成但未被主机读取的 ACK 也必须随旧操作清除
    assert(wchlink_transfer_start_partial_write(
        &fixture.transfer, 0x100u, (uint8_t)sizeof(patch)));
    wchlink_transfer_write_data(&fixture.transfer, patch, sizeof(patch));
    assert(wchlink_transfer_next_io(&fixture.transfer) ==
           WCHLINK_TRANSFER_IO_DATA_IN);
    wchlink_transfer_abort(&fixture.transfer);
    assert(wchlink_transfer_next_io(&fixture.transfer) ==
           WCHLINK_TRANSFER_IO_NONE);
    assert(!wchlink_transfer_take_reply_status(&fixture.transfer, &status));
}

int main(void) {
    wchlink_test_command_connect_and_dmi();
    wchlink_test_command_direct_dmi_resume_completion();
    wchlink_test_command_connect_failure();
    wchlink_test_command_config_and_reset();
    wchlink_test_command_control_and_device_mode();
    wchlink_test_command_memory_type();
    wchlink_test_command_ch58x_59x_info_stop();
    wchlink_test_command_repeat_and_abort();
    wchlink_test_command_program_end_resets_and_halts();
    wchlink_test_transfer_read();
    wchlink_test_command_official_memory_reads();
    wchlink_test_transfer_chunk_boundary();
    wchlink_test_transfer_ch58x_59x_padding();
    wchlink_test_transfer_l103_checksum();
    wchlink_test_transfer_v30x_checksum();
    wchlink_test_transfer_x035_checksum();
    wchlink_test_transfer_partial_write();
    wchlink_test_transfer_bidirectional_activity();
    wchlink_test_transfer_error_repeat_and_abort();
    wchlink_test_transfer_flash_address_bounds();
    wchlink_test_transfer_abort_pending_data_in();
    return 0;
}
