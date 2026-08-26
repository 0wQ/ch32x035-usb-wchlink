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
    uint32_t chip_id, uint8_t family, uint8_t loader,
    bool memory_streaming) {
    return (struct rvswd_target_info){
        .chip_id = chip_id,
        .family = family,
        .loader = loader,
        .connected = true,
        .memory_streaming = memory_streaming,
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
        0x10320710u, WCHLINK_TARGET_FAMILY_L103,
        RVSWD_TARGET_LOADER_L103, true);
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

static void wchlink_test_command_connect_failure(void) {
    const struct rvswd_target_info info = wchlink_test_info(
        0x03510611u, WCHLINK_TARGET_FAMILY_X035,
        RVSWD_TARGET_LOADER_DEFAULT, true);
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
        0x03510611u, WCHLINK_TARGET_FAMILY_X035,
        RVSWD_TARGET_LOADER_DEFAULT, true);
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
    const uint8_t extended_protection[] = {
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

    // Protection command 通过 target port 返回状态，扩展帧不能退化为基础命令
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
        &fixture.command, extended_protection, sizeof(extended_protection),
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
        0x03510611u, WCHLINK_TARGET_FAMILY_X035,
        RVSWD_TARGET_LOADER_DEFAULT, true);
    struct wchlink_test_fixture fixture;
    uint8_t response[16];
    uint8_t programmed[] = {0x00u, 0x11u, 0x22u, 0x33u};
    uint8_t erased[sizeof(programmed)];
    const uint8_t speed_request[] = {
        0x81u, WCHLINK_FAMILY_SPEED, 0x02u, WCHLINK_TARGET_FAMILY_X035, 0x01u};
    const uint8_t set_chip_type[] = {
        0x81u, WCHLINK_FAMILY_CONTROL, 0x01u,
        WCHLINK_CONTROL_SET_CHIP_TYPE};
    const uint8_t erase_request[] = {
        0x81u, WCHLINK_FAMILY_CONTROL, 0x02u,
        WCHLINK_CONTROL_CLEAR_CODE_FLASH, WCHLINK_TARGET_FAMILY_X035};
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
        0x82u, WCHLINK_FAMILY_CONTROL, 0x05u, WCHLINK_TARGET_FAMILY_X035,
        0x03u, 0x51u, 0x06u, 0x11u};
    const uint8_t erase_reply[] = {
        0x82u, WCHLINK_FAMILY_CONTROL, 0x01u,
        WCHLINK_CONTROL_CLEAR_CODE_FLASH};
    const uint8_t control_ack[] = {
        0x82u, WCHLINK_FAMILY_CONTROL, 0x01u, 0x00u};
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
        &fixture.target, WCHLINK_TARGET_FAMILY_X035));
    wchlink_test_expect_bytes(response, result.response_length, speed_reply,
                              sizeof(speed_reply));
    result = wchlink_command_process(&fixture.command, set_chip_type,
                                     sizeof(set_chip_type), response,
                                     sizeof(response));
    assert(wchlink_test_last_delay_ms == 20u);
    assert(wchlink_target_ports_info(&fixture.target).connected);
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
    assert(wchlink_test_dp_pullup_enabled);
    wchlink_test_expect_bytes(response, result.response_length, control_ack,
                              sizeof(control_ack));
    wchlink_command_process(&fixture.command, power_3v3_off,
                            sizeof(power_3v3_off), response, sizeof(response));
    assert(!wchlink_test_dp_pullup_enabled);
    wchlink_command_process(&fixture.command, power_5v_on, sizeof(power_5v_on),
                            response, sizeof(response));
    assert(wchlink_test_power_switch_enabled);
    wchlink_command_process(&fixture.command, power_5v_off,
                            sizeof(power_5v_off), response, sizeof(response));
    assert(!wchlink_test_power_switch_enabled);

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

static void wchlink_test_command_ch5xx_info_stop(void) {
    const struct rvswd_target_info info = wchlink_test_info(
        0x82000000u, WCHLINK_TARGET_FAMILY_CH58X,
        RVSWD_TARGET_LOADER_CH5XX, false);
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

    // CH5xx 的 INFO 查询使随后 STOP 返回目标信息，同时结束 response 生命周期
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
        0x03510611u, WCHLINK_TARGET_FAMILY_X035,
        RVSWD_TARGET_LOADER_DEFAULT, true);
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

static void wchlink_test_transfer_read(void) {
    const struct rvswd_target_info info = wchlink_test_info(
        0x03510611u, WCHLINK_TARGET_FAMILY_X035,
        RVSWD_TARGET_LOADER_DEFAULT, true);
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

static struct wchlink_transfer_finish_result
wchlink_test_finish_default_loader(struct wchlink_test_fixture *fixture,
                                   uint32_t address, uint32_t length) {
    uint8_t loader[WCHLINK_FLASH_PACKET_SIZE];

    memset(loader, 0x5au, sizeof(loader));
    wchlink_transfer_prepare_write(&fixture->transfer, address, length);
    assert(wchlink_transfer_start_loader(&fixture->transfer));
    wchlink_transfer_write_data(&fixture->transfer, loader, sizeof(loader));
    wchlink_transfer_write_data(&fixture->transfer, loader, sizeof(loader));
    return wchlink_transfer_finish_loader(&fixture->transfer, 0x07u);
}

static void wchlink_test_transfer_chunk_boundary(void) {
    const struct rvswd_target_info info = wchlink_test_info(
        0x03510611u, WCHLINK_TARGET_FAMILY_X035,
        RVSWD_TARGET_LOADER_DEFAULT, true);
    struct wchlink_test_fixture fixture;
    struct wchlink_transfer_finish_result finish;
    struct wchlink_test_execute execute;
    uint8_t packet[WCHLINK_FLASH_PACKET_SIZE];
    const uint8_t tail[] = {0x11u, 0x22u, 0x33u, 0x44u};

    // 4096 字节完成后继续保留 OUT，尾块完成后只留下最终 IN reply
    wchlink_test_fixture_init(&fixture, info, true);
    finish = wchlink_test_finish_default_loader(
        &fixture, 0x08000000u, WCHLINK_FLASH_CHUNK_SIZE + sizeof(tail));
    assert(finish.status == WCHLINK_TRANSFER_FINISH_READY);
    assert(wchlink_transfer_start_flash(&fixture.transfer, 0x02u));

    memset(packet, 0x3cu, sizeof(packet));
    for (size_t offset = 0u; offset < WCHLINK_FLASH_CHUNK_SIZE;
         offset += sizeof(packet)) {
        wchlink_transfer_write_data(&fixture.transfer, packet,
                                    sizeof(packet));
    }
    assert(wchlink_test_take_status(&fixture.transfer) == 0x04u);
    assert(wchlink_test_target_last_execute(&fixture.target, &execute));
    assert(execute.mode == 0x08u);
    assert(execute.address == 0x08000000u);
    assert(execute.length == WCHLINK_FLASH_CHUNK_SIZE);
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

static void wchlink_test_transfer_ch5xx_padding(void) {
    const struct rvswd_target_info info = wchlink_test_info(
        0x82000000u, WCHLINK_TARGET_FAMILY_CH58X,
        RVSWD_TARGET_LOADER_CH5XX, false);
    struct wchlink_test_fixture fixture;
    struct wchlink_transfer_finish_result finish;
    struct wchlink_test_execute execute;
    uint8_t packet[WCHLINK_FLASH_PACKET_SIZE];
    uint8_t target_page[WCHLINK_FLASH_PACKET_SIZE];
    uint8_t checksum[4];
    const uint8_t loader[] = {0x13u, 0x37u, 0x42u, 0x24u};

    // CH5xx OpenOCD 路径接收完整 4 KiB 窗口，loader 只消费实际数据和页尾 0xff
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
    for (size_t offset = 0u; offset < WCHLINK_FLASH_CHUNK_SIZE;
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
    assert(wchlink_test_target_load(&fixture.target,
                                    WCHLINK_CH5XX_LOADER_CHECKSUM_ADDRESS,
                                    checksum, sizeof(checksum)));
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

static void wchlink_test_transfer_partial_write(void) {
    const struct rvswd_target_info info = wchlink_test_info(
        0x82000000u, WCHLINK_TARGET_FAMILY_CH58X,
        RVSWD_TARGET_LOADER_CH5XX, false);
    struct wchlink_test_fixture fixture;
    uint8_t page[WCHLINK_FLASH_PACKET_SIZE];
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
        RVSWD_TARGET_LOADER_CH5XX, false);
    struct wchlink_test_fixture fixture;
    uint8_t page[WCHLINK_FLASH_PACKET_SIZE];
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
        0x03510611u, WCHLINK_TARGET_FAMILY_X035,
        RVSWD_TARGET_LOADER_DEFAULT, true);
    struct wchlink_test_fixture fixture;
    struct wchlink_transfer_finish_result finish;
    struct rvswd_target_result failure;
    uint8_t first[] = {0x01u, 0x02u, 0x03u, 0x04u};
    uint8_t loader[WCHLINK_FLASH_PACKET_SIZE];
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
}

int main(void) {
    wchlink_test_command_connect_and_dmi();
    wchlink_test_command_connect_failure();
    wchlink_test_command_config_and_reset();
    wchlink_test_command_control_and_device_mode();
    wchlink_test_command_ch5xx_info_stop();
    wchlink_test_command_repeat_and_abort();
    wchlink_test_transfer_read();
    wchlink_test_transfer_chunk_boundary();
    wchlink_test_transfer_ch5xx_padding();
    wchlink_test_transfer_partial_write();
    wchlink_test_transfer_bidirectional_activity();
    wchlink_test_transfer_error_repeat_and_abort();
    return 0;
}
