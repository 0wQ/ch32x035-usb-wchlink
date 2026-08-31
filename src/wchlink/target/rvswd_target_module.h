#pragma once

#include "wchlink/rvswd/rvswd_operation.h"
#include "wchlink/rvswd/rvswd_memory.h"
#include "wchlink/rvswd/rvswd_types.h"
#include "wchlink/target/rvswd_target_result.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct rvswd_target_profile;
struct rvswd_target_probe_ops;
struct rvswd_target_loader_ops;
struct rvswd_target_flash_ops;
struct rvswd_target_control_ops;

typedef bool (*rvswd_target_matches_chip_id_fn)(uint32_t chip_id);

typedef bool (*rvswd_target_probe_chip_id_fn)(
    struct rvswd_operation *operation, uint32_t *chip_id);

struct rvswd_target_probe_ops {
    rvswd_target_probe_chip_id_fn read_chip_id;
};

typedef bool (*rvswd_target_loader_prepare_fn)(
    struct rvswd_operation *operation,
    const struct rvswd_target_profile *profile, uint32_t mode);
typedef bool (*rvswd_target_loader_execute_fn)(
    struct rvswd_operation *operation, uint32_t entry, uint32_t stack_top,
    uint32_t mode, uint32_t address, uint32_t length, uint32_t data_address,
    uint32_t dpc_value, uint32_t *result);

struct rvswd_target_loader_ops {
    rvswd_target_loader_prepare_fn prepare;
    rvswd_target_loader_execute_fn execute;
};

typedef bool (*rvswd_target_flash_erase_all_fn)(
    struct rvswd_operation *operation,
    const struct rvswd_target_profile *profile);
typedef bool (*rvswd_target_flash_rewrite_page_fn)(
    struct rvswd_operation *operation,
    const struct rvswd_target_profile *profile, uint32_t address,
    const uint8_t *data);
typedef bool (*rvswd_target_flash_read_protected_fn)(
    struct rvswd_operation *operation,
    const struct rvswd_target_profile *profile, bool *protected);
typedef bool (*rvswd_target_flash_write_protected_fn)(
    struct rvswd_operation *operation,
    const struct rvswd_target_profile *profile, bool *protected);
typedef bool (*rvswd_target_flash_set_read_protected_fn)(
    struct rvswd_operation *operation,
    const struct rvswd_target_profile *profile, bool protected);
typedef bool (*rvswd_target_flash_set_option_bytes_fn)(
    struct rvswd_operation *operation,
    const struct rvswd_target_profile *profile, const uint8_t *values,
    size_t count);
typedef bool (*rvswd_target_flash_read_memory_type_fn)(
    struct rvswd_operation *operation,
    const struct rvswd_target_profile *profile, bool extended,
    uint8_t *memory_type);
typedef bool (*rvswd_target_flash_set_memory_type_fn)(
    struct rvswd_operation *operation,
    const struct rvswd_target_profile *profile, bool extended,
    uint8_t memory_type);

struct rvswd_target_flash_ops {
    rvswd_target_flash_erase_all_fn erase_all;
    rvswd_target_flash_rewrite_page_fn rewrite_page;
    rvswd_target_flash_read_protected_fn read_protected;
    rvswd_target_flash_write_protected_fn write_protected;
    rvswd_target_flash_set_read_protected_fn set_read_protected;
    rvswd_target_flash_set_option_bytes_fn set_option_bytes;
    rvswd_target_flash_read_memory_type_fn read_memory_type;
    rvswd_target_flash_set_memory_type_fn set_memory_type;
};

typedef bool (*rvswd_target_control_fn)(struct rvswd_operation *operation);

struct rvswd_target_control_ops {
    rvswd_target_control_fn reset_and_halt;
    rvswd_target_control_fn soft_reset_and_run;
    rvswd_target_control_fn reset_and_run;
};

struct rvswd_target_capabilities {
    enum rvswd_packet_mode packet_mode;
    enum rvswd_target_chip_info_layout chip_info_layout;
    bool memory_streaming;
};

// 族模块集中目标行为，registry 只负责按 family 和 ChipID 选择模块
struct rvswd_target_module {
    uint8_t family;
    rvswd_target_matches_chip_id_fn matches_chip_id;
    const struct rvswd_target_profile *profile;
    const struct rvswd_target_capabilities *capabilities;
    const struct rvswd_target_probe_ops *probe;
    const struct rvswd_memory_ops *memory;
    const struct rvswd_target_loader_ops *loader;
    const struct rvswd_target_flash_ops *flash;
    const struct rvswd_target_control_ops *control;
};
