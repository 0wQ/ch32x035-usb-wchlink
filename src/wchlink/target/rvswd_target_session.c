#include "rvswd_target_session.h"

#include "rvswd_debug.h"
#include "rvswd_flash.h"
#include "rvswd_memory.h"
#include "rvswd_operation.h"
#include "rvswd_reset.h"
#include "rvswd_target_profile.h"
#include "rvswd_transport.h"
#include "rvswd_types.h"

#include <stddef.h>
#include <string.h>

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

static struct rvswd_target_result rvswd_target_session_invalid_result(void) {
    return rvswd_target_result_failure(RVSWD_TARGET_RESULT_CONNECT, 0x01u,
                                       false);
}

static struct rvswd_target_result rvswd_target_session_dmi_result(
    struct rvswd_transport_result transport_result, uint32_t value) {
    struct rvswd_target_result result;

    if (transport_result.ok) {
        result = rvswd_target_result_success();
        result.value = value;
        return result;
    }
    result = rvswd_target_result_failure(RVSWD_TARGET_RESULT_DMI,
                                         transport_result.status,
                                         transport_result.retryable);
    result.dmi_status = transport_result.status;
    return result;
}

static struct rvswd_target_result rvswd_target_session_operation_result(
    const struct rvswd_operation *operation,
    enum rvswd_target_result_domain domain, uint32_t code, bool success) {
    struct rvswd_target_result result;

    if (success) {
        return rvswd_target_result_success();
    }
    result = rvswd_target_result_failure(domain, code, operation->retryable);
    result.address = operation->address;
    result.dmi_status = operation->dmi_status;
    result.abstractcs = operation->abstractcs;
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
    rvswd_transport_init(&session->transport);
}

void rvswd_target_session_disconnect(struct rvswd_target_session *session) {
    if (session == NULL) {
        return;
    }
    rvswd_transport_disconnect(&session->transport);
    session->info.connected = false;
}

void rvswd_target_session_set_family_hint(
    struct rvswd_target_session *session, uint8_t family) {
    if (session == NULL) {
        return;
    }
    session->family_hint = family;
    session->family_hint_active = false;
    rvswd_transport_set_packet_mode(
        &session->transport, family == WCHLINK_TARGET_FAMILY_CH58X
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
    const struct rvswd_target_profile *profile;

    if (session == NULL) {
        return false;
    }
    profile = rvswd_target_session_current_profile(session);
    return profile != NULL &&
           profile->memory_write_mode == RVSWD_MEMORY_WRITE_STREAMING;
}

struct rvswd_target_result rvswd_target_session_read_dmi(
    struct rvswd_target_session *session, uint8_t address) {
    uint32_t value = 0u;
    struct rvswd_transport_result transport_result;

    if (session == NULL) {
        return rvswd_target_session_invalid_result();
    }
    transport_result = rvswd_transport_read(&session->transport, address);
    value = transport_result.value;
    return rvswd_target_session_dmi_result(transport_result, value);
}

struct rvswd_target_result rvswd_target_session_write_dmi(
    struct rvswd_target_session *session, uint8_t address, uint32_t value) {
    struct rvswd_transport_result transport_result;

    if (session == NULL) {
        return rvswd_target_session_invalid_result();
    }
    transport_result =
        rvswd_transport_write(&session->transport, address, value);
    return rvswd_target_session_dmi_result(transport_result, 0u);
}

struct rvswd_target_result rvswd_target_session_read_memory32(
    struct rvswd_target_session *session, uint32_t address) {
    uint32_t value = 0u;
    struct rvswd_target_result result;
    struct rvswd_operation operation;
    bool success;

    if (session == NULL) {
        return rvswd_target_session_invalid_result();
    }
    rvswd_operation_init(&operation, &session->transport);
    operation.address = address;
    success = rvswd_memory_read32(
        &operation, rvswd_target_session_memory_profile(session),
        session->info.chip_id != 0u, address, &value);
    result = rvswd_target_session_operation_result(
        &operation, RVSWD_TARGET_RESULT_MEMORY,
        operation.memory_code == 0u ? 0x15u : operation.memory_code, success);
    if (result.ok) {
        result.value = value;
    }
    return result;
}

struct rvswd_target_result rvswd_target_session_write_memory32(
    struct rvswd_target_session *session, uint32_t address, uint32_t value) {
    struct rvswd_operation operation;
    bool success;

    if (session == NULL) {
        return rvswd_target_session_invalid_result();
    }
    rvswd_operation_init(&operation, &session->transport);
    operation.address = address;
    success = rvswd_memory_write32(&operation, address, value);
    return rvswd_target_session_operation_result(
        &operation, RVSWD_TARGET_RESULT_MEMORY,
        operation.memory_code == 0u ? 0x15u : operation.memory_code, success);
}

struct rvswd_target_result rvswd_target_session_write_memory(
    struct rvswd_target_session *session, uint32_t address,
    const uint8_t *data, uint32_t length) {
    struct rvswd_operation operation;
    bool success;

    if (session == NULL || data == NULL || length == 0u) {
        return rvswd_target_session_invalid_result();
    }
    rvswd_operation_init(&operation, &session->transport);
    success = rvswd_memory_write(
        &operation, rvswd_target_session_current_profile(session), address, data,
        length);
    return rvswd_target_session_operation_result(
        &operation, RVSWD_TARGET_RESULT_MEMORY,
        operation.memory_code == 0u ? 0x15u : operation.memory_code, success);
}

struct rvswd_target_result rvswd_target_session_execute(
    struct rvswd_target_session *session, uint32_t entry, uint32_t stack_top,
    uint32_t mode, uint32_t address, uint32_t length, uint32_t data_address) {
    uint32_t value = 0xffffffffu;
    struct rvswd_target_result result;
    struct rvswd_operation operation;
    bool success;

    if (session == NULL) {
        return rvswd_target_session_invalid_result();
    }
    rvswd_operation_init(&operation, &session->transport);
    operation.address = address;
    success = rvswd_debug_execute(&operation, entry, stack_top, mode, address,
                                  length, data_address, &value);
    result = rvswd_target_session_operation_result(
        &operation, RVSWD_TARGET_RESULT_DEBUG,
        value == 0xffffffffu ? 0x15u : value, success);
    result.value = value;
    result.address = address;
    return result;
}

struct rvswd_target_result rvswd_target_session_reset_and_halt(
    struct rvswd_target_session *session) {
    struct rvswd_operation operation;
    bool success;

    if (session == NULL) {
        return rvswd_target_session_invalid_result();
    }
    rvswd_operation_init(&operation, &session->transport);
    success = rvswd_reset_and_halt(&operation);
    return rvswd_target_session_operation_result(
        &operation, RVSWD_TARGET_RESULT_RESET, operation.dmi_status, success);
}

struct rvswd_target_result rvswd_target_session_soft_reset_and_run(
    struct rvswd_target_session *session) {
    struct rvswd_operation operation;
    bool success;

    if (session == NULL) {
        return rvswd_target_session_invalid_result();
    }
    rvswd_operation_init(&operation, &session->transport);
    success = rvswd_soft_reset_and_run(&operation);
    return rvswd_target_session_operation_result(
        &operation, RVSWD_TARGET_RESULT_RESET, operation.dmi_status, success);
}

struct rvswd_target_result rvswd_target_session_reset_and_run(
    struct rvswd_target_session *session) {
    struct rvswd_operation operation;
    bool success;

    if (session == NULL) {
        return rvswd_target_session_invalid_result();
    }
    rvswd_operation_init(&operation, &session->transport);
    success = rvswd_reset_and_run(&operation);
    return rvswd_target_session_operation_result(
        &operation, RVSWD_TARGET_RESULT_RESET, operation.dmi_status, success);
}

struct rvswd_target_result rvswd_target_session_flash_erase_all(
    struct rvswd_target_session *session) {
    struct rvswd_operation operation;
    bool success;

    if (session == NULL) {
        return rvswd_target_session_invalid_result();
    }
    rvswd_operation_init(&operation, &session->transport);
    success = rvswd_flash_erase_all(&operation, session->info.profile);
    return rvswd_target_session_operation_result(
        &operation, RVSWD_TARGET_RESULT_FLASH, operation.flash_code, success);
}

struct rvswd_target_result rvswd_target_session_flash_rewrite_page(
    struct rvswd_target_session *session, uint32_t address,
    const uint8_t *data) {
    struct rvswd_operation operation;
    bool success;

    if (session == NULL || data == NULL) {
        return rvswd_target_session_invalid_result();
    }
    rvswd_operation_init(&operation, &session->transport);
    operation.address = address;
    success = rvswd_flash_rewrite_page(&operation, session->info.profile,
                                       address, data);
    return rvswd_target_session_operation_result(
        &operation, RVSWD_TARGET_RESULT_FLASH, operation.flash_code, success);
}

struct rvswd_target_result rvswd_target_session_flash_read_protected(
    struct rvswd_target_session *session) {
    bool value = false;
    struct rvswd_target_result result;
    struct rvswd_operation operation;

    if (session == NULL) {
        return rvswd_target_session_invalid_result();
    }
    rvswd_operation_init(&operation, &session->transport);
    if (!rvswd_flash_read_protected(&operation, session->info.profile, &value)) {
        return rvswd_target_session_operation_result(
            &operation, RVSWD_TARGET_RESULT_FLASH, operation.flash_code, false);
    }
    result = rvswd_target_result_success();
    result.value = value ? 1u : 0u;
    return result;
}

struct rvswd_target_result rvswd_target_session_flash_write_protected(
    struct rvswd_target_session *session) {
    bool value = false;
    struct rvswd_target_result result;
    struct rvswd_operation operation;

    if (session == NULL) {
        return rvswd_target_session_invalid_result();
    }
    rvswd_operation_init(&operation, &session->transport);
    if (!rvswd_flash_write_protected(&operation, session->info.profile,
                                     &value)) {
        return rvswd_target_session_operation_result(
            &operation, RVSWD_TARGET_RESULT_FLASH, operation.flash_code, false);
    }
    result = rvswd_target_result_success();
    result.value = value ? 1u : 0u;
    return result;
}

struct rvswd_target_result rvswd_target_session_flash_set_read_protected(
    struct rvswd_target_session *session, bool protected) {
    struct rvswd_operation operation;
    bool success;

    if (session == NULL) {
        return rvswd_target_session_invalid_result();
    }
    rvswd_operation_init(&operation, &session->transport);
    success = rvswd_flash_set_read_protected(
        &operation, session->info.profile, protected);
    return rvswd_target_session_operation_result(
        &operation, RVSWD_TARGET_RESULT_FLASH, operation.flash_code, success);
}
