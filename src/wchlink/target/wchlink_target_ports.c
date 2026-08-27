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
    // 连接成功后只使用已锁定 profile，hint 只参与连接前候选解析
    if (ports->profile != NULL) {
        return ports->profile;
    }
    return rvswd_target_profile_resolve(ports->info.chip_id,
                                        ports->family_hint,
                                        ports->family_hint_active);
}

static const struct rvswd_target_loader_profile *
wchlink_target_ports_current_loader(const struct wchlink_target_ports *ports) {
    const struct rvswd_target_profile *profile =
        wchlink_target_ports_current_profile(ports);

    return profile == NULL ? NULL : profile->loader;
}

void wchlink_target_ports_refresh_info(struct wchlink_target_ports *ports) {
    const struct rvswd_target_loader_profile *loader;
    const struct rvswd_target_profile *profile;

    if (ports == NULL) {
        return;
    }
    profile = wchlink_target_ports_current_profile(ports);
    if (profile == NULL) {
        profile = rvswd_target_profile_from_family(ports->info.family);
    }
    loader = profile == NULL ? NULL : profile->loader;
    ports->info.loader =
        loader == NULL ? RVSWD_TARGET_LOADER_DEFAULT : loader->kind;
    ports->info.loader_download_limit =
        loader == NULL ? 0u : loader->download_limit;
    ports->info.loader_data_page_size =
        loader == NULL ? 0u : loader->data_page_size;
    ports->info.loader_variable_length =
        loader != NULL && loader->variable_length;
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

struct rvswd_target_chip_info_result wchlink_target_ports_read_chip_info(
    struct wchlink_target_ports *ports) {
    struct rvswd_target_chip_info_result chip_info = {0};
    struct rvswd_target_result read_result;

    if (ports == NULL) {
        chip_info.result = wchlink_target_ports_invalid_result(
            RVSWD_TARGET_RESULT_MEMORY);
        return chip_info;
    }
    chip_info.info.ch5xx =
        ports->info.loader == RVSWD_TARGET_LOADER_CH5XX;
    chip_info.info.chip_id = ports->info.chip_id;
    if (chip_info.info.ch5xx) {
        chip_info.result = rvswd_target_result_success();
        return chip_info;
    }

    // 非 CH5xx 目标通过 ESIG 提供 Flash 容量和 96 位 UID
    read_result = wchlink_target_ports_read_memory32(ports, 0x1ffff7e0u);
    if (!read_result.ok) {
        chip_info.result = read_result;
        return chip_info;
    }
    chip_info.info.flash_size = read_result.value;
    read_result = wchlink_target_ports_read_memory32(ports, 0x1ffff7e8u);
    if (!read_result.ok) {
        chip_info.result = read_result;
        return chip_info;
    }
    chip_info.info.uid_low = read_result.value;
    read_result = wchlink_target_ports_read_memory32(ports, 0x1ffff7ecu);
    if (!read_result.ok) {
        chip_info.result = read_result;
        return chip_info;
    }
    chip_info.info.uid_high = read_result.value;
    read_result = wchlink_target_ports_read_memory32(ports, 0x1ffff7f0u);
    if (!read_result.ok) {
        chip_info.result = read_result;
        return chip_info;
    }
    chip_info.info.uid_tail = read_result.value;
    chip_info.result = rvswd_target_result_success();
    return chip_info;
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

struct rvswd_target_result wchlink_target_ports_resume_dmi(
    struct wchlink_target_ports *ports, uint32_t dmcontrol) {
    struct rvswd_operation operation;
    struct rvswd_target_result result;
    uint32_t dmstatus = 0u;
    bool success;

    if (ports == NULL) {
        return wchlink_target_ports_invalid_result(RVSWD_TARGET_RESULT_DMI);
    }
    rvswd_operation_init(&operation, &ports->transport);
    success = rvswd_debug_resume(&operation, dmcontrol, &dmstatus);
    result = wchlink_target_ports_operation_result(
        &operation, RVSWD_TARGET_RESULT_DMI, operation.dmi_status, success);
    if (result.ok) {
        result.value = dmstatus;
    }
    return result;
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

static struct rvswd_target_result wchlink_target_ports_write_memory32(
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

static struct rvswd_target_result wchlink_target_ports_write_memory(
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

static struct rvswd_target_result wchlink_target_ports_execute(
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

struct rvswd_target_result wchlink_target_ports_write_loader_code(
    struct wchlink_target_ports *ports, uint32_t offset,
    const uint8_t *data, uint32_t length) {
    const struct rvswd_target_loader_profile *loader;
    struct rvswd_target_result result;

    if (ports == NULL) {
        return wchlink_target_ports_invalid_result(RVSWD_TARGET_RESULT_MEMORY);
    }
    loader = wchlink_target_ports_current_loader(ports);
    if (loader == NULL) {
        return wchlink_target_ports_invalid_result(RVSWD_TARGET_RESULT_MEMORY);
    }
    if (data == NULL || length == 0u ||
        length > loader->download_packet_size ||
        offset > loader->download_limit ||
        length > loader->download_limit - offset) {
        // 本地长度错误仍报告目标 loader 写入位置，保持既有 wire 诊断字段
        result = rvswd_target_result_failure(RVSWD_TARGET_RESULT_MEMORY, 0xefu,
                                             false);
        result.address = loader->code_address + offset;
        result.abstractcs = 0xffffffffu;
        return result;
    }
    return wchlink_target_ports_write_memory(
        ports, loader->code_address + offset, data, length);
}

struct rvswd_target_result wchlink_target_ports_write_loader_data(
    struct wchlink_target_ports *ports, uint32_t offset,
    const uint8_t *data, uint32_t length) {
    const struct rvswd_target_loader_profile *loader;

    if (ports == NULL) {
        return wchlink_target_ports_invalid_result(RVSWD_TARGET_RESULT_MEMORY);
    }
    loader = wchlink_target_ports_current_loader(ports);
    if (loader == NULL) {
        return wchlink_target_ports_invalid_result(RVSWD_TARGET_RESULT_MEMORY);
    }
    return wchlink_target_ports_write_memory(
        ports, loader->data_address + offset, data, length);
}

struct rvswd_target_result wchlink_target_ports_execute_loader(
    struct wchlink_target_ports *ports,
    const struct rvswd_target_loader_execute *request) {
    const struct rvswd_target_loader_profile *loader;
    struct rvswd_target_result result;

    if (ports == NULL || request == NULL) {
        return wchlink_target_ports_invalid_result(RVSWD_TARGET_RESULT_DEBUG);
    }
    loader = wchlink_target_ports_current_loader(ports);
    if (loader == NULL) {
        return wchlink_target_ports_invalid_result(RVSWD_TARGET_RESULT_DEBUG);
    }
    if (request->write_checksum && loader->checksum_address != 0u) {
        result = wchlink_target_ports_write_memory32(
            ports, loader->checksum_address, request->checksum);
        if (!result.ok) {
            // checksum 写入失败沿用旧数据端点状态 0x15
            result.value = 0x15u;
            return result;
        }
    }
    return wchlink_target_ports_execute(
        ports, loader->code_address, loader->stack_top, request->mode,
        request->address, request->length, loader->data_address);
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

struct rvswd_target_result wchlink_target_ports_flash_set_option_bytes(
    struct wchlink_target_ports *ports, const uint8_t *values, size_t count) {
    struct rvswd_operation operation;
    bool success;

    if (ports == NULL || values == NULL) {
        return wchlink_target_ports_invalid_result(RVSWD_TARGET_RESULT_FLASH);
    }
    rvswd_operation_init(&operation, &ports->transport);
    success = rvswd_flash_set_option_bytes(
        &operation, ports->profile, values, count);
    return wchlink_target_ports_operation_result(
        &operation, RVSWD_TARGET_RESULT_FLASH, operation.flash_code, success);
}
