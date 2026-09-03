#include "wchlink/rvswd/rvswd_debug.h"
#include "wchlink/rvswd/rvswd_memory.h"
#include "wchlink/rvswd/rvswd_operation.h"
#include "wchlink/rvswd/rvswd_reset.h"
#include "wchlink/rvswd/rvswd_types.h"
#include "wchlink/target/rvswd_target_module.h"
#include "wchlink/target/rvswd_target_registry.h"
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
    // 连接成功后只使用已锁定 module，连接前没有可用 target profile
    return ports == NULL || ports->module == NULL ? NULL
                                                  : ports->module->profile;
}

static void wchlink_target_ports_operation_init(
    struct wchlink_target_ports *ports, struct rvswd_operation *operation) {
    rvswd_operation_init(operation, &ports->transport);
    operation->memory = ports->module == NULL ? NULL : ports->module->memory;
}

static const struct rvswd_target_loader_profile *
wchlink_target_ports_current_loader(const struct wchlink_target_ports *ports) {
    const struct rvswd_target_profile *profile =
        wchlink_target_ports_current_profile(ports);

    return profile == NULL ? NULL : profile->loader;
}

static bool wchlink_target_ports_loader_mode(
    const struct rvswd_target_loader_profile *loader,
    enum wchlink_target_loader_operation operation, uint32_t *mode) {
    if (loader == NULL || mode == NULL) {
        return false;
    }
    switch (operation) {
        case WCHLINK_TARGET_LOADER_INITIALIZE:
            *mode = loader->initialize_mode;
            return true;
        case WCHLINK_TARGET_LOADER_INITIALIZE_PREPARED:
            *mode = loader->prepared_mode;
            return true;
        case WCHLINK_TARGET_LOADER_PROGRAM:
            *mode = loader->program_mode;
            return true;
        case WCHLINK_TARGET_LOADER_VERIFY:
            *mode = loader->verify_mode;
            return true;
        case WCHLINK_TARGET_LOADER_PROGRAM_VERIFY:
            *mode = loader->program_verify_mode;
            return true;
        default:
            return false;
    }
}

void wchlink_target_ports_refresh_info(struct wchlink_target_ports *ports) {
    const struct rvswd_target_loader_profile *loader;
    const struct rvswd_target_profile *profile;

    if (ports == NULL) {
        return;
    }
    profile = wchlink_target_ports_current_profile(ports);
    loader = profile == NULL ? NULL : profile->loader;
    ports->info.loader_download_limit =
        loader == NULL ? 0u : loader->download_limit;
    ports->info.loader_data_page_size =
        loader == NULL ? 0u : loader->data_page_size;
    ports->info.loader_initialize_mode =
        loader == NULL ? 0u : loader->initialize_mode;
    ports->info.loader_prepared_mode =
        loader == NULL ? 0u : loader->prepared_mode;
    ports->info.loader_program_mode =
        loader == NULL ? 0u : loader->program_mode;
    ports->info.loader_verify_mode =
        loader == NULL ? 0u : loader->verify_mode;
    ports->info.loader_program_verify_mode =
        loader == NULL ? 0u : loader->program_verify_mode;
    ports->info.loader_checksum_mode_mask =
        loader == NULL ? 0u : loader->checksum_mode_mask;
    ports->info.loader_length_mode_mask =
        loader == NULL ? 0u : loader->length_mode_mask;
    ports->info.loader_repeat_initialize =
        loader != NULL && loader->repeat_initialize;
    ports->info.partial_write_supported =
        loader != NULL && loader->partial_write_supported;
    ports->info.loader_variable_length =
        loader != NULL && loader->variable_length;
    ports->info.memory_streaming =
        ports->module != NULL && ports->module->capabilities != NULL &&
        ports->module->capabilities->memory_streaming;
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
    const struct rvswd_target_module *module;
    uint8_t family_hint;
    uint8_t requested_speed;

    if (ports == NULL) {
        return;
    }
    // MRS 在重建 transport 前发送 family hint，初始化只能清除探测结果
    family_hint = ports->family_hint;
    requested_speed = ports->requested_speed;
    memset(ports, 0, sizeof(*ports));
    ports->family_hint = family_hint;
    ports->requested_speed = requested_speed;
    // MRS 会在 SetSpeed 后重建 target 状态，保留 module 才能继续走已知族的连接路径
    module = rvswd_target_registry_module_from_family(family_hint);
    ports->module = module;
    rvswd_transport_init(&ports->transport);
    if (module != NULL && module->capabilities != NULL) {
        rvswd_transport_set_packet_mode(
            &ports->transport, module->capabilities->packet_mode);
    }
    if (requested_speed != 0u) {
        rvswd_transport_set_fast_timing(&ports->transport,
                                        requested_speed != 0x03u);
    }
}

void wchlink_target_ports_disconnect(struct wchlink_target_ports *ports) {
    if (ports == NULL) {
        return;
    }
    rvswd_transport_disconnect(&ports->transport);
    ports->info.connected = false;
    ports->module = NULL;
}

void wchlink_target_ports_set_speed(struct wchlink_target_ports *ports,
                                    uint8_t speed) {
    if (ports == NULL) {
        return;
    }
    ports->requested_speed = speed;
    rvswd_transport_set_fast_timing(&ports->transport, speed != 0x03u);
}

void wchlink_target_ports_set_family_hint(
    struct wchlink_target_ports *ports, uint8_t family) {
    if (ports == NULL) {
        return;
    }
    ports->family_hint = family;
    ports->family_hint_active = false;
    if (!ports->info.connected) {
        ports->info.family = family;
        ports->module = rvswd_target_registry_module_from_family(family);
        rvswd_transport_set_packet_mode(
            &ports->transport,
            ports->module != NULL && ports->module->capabilities != NULL
                ? ports->module->capabilities->packet_mode
                : RVSWD_PACKET_SHORT);
        wchlink_target_ports_refresh_info(ports);
    }
}

struct rvswd_target_info wchlink_target_ports_info(
    const struct wchlink_target_ports *ports) {
    return ports->info;
}

bool wchlink_target_ports_is_connected(
    const struct wchlink_target_ports *ports) {
    return ports != NULL && ports->info.connected;
}

bool wchlink_target_ports_uses_legacy_info(
    const struct wchlink_target_ports *ports) {
    return ports != NULL && ports->module != NULL &&
           ports->module->capabilities != NULL &&
           ports->module->capabilities->chip_info_layout ==
               RVSWD_TARGET_CHIP_INFO_LEGACY;
}

bool wchlink_target_ports_loader_start(
    struct wchlink_target_ports *ports,
    struct wchlink_target_loader_start *start) {
    const struct rvswd_target_loader_profile *loader;

    if (ports == NULL || start == NULL || !ports->info.connected) {
        return false;
    }
    loader = wchlink_target_ports_current_loader(ports);
    if (loader == NULL || loader->download_limit == 0u) {
        return false;
    }
    start->download_limit = loader->download_limit;
    start->variable_length = loader->variable_length;
    return true;
}

bool wchlink_target_ports_loader_supports_partial_write(
    const struct wchlink_target_ports *ports) {
    const struct rvswd_target_loader_profile *loader =
        wchlink_target_ports_current_loader(ports);

    return loader != NULL && loader->partial_write_supported;
}

bool wchlink_target_ports_loader_uses_streaming(
    const struct wchlink_target_ports *ports) {
    return ports != NULL && ports->module != NULL &&
           ports->module->capabilities != NULL &&
           ports->module->capabilities->memory_streaming;
}

bool wchlink_target_ports_loader_repeats_initialize(
    const struct wchlink_target_ports *ports) {
    const struct rvswd_target_loader_profile *loader =
        wchlink_target_ports_current_loader(ports);

    return loader != NULL && loader->repeat_initialize;
}

uint32_t wchlink_target_ports_loader_data_length(
    const struct wchlink_target_ports *ports, uint32_t length) {
    const struct rvswd_target_loader_profile *loader =
        wchlink_target_ports_current_loader(ports);
    uint32_t page_size = loader == NULL ? 0u : loader->data_page_size;

    if (page_size > 1u) {
        length = (length + (page_size - 1u)) & ~(page_size - 1u);
    }
    return length;
}

struct rvswd_target_chip_info_result wchlink_target_ports_read_chip_info(
    struct wchlink_target_ports *ports) {
    struct rvswd_target_chip_info_result chip_info = {0};
    struct rvswd_target_result read_result;
    const struct rvswd_target_profile *profile;

    if (ports == NULL) {
        chip_info.result = wchlink_target_ports_invalid_result(
            RVSWD_TARGET_RESULT_MEMORY);
        return chip_info;
    }
    chip_info.info.legacy_layout = wchlink_target_ports_uses_legacy_info(ports);
    chip_info.info.chip_id = ports->info.chip_id;
    if (chip_info.info.legacy_layout) {
        chip_info.result = rvswd_target_result_success();
        return chip_info;
    }

    profile = wchlink_target_ports_current_profile(ports);
    if (profile == NULL || profile->identity == NULL ||
        profile->identity->esig_flash_size_address == 0u) {
        chip_info.result = wchlink_target_ports_invalid_result(
            RVSWD_TARGET_RESULT_MEMORY);
        return chip_info;
    }

    // 非 CH58X/CH59X 目标通过 ESIG 提供 Flash 容量和 96 位 UID
    read_result = wchlink_target_ports_read_memory32(
        ports, profile->identity->esig_flash_size_address);
    if (!read_result.ok) {
        chip_info.result = read_result;
        return chip_info;
    }
    chip_info.info.flash_size = read_result.value;
    read_result = wchlink_target_ports_read_memory32(
        ports, profile->identity->esig_uid_low_address);
    if (!read_result.ok) {
        chip_info.result = read_result;
        return chip_info;
    }
    chip_info.info.uid_low = read_result.value;
    read_result = wchlink_target_ports_read_memory32(
        ports, profile->identity->esig_uid_high_address);
    if (!read_result.ok) {
        chip_info.result = read_result;
        return chip_info;
    }
    chip_info.info.uid_high = read_result.value;
    read_result = wchlink_target_ports_read_memory32(
        ports, profile->identity->esig_uid_tail_address);
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
    wchlink_target_ports_operation_init(ports, &operation);
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
    wchlink_target_ports_operation_init(ports, &operation);
    operation.address = address;
    success = ports->module != NULL && ports->module->memory != NULL &&
              ports->module->memory->read32(
                  &operation, address, &value);
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
    const struct rvswd_target_profile *profile;
    struct rvswd_operation operation;
    bool success;

    if (ports == NULL) {
        return wchlink_target_ports_invalid_result(RVSWD_TARGET_RESULT_MEMORY);
    }
    wchlink_target_ports_operation_init(ports, &operation);
    operation.address = address;
    profile = wchlink_target_ports_current_profile(ports);
    // loader 运行后会清除调试解锁，checksum mailbox 写入也必须重新解锁
    if (profile != NULL && profile->loader_clears_debug_unlock &&
        !rvswd_debug_restore_unlock(&operation)) {
        operation.memory_code = 0xd1u;
        return wchlink_target_ports_operation_result(
            &operation, RVSWD_TARGET_RESULT_MEMORY, operation.memory_code, false);
    }
    success = profile != NULL && ports->module != NULL &&
              ports->module->memory != NULL &&
              ports->module->memory->write32(&operation, address, value);
    return wchlink_target_ports_operation_result(
        &operation, RVSWD_TARGET_RESULT_MEMORY,
        operation.memory_code == 0u ? 0x15u : operation.memory_code, success);
}

static struct rvswd_target_result wchlink_target_ports_write_memory(
    struct wchlink_target_ports *ports, uint32_t address,
    const uint8_t *data, uint32_t length) {
    const struct rvswd_target_profile *profile;
    struct rvswd_operation operation;
    bool success;

    if (ports == NULL) {
        return wchlink_target_ports_invalid_result(RVSWD_TARGET_RESULT_MEMORY);
    }
    if (data == NULL || length == 0u) {
        return wchlink_target_ports_invalid_result(RVSWD_TARGET_RESULT_CONNECT);
    }
    wchlink_target_ports_operation_init(ports, &operation);
    profile = wchlink_target_ports_current_profile(ports);
    // loader 运行后会清除调试解锁，后续 SRAM 和 mailbox 写入必须重新解锁
    if (profile != NULL && profile->loader_clears_debug_unlock &&
        !rvswd_debug_restore_unlock(&operation)) {
        operation.memory_code = 0xd1u;
        operation.address = address;
        return wchlink_target_ports_operation_result(
            &operation, RVSWD_TARGET_RESULT_MEMORY, operation.memory_code, false);
    }
    success = profile != NULL && ports->module != NULL &&
              ports->module->memory != NULL &&
              ports->module->memory->write(&operation, address, data, length);
    return wchlink_target_ports_operation_result(
        &operation, RVSWD_TARGET_RESULT_MEMORY,
        operation.memory_code == 0u ? 0x15u : operation.memory_code, success);
}

static struct rvswd_target_result wchlink_target_ports_execute(
    struct wchlink_target_ports *ports, uint32_t entry, uint32_t stack_top,
    uint32_t mode, uint32_t address, uint32_t length, uint32_t data_address,
    uint32_t dpc_value) {
    uint32_t value = 0xffffffffu;
    struct rvswd_target_result result;
    struct rvswd_operation operation;
    const struct rvswd_target_profile *profile;
    const struct rvswd_target_module *module;
    bool success;

    if (ports == NULL) {
        return wchlink_target_ports_invalid_result(RVSWD_TARGET_RESULT_DEBUG);
    }
    wchlink_target_ports_operation_init(ports, &operation);
    operation.address = address;
    profile = ports->module == NULL ? NULL : ports->module->profile;
    module = ports->module;
    // 每次 loader 执行前都重新写入解锁值，确保 prepare 的目标内存访问有效
    if (!rvswd_debug_restore_unlock(&operation)) {
        value = 0xe00au;
        result = wchlink_target_ports_operation_result(
            &operation, RVSWD_TARGET_RESULT_DEBUG, value, false);
        result.value = value;
        result.address = address;
        return result;
    }
    // 族模块在 loader 首次启动前建立目标环境，未迁移族保持旧的空前置路径
    if (module != NULL && module->loader != NULL &&
        module->loader->prepare != NULL &&
        !module->loader->prepare(&operation, profile, mode)) {
        // 环境准备失败说明 DMI 已不可用，loader 返回值无法反映真实原因
        value = operation.memory_code == 0u ? 0x15u : operation.memory_code;
        result = wchlink_target_ports_operation_result(
            &operation, RVSWD_TARGET_RESULT_DEBUG, value, false);
        result.value = value;
        result.address = address;
        return result;
    }
    if (module != NULL && module->loader != NULL &&
        module->loader->execute != NULL) {
        success = module->loader->execute(
            &operation, entry, stack_top, mode, address, length, data_address,
            dpc_value, &value);
    } else {
        value = 0xe00bu;
        success = false;
    }
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
    uint32_t mode;
    bool write_checksum;
    struct rvswd_target_result result;

    if (ports == NULL || request == NULL) {
        return wchlink_target_ports_invalid_result(RVSWD_TARGET_RESULT_DEBUG);
    }
    loader = wchlink_target_ports_current_loader(ports);
    if (loader == NULL) {
        return wchlink_target_ports_invalid_result(RVSWD_TARGET_RESULT_DEBUG);
    }
    if (!wchlink_target_ports_loader_mode(loader, request->operation, &mode)) {
        return wchlink_target_ports_invalid_result(RVSWD_TARGET_RESULT_DEBUG);
    }
    write_checksum = (mode & loader->checksum_mode_mask) != 0u;
    if (write_checksum && loader->checksum_address != 0u) {
        result = wchlink_target_ports_write_memory32(
            ports, loader->checksum_address, request->checksum);
        if (!result.ok) {
            // checksum 写入失败沿用旧数据端点状态 0x15
            result.value = 0x15u;
            return result;
        }
    }
    if (!write_checksum &&
        (mode & loader->length_mode_mask) != 0u &&
        loader->length_address != 0u) {
        result = wchlink_target_ports_write_memory32(
            ports, loader->length_address, request->length);
        if (!result.ok) {
            // 长度 mailbox 写入失败时不启动目标 loader
            result.value = 0x15u;
            return result;
        }
    }
    return wchlink_target_ports_execute(
        ports, loader->code_address, loader->stack_top, mode,
        request->address, request->length, loader->data_address,
        loader->dpc_value);
}

struct rvswd_target_result wchlink_target_ports_reset_and_halt(
    struct wchlink_target_ports *ports) {
    struct rvswd_operation operation;
    bool success;

    if (ports == NULL) {
        return wchlink_target_ports_invalid_result(RVSWD_TARGET_RESULT_RESET);
    }
    wchlink_target_ports_operation_init(ports, &operation);
    success = ports->module != NULL && ports->module->control != NULL &&
              ports->module->control->reset_and_halt != NULL &&
              ports->module->control->reset_and_halt(&operation);
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
    wchlink_target_ports_operation_init(ports, &operation);
    success = ports->module != NULL && ports->module->control != NULL &&
              ports->module->control->soft_reset_and_run != NULL &&
              ports->module->control->soft_reset_and_run(&operation);
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
    wchlink_target_ports_operation_init(ports, &operation);
    success = ports->module != NULL && ports->module->control != NULL &&
              ports->module->control->reset_and_run != NULL &&
              ports->module->control->reset_and_run(&operation);
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
    wchlink_target_ports_operation_init(ports, &operation);
    success = ports->module != NULL && ports->module->flash != NULL &&
              ports->module->flash->erase_all != NULL &&
              ports->module->flash->erase_all(
                  &operation, wchlink_target_ports_current_profile(ports));
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
    wchlink_target_ports_operation_init(ports, &operation);
    operation.address = address;
    success = ports->module != NULL && ports->module->flash != NULL &&
              ports->module->flash->rewrite_page != NULL &&
              ports->module->flash->rewrite_page(
                  &operation, wchlink_target_ports_current_profile(ports),
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
    wchlink_target_ports_operation_init(ports, &operation);
    if (ports->module == NULL || ports->module->flash == NULL ||
        ports->module->flash->read_protected == NULL ||
        !ports->module->flash->read_protected(
            &operation, wchlink_target_ports_current_profile(ports), &value)) {
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
    wchlink_target_ports_operation_init(ports, &operation);
    if (ports->module == NULL || ports->module->flash == NULL ||
        ports->module->flash->write_protected == NULL ||
        !ports->module->flash->write_protected(
            &operation, wchlink_target_ports_current_profile(ports), &value)) {
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
    wchlink_target_ports_operation_init(ports, &operation);
    success = ports->module != NULL && ports->module->flash != NULL &&
              ports->module->flash->set_read_protected != NULL &&
              ports->module->flash->set_read_protected(
                  &operation, wchlink_target_ports_current_profile(ports),
                  protected);
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
    wchlink_target_ports_operation_init(ports, &operation);
    success = ports->module != NULL && ports->module->flash != NULL &&
              ports->module->flash->set_option_bytes != NULL &&
              ports->module->flash->set_option_bytes(
                  &operation, wchlink_target_ports_current_profile(ports),
                  values, count);
    return wchlink_target_ports_operation_result(
        &operation, RVSWD_TARGET_RESULT_FLASH, operation.flash_code, success);
}

struct rvswd_target_result wchlink_target_ports_flash_read_memory_type(
    struct wchlink_target_ports *ports, bool extended) {
    struct rvswd_operation operation;
    struct rvswd_target_result result;
    uint8_t memory_type = 0u;

    if (ports == NULL) {
        return wchlink_target_ports_invalid_result(RVSWD_TARGET_RESULT_FLASH);
    }
    wchlink_target_ports_operation_init(ports, &operation);
    if (ports->module == NULL || ports->module->flash == NULL ||
        ports->module->flash->read_memory_type == NULL ||
        !ports->module->flash->read_memory_type(
            &operation, wchlink_target_ports_current_profile(ports), extended,
            &memory_type)) {
        return wchlink_target_ports_operation_result(
            &operation, RVSWD_TARGET_RESULT_FLASH, operation.flash_code, false);
    }
    result = rvswd_target_result_success();
    result.value = memory_type;
    return result;
}

struct rvswd_target_result wchlink_target_ports_flash_set_memory_type(
    struct wchlink_target_ports *ports, bool extended, uint8_t memory_type) {
    struct rvswd_operation operation;
    bool success;

    if (ports == NULL) {
        return wchlink_target_ports_invalid_result(RVSWD_TARGET_RESULT_FLASH);
    }
    wchlink_target_ports_operation_init(ports, &operation);
    success = ports->module != NULL && ports->module->flash != NULL &&
              ports->module->flash->set_memory_type != NULL &&
              ports->module->flash->set_memory_type(
                  &operation, wchlink_target_ports_current_profile(ports),
                  extended, memory_type);
    return wchlink_target_ports_operation_result(
        &operation, RVSWD_TARGET_RESULT_FLASH, operation.flash_code, success);
}
