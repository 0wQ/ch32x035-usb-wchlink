#include "wchlink/flash/rvswd_flash.h"
#include "wchlink/protocol/wchlink_family.h"
#include "wchlink/rvswd/rvswd_debug.h"
#include "wchlink/rvswd/rvswd_memory.h"
#include "wchlink/rvswd/rvswd_operation.h"
#include "wchlink/rvswd/rvswd_reset.h"
#include "wchlink/rvswd/rvswd_types.h"
#include "wchlink/target/rvswd_target_profile.h"
#include "wchlink/target/wchlink_target_control.h"
#include "wchlink/target/wchlink_target_dmi.h"
#include "wchlink/target/wchlink_target_flash.h"
#include "wchlink/target/wchlink_target_ports_internal.h"
#include "wchlink/target/wchlink_target_transfer.h"
#include "wchlink/transport/rvswd_transport.h"

#include <stddef.h>
#include <string.h>

static const struct rvswd_target_profile *wchlink_target_ports_current_profile(
    const struct wchlink_target_ports *ports) {
    return rvswd_target_profile_resolve(ports->info.chip_id,
                                        ports->family_hint,
                                        ports->family_hint_active);
}

void wchlink_target_ports_refresh_info(struct wchlink_target_ports *ports) {
    const struct rvswd_target_profile *family_profile;
    const struct rvswd_target_profile *profile;

    if (ports == NULL) {
        return;
    }
    family_profile = rvswd_target_profile_from_family(ports->info.family);
    ports->info.loader = family_profile == NULL
                             ? RVSWD_TARGET_LOADER_DEFAULT
                             : family_profile->loader;
    profile = wchlink_target_ports_current_profile(ports);
    ports->info.memory_streaming =
        profile != NULL &&
        profile->memory_write_mode == RVSWD_MEMORY_WRITE_STREAMING;
}

static const struct rvswd_target_profile *
wchlink_target_ports_memory_profile(
    const struct wchlink_target_ports *ports) {
    const struct rvswd_target_profile *profile =
        wchlink_target_ports_current_profile(ports);

    if (profile != NULL) {
        return profile;
    }
    return rvswd_target_profile_from_family(ports->family_hint);
}

static struct rvswd_target_result wchlink_target_ports_invalid_result(
    enum rvswd_target_result_domain domain) {
    return rvswd_target_result_failure(domain, 0x01u, false);
}

static struct rvswd_target_result wchlink_target_ports_dmi_result(
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

static struct rvswd_target_result wchlink_target_ports_operation_result(
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

void wchlink_target_ports_init(struct wchlink_target_ports *ports) {
    uint8_t family_hint;

    if (ports == NULL) {
        return;
    }
    // MRS 在重建 transport 前发送 family hint，初始化只能清除探测结果
    family_hint = ports->family_hint;
    memset(ports, 0, sizeof(*ports));
    ports->family_hint = family_hint;
    rvswd_transport_init(&ports->transport);
}

void wchlink_target_ports_disconnect(struct wchlink_target_ports *ports) {
    if (ports == NULL) {
        return;
    }
    rvswd_transport_disconnect(&ports->transport);
    ports->info.connected = false;
}

void wchlink_target_ports_set_family_hint(
    struct wchlink_target_ports *ports, uint8_t family) {
    if (ports == NULL) {
        return;
    }
    ports->family_hint = family;
    ports->family_hint_active = false;
    rvswd_transport_set_packet_mode(
        &ports->transport, family == WCHLINK_TARGET_FAMILY_CH58X
                               ? RVSWD_PACKET_LONG
                               : RVSWD_PACKET_SHORT);
    if (!ports->info.connected) {
        ports->info.family = family;
        ports->profile = NULL;
        wchlink_target_ports_refresh_info(ports);
    }
}

struct rvswd_target_info wchlink_target_ports_info(
    const struct wchlink_target_ports *ports) {
    return ports->info;
}

struct rvswd_target_result wchlink_target_ports_read_dmi(
    struct wchlink_target_ports *ports, uint8_t address) {
    uint32_t value = 0u;
    struct rvswd_transport_result transport_result;

    if (ports == NULL) {
        return wchlink_target_ports_invalid_result(RVSWD_TARGET_RESULT_DMI);
    }
    transport_result = rvswd_transport_read(&ports->transport, address);
    value = transport_result.value;
    return wchlink_target_ports_dmi_result(transport_result, value);
}

struct rvswd_target_result wchlink_target_ports_write_dmi(
    struct wchlink_target_ports *ports, uint8_t address, uint32_t value) {
    struct rvswd_transport_result transport_result;

    if (ports == NULL) {
        return wchlink_target_ports_invalid_result(RVSWD_TARGET_RESULT_DMI);
    }
    transport_result =
        rvswd_transport_write(&ports->transport, address, value);
    return wchlink_target_ports_dmi_result(transport_result, 0u);
}

struct rvswd_target_result wchlink_target_ports_read_memory32(
    struct wchlink_target_ports *ports, uint32_t address) {
    uint32_t value = 0u;
    struct rvswd_target_result result;
    struct rvswd_operation operation;
    bool success;

    if (ports == NULL) {
        return wchlink_target_ports_invalid_result(RVSWD_TARGET_RESULT_MEMORY);
    }
    rvswd_operation_init(&operation, &ports->transport);
    operation.address = address;
    success = rvswd_memory_read32(
        &operation, wchlink_target_ports_memory_profile(ports),
        ports->info.chip_id != 0u, address, &value);
    result = wchlink_target_ports_operation_result(
        &operation, RVSWD_TARGET_RESULT_MEMORY,
        operation.memory_code == 0u ? 0x15u : operation.memory_code, success);
    if (result.ok) {
        result.value = value;
    }
    return result;
}

struct rvswd_target_result wchlink_target_ports_write_memory32(
    struct wchlink_target_ports *ports, uint32_t address, uint32_t value) {
    struct rvswd_operation operation;
    bool success;

    if (ports == NULL) {
        return wchlink_target_ports_invalid_result(RVSWD_TARGET_RESULT_MEMORY);
    }
    rvswd_operation_init(&operation, &ports->transport);
    operation.address = address;
    success = rvswd_memory_write32(&operation, address, value);
    return wchlink_target_ports_operation_result(
        &operation, RVSWD_TARGET_RESULT_MEMORY,
        operation.memory_code == 0u ? 0x15u : operation.memory_code, success);
}

struct rvswd_target_result wchlink_target_ports_write_memory(
    struct wchlink_target_ports *ports, uint32_t address,
    const uint8_t *data, uint32_t length) {
    struct rvswd_operation operation;
    bool success;

    if (ports == NULL) {
        return wchlink_target_ports_invalid_result(RVSWD_TARGET_RESULT_MEMORY);
    }
    if (data == NULL || length == 0u) {
        return wchlink_target_ports_invalid_result(RVSWD_TARGET_RESULT_CONNECT);
    }
    rvswd_operation_init(&operation, &ports->transport);
    success = rvswd_memory_write(
        &operation, wchlink_target_ports_current_profile(ports), address, data,
        length);
    return wchlink_target_ports_operation_result(
        &operation, RVSWD_TARGET_RESULT_MEMORY,
        operation.memory_code == 0u ? 0x15u : operation.memory_code, success);
}

struct rvswd_target_result wchlink_target_ports_execute(
    struct wchlink_target_ports *ports, uint32_t entry, uint32_t stack_top,
    uint32_t mode, uint32_t address, uint32_t length, uint32_t data_address) {
    uint32_t value = 0xffffffffu;
    struct rvswd_target_result result;
    struct rvswd_operation operation;
    bool success;

    if (ports == NULL) {
        return wchlink_target_ports_invalid_result(RVSWD_TARGET_RESULT_DEBUG);
    }
    rvswd_operation_init(&operation, &ports->transport);
    operation.address = address;
    success = rvswd_debug_execute(&operation, entry, stack_top, mode, address,
                                  length, data_address, &value);
    result = wchlink_target_ports_operation_result(
        &operation, RVSWD_TARGET_RESULT_DEBUG,
        value == 0xffffffffu ? 0x15u : value, success);
    result.value = value;
    result.address = address;
    return result;
}

struct rvswd_target_result wchlink_target_ports_reset_and_halt(
    struct wchlink_target_ports *ports) {
    struct rvswd_operation operation;
    bool success;

    if (ports == NULL) {
        return wchlink_target_ports_invalid_result(RVSWD_TARGET_RESULT_RESET);
    }
    rvswd_operation_init(&operation, &ports->transport);
    success = rvswd_reset_and_halt(&operation);
    return wchlink_target_ports_operation_result(
        &operation, RVSWD_TARGET_RESULT_RESET, operation.dmi_status, success);
}

struct rvswd_target_result wchlink_target_ports_soft_reset_and_run(
    struct wchlink_target_ports *ports) {
    struct rvswd_operation operation;
    bool success;

    if (ports == NULL) {
        return wchlink_target_ports_invalid_result(RVSWD_TARGET_RESULT_RESET);
    }
    rvswd_operation_init(&operation, &ports->transport);
    success = rvswd_soft_reset_and_run(&operation);
    return wchlink_target_ports_operation_result(
        &operation, RVSWD_TARGET_RESULT_RESET, operation.dmi_status, success);
}

struct rvswd_target_result wchlink_target_ports_reset_and_run(
    struct wchlink_target_ports *ports) {
    struct rvswd_operation operation;
    bool success;

    if (ports == NULL) {
        return wchlink_target_ports_invalid_result(RVSWD_TARGET_RESULT_RESET);
    }
    rvswd_operation_init(&operation, &ports->transport);
    success = rvswd_reset_and_run(&operation);
    return wchlink_target_ports_operation_result(
        &operation, RVSWD_TARGET_RESULT_RESET, operation.dmi_status, success);
}

struct rvswd_target_result wchlink_target_ports_flash_erase_all(
    struct wchlink_target_ports *ports) {
    struct rvswd_operation operation;
    bool success;

    if (ports == NULL) {
        return wchlink_target_ports_invalid_result(RVSWD_TARGET_RESULT_FLASH);
    }
    rvswd_operation_init(&operation, &ports->transport);
    success = rvswd_flash_erase_all(&operation, ports->profile);
    return wchlink_target_ports_operation_result(
        &operation, RVSWD_TARGET_RESULT_FLASH, operation.flash_code, success);
}

struct rvswd_target_result wchlink_target_ports_flash_rewrite_page(
    struct wchlink_target_ports *ports, uint32_t address,
    const uint8_t *data) {
    struct rvswd_operation operation;
    bool success;

    if (ports == NULL) {
        return wchlink_target_ports_invalid_result(RVSWD_TARGET_RESULT_FLASH);
    }
    if (data == NULL) {
        return wchlink_target_ports_invalid_result(RVSWD_TARGET_RESULT_CONNECT);
    }
    rvswd_operation_init(&operation, &ports->transport);
    operation.address = address;
    success = rvswd_flash_rewrite_page(&operation, ports->profile,
                                       address, data);
    return wchlink_target_ports_operation_result(
        &operation, RVSWD_TARGET_RESULT_FLASH, operation.flash_code, success);
}

struct rvswd_target_result wchlink_target_ports_flash_read_protected(
    struct wchlink_target_ports *ports) {
    bool value = false;
    struct rvswd_target_result result;
    struct rvswd_operation operation;

    if (ports == NULL) {
        return wchlink_target_ports_invalid_result(RVSWD_TARGET_RESULT_FLASH);
    }
    rvswd_operation_init(&operation, &ports->transport);
    if (!rvswd_flash_read_protected(&operation, ports->profile, &value)) {
        return wchlink_target_ports_operation_result(
            &operation, RVSWD_TARGET_RESULT_FLASH, operation.flash_code, false);
    }
    result = rvswd_target_result_success();
    result.value = value ? 1u : 0u;
    return result;
}

struct rvswd_target_result wchlink_target_ports_flash_write_protected(
    struct wchlink_target_ports *ports) {
    bool value = false;
    struct rvswd_target_result result;
    struct rvswd_operation operation;

    if (ports == NULL) {
        return wchlink_target_ports_invalid_result(RVSWD_TARGET_RESULT_FLASH);
    }
    rvswd_operation_init(&operation, &ports->transport);
    if (!rvswd_flash_write_protected(&operation, ports->profile,
                                     &value)) {
        return wchlink_target_ports_operation_result(
            &operation, RVSWD_TARGET_RESULT_FLASH, operation.flash_code, false);
    }
    result = rvswd_target_result_success();
    result.value = value ? 1u : 0u;
    return result;
}

struct rvswd_target_result wchlink_target_ports_flash_set_read_protected(
    struct wchlink_target_ports *ports, bool protected) {
    struct rvswd_operation operation;
    bool success;

    if (ports == NULL) {
        return wchlink_target_ports_invalid_result(RVSWD_TARGET_RESULT_FLASH);
    }
    rvswd_operation_init(&operation, &ports->transport);
    success = rvswd_flash_set_read_protected(
        &operation, ports->profile, protected);
    return wchlink_target_ports_operation_result(
        &operation, RVSWD_TARGET_RESULT_FLASH, operation.flash_code, success);
}
