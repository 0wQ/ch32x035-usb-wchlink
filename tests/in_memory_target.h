#pragma once

#include "wchlink/target/wchlink_target_control.h"
#include "wchlink/target/wchlink_target_dmi.h"
#include "wchlink/target/wchlink_target_flash.h"
#include "wchlink/target/wchlink_target_transfer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum wchlink_test_target_operation {
    WCHLINK_TEST_TARGET_CONNECT,
    WCHLINK_TEST_TARGET_READ_DMI,
    WCHLINK_TEST_TARGET_WRITE_DMI,
    WCHLINK_TEST_TARGET_RESUME_DMI,
    WCHLINK_TEST_TARGET_READ_MEMORY,
    WCHLINK_TEST_TARGET_WRITE_MEMORY,
    WCHLINK_TEST_TARGET_EXECUTE,
    WCHLINK_TEST_TARGET_RESET_AND_HALT,
    WCHLINK_TEST_TARGET_SOFT_RESET_AND_RUN,
    WCHLINK_TEST_TARGET_RESET_AND_RUN,
    WCHLINK_TEST_TARGET_FLASH_ERASE,
    WCHLINK_TEST_TARGET_FLASH_REWRITE,
    WCHLINK_TEST_TARGET_FLASH_READ_PROTECTION,
    WCHLINK_TEST_TARGET_FLASH_WRITE_PROTECTION,
    WCHLINK_TEST_TARGET_FLASH_SET_PROTECTION,
    WCHLINK_TEST_TARGET_FLASH_SET_OPTION_BYTES,
    WCHLINK_TEST_TARGET_FLASH_READ_MEMORY_TYPE,
    WCHLINK_TEST_TARGET_FLASH_SET_MEMORY_TYPE,
};

struct wchlink_test_execute {
    uint32_t entry;
    uint32_t stack_top;
    uint32_t mode;
    uint32_t address;
    uint32_t length;
    uint32_t data_address;
};

enum {
    WCHLINK_TEST_FLASH_SIZE = 8192u,
    WCHLINK_TEST_RAM_SIZE = 32768u,
    WCHLINK_TEST_SYSTEM_SIZE = 4096u,
};

// 内存 adapter 实现与固件相同的 target port seam，不模拟 GPIO 和 RVSWD 时序
// 该结构只为主机 fixture 提供存储，测试通过下方 helper 观察目标侧结果
struct wchlink_target_ports {
    struct rvswd_target_info info;
    struct rvswd_target_info connect_info;
    uint32_t dmi[256];
    uint8_t flash[WCHLINK_TEST_FLASH_SIZE];
    uint8_t ram[WCHLINK_TEST_RAM_SIZE];
    uint8_t system[WCHLINK_TEST_SYSTEM_SIZE];
    uint8_t option_bytes[RVSWD_OPTION_CONFIG_BYTE_COUNT];
    uint8_t family_hint;
    bool read_protected;
    uint32_t execute_value;
    struct wchlink_test_execute last_execute;
    bool execute_seen;
    enum wchlink_test_target_operation failure_operation;
    struct rvswd_target_result failure_result;
    uint32_t operation_count[WCHLINK_TEST_TARGET_FLASH_SET_OPTION_BYTES + 1u];
    bool failure_armed;
};

void wchlink_test_target_reset(struct wchlink_target_ports *target,
                               struct rvswd_target_info info,
                               bool connected);
void wchlink_test_target_fail_next(
    struct wchlink_target_ports *target,
    enum wchlink_test_target_operation operation,
    struct rvswd_target_result result);
void wchlink_test_target_set_execute_value(
    struct wchlink_target_ports *target, uint32_t value);
void wchlink_test_target_set_dmi(struct wchlink_target_ports *target,
                                 uint8_t address, uint32_t value);
bool wchlink_test_target_store(struct wchlink_target_ports *target,
                               uint32_t address, const uint8_t *data,
                               size_t length);
bool wchlink_test_target_load(const struct wchlink_target_ports *target,
                              uint32_t address, uint8_t *data,
                              size_t length);
bool wchlink_test_target_last_execute(
    const struct wchlink_target_ports *target,
    struct wchlink_test_execute *execute);
bool wchlink_test_target_has_family_hint(
    const struct wchlink_target_ports *target, uint8_t family);
uint32_t wchlink_test_target_operation_count(
    const struct wchlink_target_ports *target,
    enum wchlink_test_target_operation operation);
