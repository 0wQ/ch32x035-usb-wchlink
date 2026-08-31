#include "wchlink/flash/rvswd_flash_ch58x_59x.h"
#include "wchlink/protocol/wchlink_family.h"
#include "wchlink/rvswd/rvswd_debug.h"
#include "wchlink/rvswd/rvswd_memory.h"
#include "wchlink/target/rvswd_target_ch58x.h"
#include "wchlink/target/rvswd_target_ch59x.h"
#include "wchlink/target/rvswd_target_l103.h"
#include "wchlink/target/rvswd_target_registry.h"
#include "wchlink/target/rvswd_target_v30x.h"
#include "wchlink/target/rvswd_target_x03x.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

void bsp_delay_us(uint32_t delay_us) {
    (void)delay_us;
}

static bool rvswd_test_memory_read32(struct rvswd_operation *operation,
                                     uint32_t address, uint32_t *value) {
    (void)operation;
    (void)address;
    (void)value;
    return true;
}

bool rvswd_memory_read32_synchronized(struct rvswd_operation *operation,
                                      uint32_t address, uint32_t *value) {
    return rvswd_test_memory_read32(operation, address, value);
}

bool rvswd_memory_read32_v30x(struct rvswd_operation *operation,
                              uint32_t address, uint32_t *value) {
    return rvswd_test_memory_read32(operation, address, value);
}

bool rvswd_memory_write32(struct rvswd_operation *operation, uint32_t address,
                           uint32_t value) {
    (void)operation;
    (void)address;
    (void)value;
    return true;
}

bool rvswd_memory_write32_slow(struct rvswd_operation *operation,
                               uint32_t address, uint32_t value) {
    return rvswd_memory_write32(operation, address, value);
}

bool rvswd_memory_write32_direct(struct rvswd_operation *operation,
                                 uint32_t address, uint32_t value) {
    return rvswd_memory_write32(operation, address, value);
}

bool rvswd_memory_write(struct rvswd_operation *operation,
                        const struct rvswd_target_profile *profile,
                        uint32_t address, const uint8_t *data,
                        uint32_t length) {
    (void)profile;
    (void)data;
    (void)length;
    return rvswd_memory_write32(operation, address, 0u);
}

bool rvswd_memory_write_direct(struct rvswd_operation *operation,
                               uint32_t address, const uint8_t *data,
                               uint32_t length) {
    (void)data;
    (void)length;
    return rvswd_memory_write32(operation, address, 0u);
}

bool rvswd_memory_write_slow(struct rvswd_operation *operation,
                             uint32_t address, const uint8_t *data,
                             uint32_t length) {
    return rvswd_memory_write_direct(operation, address, data, length);
}

bool rvswd_memory_write_streaming_retry(struct rvswd_operation *operation,
                                        uint32_t address,
                                        const uint8_t *data,
                                        uint32_t length) {
    return rvswd_memory_write_direct(operation, address, data, length);
}

const uint8_t ch58x_59x_flash_erase_stub_start[] = {0u};
const uint8_t ch58x_59x_flash_erase_stub_end[] = {1u};

bool rvswd_flash_erase_all(struct rvswd_operation *operation,
                           const struct rvswd_target_profile *profile) {
    (void)operation;
    (void)profile;
    return true;
}

bool rvswd_flash_ch32_erase_all(struct rvswd_operation *operation,
                                const struct rvswd_target_profile *profile) {
    return rvswd_flash_erase_all(operation, profile);
}

bool rvswd_flash_ch58x_59x_erase_all(
    struct rvswd_operation *operation,
    const struct rvswd_target_profile *profile, const uint8_t *stub_start,
    const uint8_t *stub_end, rvswd_flash_ch58x_59x_execute_fn execute) {
    (void)stub_start;
    (void)stub_end;
    (void)execute;
    return rvswd_flash_erase_all(operation, profile);
}

bool rvswd_flash_rewrite_page(struct rvswd_operation *operation,
                              const struct rvswd_target_profile *profile,
                              uint32_t address, const uint8_t *data) {
    (void)operation;
    (void)profile;
    (void)address;
    (void)data;
    return true;
}

bool rvswd_flash_read_protected(struct rvswd_operation *operation,
                                const struct rvswd_target_profile *profile,
                                bool *protected) {
    (void)operation;
    (void)profile;
    *protected = false;
    return true;
}

bool rvswd_flash_write_protected(struct rvswd_operation *operation,
                                 const struct rvswd_target_profile *profile,
                                 bool *protected) {
    return rvswd_flash_read_protected(operation, profile, protected);
}

bool rvswd_flash_set_read_protected(struct rvswd_operation *operation,
                                    const struct rvswd_target_profile *profile,
                                    bool protected) {
    (void)operation;
    (void)profile;
    (void)protected;
    return true;
}

bool rvswd_flash_set_option_bytes(struct rvswd_operation *operation,
                                  const struct rvswd_target_profile *profile,
                                  const uint8_t *values, size_t count) {
    (void)operation;
    (void)profile;
    (void)values;
    (void)count;
    return true;
}

bool rvswd_flash_read_memory_type(struct rvswd_operation *operation,
                                  const struct rvswd_target_profile *profile,
                                  bool extended, uint8_t *memory_type) {
    (void)operation;
    (void)profile;
    (void)extended;
    *memory_type = 0u;
    return true;
}

bool rvswd_flash_set_memory_type(struct rvswd_operation *operation,
                                 const struct rvswd_target_profile *profile,
                                 bool extended, uint8_t memory_type) {
    (void)operation;
    (void)profile;
    (void)extended;
    (void)memory_type;
    return true;
}

bool rvswd_reset_and_halt(struct rvswd_operation *operation) {
    (void)operation;
    return true;
}

bool rvswd_soft_reset_and_run(struct rvswd_operation *operation) {
    (void)operation;
    return true;
}

bool rvswd_reset_and_run(struct rvswd_operation *operation) {
    (void)operation;
    return true;
}

bool rvswd_debug_write_register(struct rvswd_operation *operation,
                                uint16_t regno, uint32_t value) {
    (void)operation;
    (void)regno;
    (void)value;
    return true;
}

bool rvswd_debug_write_raw_gpr(struct rvswd_operation *operation,
                               uint8_t regno, uint32_t value) {
    (void)operation;
    (void)regno;
    (void)value;
    return true;
}

bool rvswd_debug_read_raw_gpr(struct rvswd_operation *operation, uint8_t regno,
                              uint32_t *value) {
    (void)operation;
    (void)regno;
    *value = 0u;
    return true;
}

bool rvswd_debug_wait_dmstatus(struct rvswd_operation *operation,
                               uint32_t mask, bool set, uint32_t timeout_ms) {
    (void)operation;
    (void)mask;
    (void)set;
    (void)timeout_ms;
    return true;
}

bool rvswd_debug_wait_abstract_idle(struct rvswd_operation *operation,
                                    uint32_t *abstractcs) {
    (void)operation;
    *abstractcs = 0u;
    return true;
}

struct rvswd_transport_result rvswd_operation_read_dmi(
    struct rvswd_operation *operation, uint8_t address) {
    (void)operation;
    (void)address;
    return (struct rvswd_transport_result){.ok = true};
}

struct rvswd_transport_result rvswd_operation_write_dmi(
    struct rvswd_operation *operation, uint8_t address, uint32_t value) {
    (void)operation;
    (void)address;
    (void)value;
    return (struct rvswd_transport_result){.ok = true};
}

static void assert_module_operations(
    const struct rvswd_target_module *module) {
    assert(module != NULL);
    assert(module->capabilities != NULL);
    assert(module->probe != NULL);
    assert(module->probe->read_chip_id != NULL);
    assert(module->memory != NULL);
    assert(module->memory->read32 != NULL);
    assert(module->memory->write32 != NULL);
    assert(module->memory->write != NULL);
    assert(module->loader != NULL);
    assert(module->loader->execute != NULL);
    assert(module->flash != NULL);
    assert(module->flash->erase_all != NULL);
    assert(module->control != NULL);
    assert(module->control->reset_and_halt != NULL);
    assert(module->control->soft_reset_and_run != NULL);
    assert(module->control->reset_and_run != NULL);
}

int main(void) {
    const struct rvswd_target_module *module;
    const struct rvswd_target_profile *profile;

    module = rvswd_target_registry_module_from_chip_id(0x03510611u);
    assert(module != NULL);
    assert(module == rvswd_target_x03x_module());
    assert(module->profile == rvswd_target_x03x_profile());
    assert(module->loader != NULL);
    assert(module->loader->prepare != NULL);
    assert(module->loader->execute != NULL);
    assert_module_operations(module);
    assert(rvswd_target_registry_module_from_family(
               WCHLINK_TARGET_FAMILY_X03X) == rvswd_target_x03x_module());
    assert(rvswd_target_registry_module_from_family(
               WCHLINK_TARGET_FAMILY_CH32L10X) == rvswd_target_l103_module());
    assert(rvswd_target_registry_module_from_family(
               WCHLINK_TARGET_FAMILY_CH32V30X) == rvswd_target_v30x_module());
    assert(rvswd_target_registry_module_from_family(
               WCHLINK_TARGET_FAMILY_CH58X) == rvswd_target_ch58x_module());
    assert(rvswd_target_registry_module_from_family(
               WCHLINK_TARGET_FAMILY_CH59X) == rvswd_target_ch59x_module());
    assert(rvswd_target_registry_module_from_chip_id(0x10320710u) ==
           rvswd_target_l103_module());
    assert(rvswd_target_registry_module_from_chip_id(0x30700528u) ==
           rvswd_target_v30x_module());
    assert(rvswd_target_registry_module_from_chip_id(0x82000000u) ==
           rvswd_target_ch58x_module());
    assert(rvswd_target_registry_module_from_chip_id(0x92000000u) ==
           rvswd_target_ch59x_module());
    assert(rvswd_target_registry_module_from_family(0x00u) == NULL);

    for (size_t index = 0u;
         index < rvswd_target_registry_module_count(); ++index) {
        assert_module_operations(rvswd_target_registry_module_at(index));
    }

    profile = module->profile;
    assert(profile == module->profile);
    return 0;
}
