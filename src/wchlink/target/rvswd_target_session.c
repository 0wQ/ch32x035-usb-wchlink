#include "rvswd_target_session.h"

#include "rvswd_debug.h"
#include "rvswd_dmi.h"
#include "rvswd_flash.h"
#include "rvswd_memory.h"
#include "rvswd_phy_gpio.h"
#include "rvswd_reset.h"
#include "rvswd_target_profile.h"
#include "rvswd_types.h"

#include <stddef.h>
#include <string.h>

extern const uint8_t ch5xx_flash_erase_stub_start[];
extern const uint8_t ch5xx_flash_erase_stub_end[];

static const struct rvswd_target_profile *rvswd_target_session_current_profile(
    const struct rvswd_target_session *session) {
    return rvswd_target_profile_resolve(session->info.chip_id,
                                        session->family_hint,
                                        session->family_hint_active);
}

static const struct rvswd_target_profile *
rvswd_target_session_memory_profile(
    const struct rvswd_target_session *session) {
    const struct rvswd_target_profile *profile =
        rvswd_target_session_current_profile(session);

    if (profile != NULL) {
        return profile;
    }
    return rvswd_target_profile_from_family(session->family_hint);
}

static void rvswd_target_session_legacy_init(void) {
    rvswd_dmi_reset();
    rvswd_phy_gpio_init();
}

static void rvswd_target_session_legacy_disconnect(void) {
    rvswd_phy_gpio_disconnect();
}

static bool rvswd_target_session_legacy_write_dmi(uint8_t address,
                                                  uint32_t value) {
    return rvswd_dmi_write(address, value);
}

static bool rvswd_target_session_legacy_read_memory32(
    struct rvswd_target_session *session, uint32_t address, uint32_t *value) {
    return rvswd_memory_read32(rvswd_target_session_memory_profile(session),
                               session->info.chip_id != 0u, address, value);
}

static bool rvswd_target_session_legacy_write_memory32(uint32_t address, uint32_t value) {
    return rvswd_memory_write32(address, value);
}

static bool rvswd_target_session_legacy_write_memory(
    struct rvswd_target_session *session, uint32_t address,
    const uint8_t *data, uint32_t length) {
    return rvswd_memory_write(rvswd_target_session_current_profile(session),
                              address, data, length);
}

static bool rvswd_target_session_legacy_reset_and_halt(void) {
    return rvswd_reset_and_halt();
}

static bool rvswd_target_session_legacy_soft_reset_and_run(void) {
    return rvswd_soft_reset_and_run();
}

static bool rvswd_target_session_legacy_reset_and_run(void) {
    return rvswd_reset_and_run();
}

static bool rvswd_target_session_legacy_write_register(uint16_t regno, uint32_t value) {
    return rvswd_debug_write_register(regno, value);
}

static bool rvswd_target_session_legacy_read_register(uint16_t regno, uint32_t *value) {
    return rvswd_debug_read_register(regno, value);
}

static bool rvswd_target_session_legacy_halt(void) {
    return rvswd_debug_halt();
}

static bool rvswd_target_session_legacy_execute(uint32_t entry, uint32_t stack_top, uint32_t mode,
                                                uint32_t address, uint32_t length, uint32_t data_address,
                                                uint32_t *result) {
    return rvswd_debug_execute(entry, stack_top, mode, address, length,
                               data_address, result);
}

static bool rvswd_target_session_legacy_read_dmi(uint8_t address,
                                                 uint32_t *value) {
    return rvswd_dmi_read(address, value);
}

static bool rvswd_target_session_legacy_dmi_failure_retryable(void) {
    return rvswd_dmi_failure_retryable();
}

static bool rvswd_target_session_target_supports_memory_streaming(
    const struct rvswd_target_session *session) {
    const struct rvswd_target_profile *profile =
        rvswd_target_session_current_profile(session);

    return profile != NULL &&
           profile->memory_write_mode == RVSWD_MEMORY_WRITE_STREAMING;
}

static struct rvswd_target_result rvswd_target_session_invalid_result(void) {
    return rvswd_target_result_failure(RVSWD_TARGET_RESULT_CONNECT, 0x01u,
                                       false);
}

static struct rvswd_target_result rvswd_target_session_memory_result(
    struct rvswd_target_session *session, uint32_t address, bool success) {
    struct rvswd_target_result result;

    if (success) {
        result = rvswd_target_result_success();
    } else {
        result = rvswd_target_result_failure(
            RVSWD_TARGET_RESULT_MEMORY,
            rvswd_memory_last_error(),
            rvswd_dmi_failure_retryable());
        result.address = rvswd_memory_failure_address();
        result.dmi_status = rvswd_memory_failure_dmi_status();
        result.abstractcs = rvswd_memory_failure_abstractcs();
    }
    (void)session;
    if (!success && result.code == 0u) {
        result.code = 0x15u;
    }
    if (!success && result.address == 0u) {
        result.address = address;
    }
    return result;
}

void rvswd_target_session_init(struct rvswd_target_session *session) {
    uint8_t family_hint;

    if (session == NULL) {
        return;
    }
    // MRS 在重建 transport 前发送 family hint，初始化只能清除探测结果
    family_hint = session->family_hint;
    memset(session, 0, sizeof(*session));
    session->family_hint = family_hint;
    rvswd_target_session_legacy_init();
}

void rvswd_target_session_disconnect(struct rvswd_target_session *session) {
    if (session == NULL) {
        return;
    }
    rvswd_target_session_legacy_disconnect();
    session->info.connected = false;
}

void rvswd_target_session_set_family_hint(
    struct rvswd_target_session *session, uint8_t family) {
    if (session == NULL) {
        return;
    }
    session->family_hint = family;
    session->family_hint_active = false;
    rvswd_dmi_set_packet_mode(family == WCHLINK_TARGET_FAMILY_CH58X
                                  ? RVSWD_PACKET_LONG
                                  : RVSWD_PACKET_SHORT);
    if (!session->info.connected) {
        session->info.family = family;
        session->info.profile = NULL;
    }
}

bool rvswd_target_session_is_connected(
    const struct rvswd_target_session *session) {
    return session != NULL && session->info.connected;
}

const struct rvswd_target_info *rvswd_target_session_info(
    const struct rvswd_target_session *session) {
    return session == NULL ? NULL : &session->info;
}

bool rvswd_target_session_supports_memory_streaming(
    const struct rvswd_target_session *session) {
    return session != NULL &&
           rvswd_target_session_target_supports_memory_streaming(session);
}

struct rvswd_target_result rvswd_target_session_read_dmi(
    struct rvswd_target_session *session, uint8_t address) {
    uint32_t value = 0u;
    struct rvswd_target_result result;

    if (session == NULL) {
        return rvswd_target_session_invalid_result();
    }
    if (rvswd_target_session_legacy_read_dmi(address, &value)) {
        result = rvswd_target_result_success();
        result.value = value;
        return result;
    }
    return rvswd_target_result_failure(RVSWD_TARGET_RESULT_DMI,
                                       rvswd_dmi_last_status(),
                                       rvswd_target_session_legacy_dmi_failure_retryable());
}

struct rvswd_target_result rvswd_target_session_write_dmi(
    struct rvswd_target_session *session, uint8_t address, uint32_t value) {
    if (session == NULL) {
        return rvswd_target_session_invalid_result();
    }
    if (rvswd_target_session_legacy_write_dmi(address, value)) {
        return rvswd_target_result_success();
    }
    return rvswd_target_result_failure(RVSWD_TARGET_RESULT_DMI,
                                       rvswd_dmi_last_status(),
                                       rvswd_target_session_legacy_dmi_failure_retryable());
}

struct rvswd_target_result rvswd_target_session_read_memory32(
    struct rvswd_target_session *session, uint32_t address) {
    uint32_t value = 0u;
    struct rvswd_target_result result;

    if (session == NULL) {
        return rvswd_target_session_invalid_result();
    }
    result = rvswd_target_session_memory_result(
        session, address,
        rvswd_target_session_legacy_read_memory32(session, address, &value));
    if (result.ok) {
        result.value = value;
    }
    return result;
}

struct rvswd_target_result rvswd_target_session_write_memory32(
    struct rvswd_target_session *session, uint32_t address, uint32_t value) {
    if (session == NULL) {
        return rvswd_target_session_invalid_result();
    }
    return rvswd_target_session_memory_result(
        session, address,
        rvswd_target_session_legacy_write_memory32(address, value));
}

struct rvswd_target_result rvswd_target_session_write_memory(
    struct rvswd_target_session *session, uint32_t address,
    const uint8_t *data, uint32_t length) {
    if (session == NULL || data == NULL || length == 0u) {
        return rvswd_target_session_invalid_result();
    }
    return rvswd_target_session_memory_result(
        session, address,
        rvswd_target_session_legacy_write_memory(session, address, data,
                                                 length));
}

struct rvswd_target_result rvswd_target_session_write_register(
    struct rvswd_target_session *session, uint16_t regno, uint32_t value) {
    if (session == NULL) {
        return rvswd_target_session_invalid_result();
    }
    return rvswd_target_session_legacy_write_register(regno, value)
               ? rvswd_target_result_success()
               : rvswd_target_result_failure(RVSWD_TARGET_RESULT_DEBUG,
                                             rvswd_dmi_last_status(),
                                             rvswd_dmi_failure_retryable());
}

struct rvswd_target_result rvswd_target_session_read_register(
    struct rvswd_target_session *session, uint16_t regno) {
    uint32_t value = 0u;
    struct rvswd_target_result result;

    if (session == NULL) {
        return rvswd_target_session_invalid_result();
    }
    if (!rvswd_target_session_legacy_read_register(regno, &value)) {
        return rvswd_target_result_failure(RVSWD_TARGET_RESULT_DEBUG,
                                           rvswd_dmi_last_status(),
                                           rvswd_dmi_failure_retryable());
    }
    result = rvswd_target_result_success();
    result.value = value;
    return result;
}

struct rvswd_target_result rvswd_target_session_halt(
    struct rvswd_target_session *session) {
    if (session == NULL) {
        return rvswd_target_session_invalid_result();
    }
    return rvswd_target_session_legacy_halt()
               ? rvswd_target_result_success()
               : rvswd_target_result_failure(RVSWD_TARGET_RESULT_DEBUG,
                                             rvswd_dmi_last_status(),
                                             rvswd_dmi_failure_retryable());
}

struct rvswd_target_result rvswd_target_session_execute(
    struct rvswd_target_session *session, uint32_t entry, uint32_t stack_top,
    uint32_t mode, uint32_t address, uint32_t length, uint32_t data_address) {
    uint32_t value = 0xffffffffu;
    struct rvswd_target_result result;
    bool success;

    if (session == NULL) {
        return rvswd_target_session_invalid_result();
    }
    success = rvswd_target_session_legacy_execute(entry, stack_top, mode,
                                                  address, length, data_address,
                                                  &value);
    result = success
                 ? rvswd_target_result_success()
                 : rvswd_target_result_failure(RVSWD_TARGET_RESULT_DEBUG,
                                               value == 0xffffffffu ? 0x15u : value,
                                               rvswd_dmi_failure_retryable());
    result.value = value;
    result.address = address;
    return result;
}

struct rvswd_target_result rvswd_target_session_reset_and_halt(
    struct rvswd_target_session *session) {
    if (session == NULL) {
        return rvswd_target_session_invalid_result();
    }
    return rvswd_target_session_legacy_reset_and_halt()
               ? rvswd_target_result_success()
               : rvswd_target_result_failure(RVSWD_TARGET_RESULT_RESET,
                                             rvswd_dmi_last_status(),
                                             rvswd_dmi_failure_retryable());
}

struct rvswd_target_result rvswd_target_session_soft_reset_and_run(
    struct rvswd_target_session *session) {
    if (session == NULL) {
        return rvswd_target_session_invalid_result();
    }
    return rvswd_target_session_legacy_soft_reset_and_run()
               ? rvswd_target_result_success()
               : rvswd_target_result_failure(RVSWD_TARGET_RESULT_RESET,
                                             rvswd_dmi_last_status(),
                                             rvswd_dmi_failure_retryable());
}

struct rvswd_target_result rvswd_target_session_reset_and_run(
    struct rvswd_target_session *session) {
    if (session == NULL) {
        return rvswd_target_session_invalid_result();
    }
    return rvswd_target_session_legacy_reset_and_run()
               ? rvswd_target_result_success()
               : rvswd_target_result_failure(RVSWD_TARGET_RESULT_RESET,
                                             rvswd_dmi_last_status(),
                                             rvswd_dmi_failure_retryable());
}

struct rvswd_target_result rvswd_target_session_flash_erase_all(
    struct rvswd_target_session *session) {
    if (session == NULL) {
        return rvswd_target_session_invalid_result();
    }
    return rvswd_flash_erase_all(session->info.profile)
               ? rvswd_target_result_success()
               : rvswd_target_result_failure(RVSWD_TARGET_RESULT_FLASH,
                                             rvswd_flash_last_error(),
                                             rvswd_dmi_failure_retryable());
}

struct rvswd_target_result rvswd_target_session_flash_rewrite_page(
    struct rvswd_target_session *session, uint32_t address,
    const uint8_t *data) {
    if (session == NULL || data == NULL) {
        return rvswd_target_session_invalid_result();
    }
    return rvswd_flash_rewrite_page(session->info.profile, address, data)
               ? rvswd_target_result_success()
               : rvswd_target_result_failure(RVSWD_TARGET_RESULT_FLASH,
                                             rvswd_flash_last_error(),
                                             rvswd_dmi_failure_retryable());
}

struct rvswd_target_result rvswd_target_session_flash_read_protected(
    struct rvswd_target_session *session) {
    bool value = false;
    struct rvswd_target_result result;

    if (session == NULL) {
        return rvswd_target_session_invalid_result();
    }
    if (!rvswd_flash_read_protected(session->info.profile, &value)) {
        return rvswd_target_result_failure(RVSWD_TARGET_RESULT_FLASH,
                                           rvswd_flash_last_error(),
                                           rvswd_dmi_failure_retryable());
    }
    result = rvswd_target_result_success();
    result.value = value ? 1u : 0u;
    return result;
}

struct rvswd_target_result rvswd_target_session_flash_write_protected(
    struct rvswd_target_session *session) {
    bool value = false;
    struct rvswd_target_result result;

    if (session == NULL) {
        return rvswd_target_session_invalid_result();
    }
    if (!rvswd_flash_write_protected(session->info.profile, &value)) {
        return rvswd_target_result_failure(RVSWD_TARGET_RESULT_FLASH,
                                           rvswd_flash_last_error(),
                                           rvswd_dmi_failure_retryable());
    }
    result = rvswd_target_result_success();
    result.value = value ? 1u : 0u;
    return result;
}

struct rvswd_target_result rvswd_target_session_flash_set_read_protected(
    struct rvswd_target_session *session, bool protected) {
    if (session == NULL) {
        return rvswd_target_session_invalid_result();
    }
    return rvswd_flash_set_read_protected(session->info.profile, protected)
               ? rvswd_target_result_success()
               : rvswd_target_result_failure(RVSWD_TARGET_RESULT_FLASH,
                                             rvswd_flash_last_error(),
                                             rvswd_dmi_failure_retryable());
}
