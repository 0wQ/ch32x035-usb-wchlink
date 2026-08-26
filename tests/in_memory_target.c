#include "in_memory_target.h"

#include <string.h>

static const uint32_t wchlink_test_ram_base = 0x20000000u;
static const uint32_t wchlink_test_system_base = 0x1ffff000u;
static const size_t wchlink_test_flash_page_size = 256u;

// 三个窗口覆盖 transfer 使用的目标 Flash、loader RAM 和 ESIG 地址
static bool wchlink_test_target_range(uint32_t address, uint32_t base,
                                      size_t size, size_t length,
                                      size_t *offset) {
    size_t relative;

    if (address < base || length > size) {
        return false;
    }
    relative = address - base;
    if (relative > size - length) {
        return false;
    }
    *offset = relative;
    return true;
}

static uint8_t *wchlink_test_target_memory(
    struct wchlink_target_ports *target, uint32_t address, size_t length) {
    size_t offset;

    if (wchlink_test_target_range(address, 0u, WCHLINK_TEST_FLASH_SIZE,
                                  length, &offset)) {
        return &target->flash[offset];
    }
    if (wchlink_test_target_range(address, wchlink_test_ram_base,
                                  WCHLINK_TEST_RAM_SIZE, length, &offset)) {
        return &target->ram[offset];
    }
    if (wchlink_test_target_range(address, wchlink_test_system_base,
                                  WCHLINK_TEST_SYSTEM_SIZE, length, &offset)) {
        return &target->system[offset];
    }
    return NULL;
}

static const uint8_t *wchlink_test_target_const_memory(
    const struct wchlink_target_ports *target, uint32_t address,
    size_t length) {
    size_t offset;

    if (wchlink_test_target_range(address, 0u, WCHLINK_TEST_FLASH_SIZE,
                                  length, &offset)) {
        return &target->flash[offset];
    }
    if (wchlink_test_target_range(address, wchlink_test_ram_base,
                                  WCHLINK_TEST_RAM_SIZE, length, &offset)) {
        return &target->ram[offset];
    }
    if (wchlink_test_target_range(address, wchlink_test_system_base,
                                  WCHLINK_TEST_SYSTEM_SIZE, length, &offset)) {
        return &target->system[offset];
    }
    return NULL;
}

static struct rvswd_target_result wchlink_test_target_memory_failure(
    uint32_t address) {
    struct rvswd_target_result result = rvswd_target_result_failure(
        RVSWD_TARGET_RESULT_MEMORY, 0x15u, false);

    result.address = address;
    return result;
}

static bool wchlink_test_target_take_failure(
    struct wchlink_target_ports *target,
    enum wchlink_test_target_operation operation,
    struct rvswd_target_result *result) {
    if (!target->failure_armed || target->failure_operation != operation) {
        return false;
    }
    // 故障脚本只影响下一次匹配操作，后续调用恢复确定性内存行为
    *result = target->failure_result;
    target->failure_armed = false;
    return true;
}

static struct rvswd_target_result wchlink_test_target_success(
    uint32_t value) {
    struct rvswd_target_result result = rvswd_target_result_success();

    result.value = value;
    return result;
}

void wchlink_test_target_reset(struct wchlink_target_ports *target,
                               struct rvswd_target_info info,
                               bool connected) {
    memset(target, 0, sizeof(*target));
    memset(target->flash, 0xff, sizeof(target->flash));
    memset(target->ram, 0xff, sizeof(target->ram));
    memset(target->system, 0xff, sizeof(target->system));
    info.connected = true;
    target->connect_info = info;
    target->info = info;
    target->info.connected = connected;
}

void wchlink_test_target_fail_next(
    struct wchlink_target_ports *target,
    enum wchlink_test_target_operation operation,
    struct rvswd_target_result result) {
    target->failure_operation = operation;
    target->failure_result = result;
    target->failure_armed = true;
}

void wchlink_test_target_set_execute_value(
    struct wchlink_target_ports *target, uint32_t value) {
    target->execute_value = value;
}

void wchlink_test_target_set_dmi(struct wchlink_target_ports *target,
                                 uint8_t address, uint32_t value) {
    target->dmi[address] = value;
}

bool wchlink_test_target_store(struct wchlink_target_ports *target,
                               uint32_t address, const uint8_t *data,
                               size_t length) {
    uint8_t *destination =
        wchlink_test_target_memory(target, address, length);

    if (destination == NULL || data == NULL) {
        return false;
    }
    memcpy(destination, data, length);
    return true;
}

bool wchlink_test_target_load(const struct wchlink_target_ports *target,
                              uint32_t address, uint8_t *data,
                              size_t length) {
    const uint8_t *source =
        wchlink_test_target_const_memory(target, address, length);

    if (source == NULL || data == NULL) {
        return false;
    }
    memcpy(data, source, length);
    return true;
}

bool wchlink_test_target_last_execute(
    const struct wchlink_target_ports *target,
    struct wchlink_test_execute *execute) {
    if (!target->execute_seen || execute == NULL) {
        return false;
    }
    *execute = target->last_execute;
    return true;
}

void wchlink_target_ports_init(struct wchlink_target_ports *target) {
    uint8_t family_hint = target->family_hint;

    // transport 重建只清除连接快照，目标内存和预设连接身份保持不变
    target->info = (struct rvswd_target_info){0};
    target->family_hint = family_hint;
    target->execute_seen = false;
}

void wchlink_target_ports_disconnect(struct wchlink_target_ports *target) {
    target->info.connected = false;
}

struct rvswd_target_result wchlink_target_ports_connect(
    struct wchlink_target_ports *target) {
    struct rvswd_target_result result;

    if (wchlink_test_target_take_failure(target, WCHLINK_TEST_TARGET_CONNECT,
                                         &result)) {
        target->info.connected = false;
        return result;
    }
    target->info = target->connect_info;
    target->info.connected = true;
    return rvswd_target_result_success();
}

void wchlink_target_ports_set_family_hint(
    struct wchlink_target_ports *target, uint8_t family) {
    target->family_hint = family;
    if (!target->info.connected) {
        target->info.family = family;
    }
}

struct rvswd_target_info wchlink_target_ports_info(
    const struct wchlink_target_ports *target) {
    return target->info;
}

struct rvswd_target_result wchlink_target_ports_read_dmi(
    struct wchlink_target_ports *target, uint8_t address) {
    struct rvswd_target_result result;

    if (wchlink_test_target_take_failure(
            target, WCHLINK_TEST_TARGET_READ_DMI, &result)) {
        return result;
    }
    return wchlink_test_target_success(target->dmi[address]);
}

struct rvswd_target_result wchlink_target_ports_write_dmi(
    struct wchlink_target_ports *target, uint8_t address, uint32_t value) {
    struct rvswd_target_result result;

    if (wchlink_test_target_take_failure(
            target, WCHLINK_TEST_TARGET_WRITE_DMI, &result)) {
        return result;
    }
    target->dmi[address] = value;
    return rvswd_target_result_success();
}

struct rvswd_target_result wchlink_target_ports_read_memory32(
    struct wchlink_target_ports *target, uint32_t address) {
    const uint8_t *data;
    struct rvswd_target_result result;
    uint32_t value;

    if (wchlink_test_target_take_failure(
            target, WCHLINK_TEST_TARGET_READ_MEMORY, &result)) {
        return result;
    }
    data = wchlink_test_target_const_memory(target, address, 4u);
    if (data == NULL) {
        return wchlink_test_target_memory_failure(address);
    }
    value = (uint32_t)data[0] | ((uint32_t)data[1] << 8u) |
            ((uint32_t)data[2] << 16u) | ((uint32_t)data[3] << 24u);
    return wchlink_test_target_success(value);
}

struct rvswd_target_result wchlink_target_ports_write_memory32(
    struct wchlink_target_ports *target, uint32_t address, uint32_t value) {
    uint8_t data[] = {
        (uint8_t)value,
        (uint8_t)(value >> 8u),
        (uint8_t)(value >> 16u),
        (uint8_t)(value >> 24u),
    };
    struct rvswd_target_result result;

    if (wchlink_test_target_take_failure(
            target, WCHLINK_TEST_TARGET_WRITE_MEMORY, &result)) {
        return result;
    }
    if (!wchlink_test_target_store(target, address, data, sizeof(data))) {
        return wchlink_test_target_memory_failure(address);
    }
    return rvswd_target_result_success();
}

struct rvswd_target_result wchlink_target_ports_write_memory(
    struct wchlink_target_ports *target, uint32_t address,
    const uint8_t *data, uint32_t length) {
    struct rvswd_target_result result;

    if (wchlink_test_target_take_failure(
            target, WCHLINK_TEST_TARGET_WRITE_MEMORY, &result)) {
        return result;
    }
    if (!wchlink_test_target_store(target, address, data, length)) {
        return wchlink_test_target_memory_failure(address);
    }
    return rvswd_target_result_success();
}

struct rvswd_target_result wchlink_target_ports_execute(
    struct wchlink_target_ports *target, uint32_t entry, uint32_t stack_top,
    uint32_t mode, uint32_t address, uint32_t length,
    uint32_t data_address) {
    struct rvswd_target_result result;

    if (wchlink_test_target_take_failure(
            target, WCHLINK_TEST_TARGET_EXECUTE, &result)) {
        return result;
    }
    // 记录 target 可观察到的 loader 调用参数，不暴露 transfer 内部状态
    target->last_execute = (struct wchlink_test_execute){
        .entry = entry,
        .stack_top = stack_top,
        .mode = mode,
        .address = address,
        .length = length,
        .data_address = data_address,
    };
    target->execute_seen = true;
    return wchlink_test_target_success(target->execute_value);
}

static struct rvswd_target_result wchlink_test_target_simple_operation(
    struct wchlink_target_ports *target,
    enum wchlink_test_target_operation operation) {
    struct rvswd_target_result result;

    if (wchlink_test_target_take_failure(target, operation, &result)) {
        return result;
    }
    return rvswd_target_result_success();
}

struct rvswd_target_result wchlink_target_ports_reset_and_halt(
    struct wchlink_target_ports *target) {
    return wchlink_test_target_simple_operation(
        target, WCHLINK_TEST_TARGET_RESET_AND_HALT);
}

struct rvswd_target_result wchlink_target_ports_soft_reset_and_run(
    struct wchlink_target_ports *target) {
    return wchlink_test_target_simple_operation(
        target, WCHLINK_TEST_TARGET_SOFT_RESET_AND_RUN);
}

struct rvswd_target_result wchlink_target_ports_reset_and_run(
    struct wchlink_target_ports *target) {
    return wchlink_test_target_simple_operation(
        target, WCHLINK_TEST_TARGET_RESET_AND_RUN);
}

struct rvswd_target_result wchlink_target_ports_flash_erase_all(
    struct wchlink_target_ports *target) {
    struct rvswd_target_result result;

    if (wchlink_test_target_take_failure(
            target, WCHLINK_TEST_TARGET_FLASH_ERASE, &result)) {
        return result;
    }
    memset(target->flash, 0xff, sizeof(target->flash));
    return rvswd_target_result_success();
}

struct rvswd_target_result wchlink_target_ports_flash_rewrite_page(
    struct wchlink_target_ports *target, uint32_t address,
    const uint8_t *data) {
    struct rvswd_target_result result;

    if (wchlink_test_target_take_failure(
            target, WCHLINK_TEST_TARGET_FLASH_REWRITE, &result)) {
        return result;
    }
    if (!wchlink_test_target_store(target, address, data,
                                   wchlink_test_flash_page_size)) {
        return wchlink_test_target_memory_failure(address);
    }
    return rvswd_target_result_success();
}

struct rvswd_target_result wchlink_target_ports_flash_read_protected(
    struct wchlink_target_ports *target) {
    struct rvswd_target_result result;

    if (wchlink_test_target_take_failure(
            target, WCHLINK_TEST_TARGET_FLASH_READ_PROTECTION, &result)) {
        return result;
    }
    return wchlink_test_target_success(target->read_protected ? 1u : 0u);
}

struct rvswd_target_result wchlink_target_ports_flash_write_protected(
    struct wchlink_target_ports *target) {
    struct rvswd_target_result result;

    if (wchlink_test_target_take_failure(
            target, WCHLINK_TEST_TARGET_FLASH_WRITE_PROTECTION, &result)) {
        return result;
    }
    return wchlink_test_target_success(target->read_protected ? 1u : 0u);
}

struct rvswd_target_result wchlink_target_ports_flash_set_read_protected(
    struct wchlink_target_ports *target, bool protected) {
    struct rvswd_target_result result;

    if (wchlink_test_target_take_failure(
            target, WCHLINK_TEST_TARGET_FLASH_SET_PROTECTION, &result)) {
        return result;
    }
    target->read_protected = protected;
    return rvswd_target_result_success();
}
