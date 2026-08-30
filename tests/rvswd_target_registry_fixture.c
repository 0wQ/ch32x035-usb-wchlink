#include "wchlink/rvswd/rvswd_debug.h"
#include "wchlink/rvswd/rvswd_memory.h"
#include "wchlink/target/rvswd_target_registry.h"
#include "wchlink/target/rvswd_target_x03x.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

bool rvswd_memory_read32(struct rvswd_operation *operation,
                         const struct rvswd_target_profile *profile,
                         bool target_identified, uint32_t address,
                         uint32_t *value) {
    (void)operation;
    (void)profile;
    (void)target_identified;
    (void)address;
    (void)value;
    return true;
}

bool rvswd_memory_write32(struct rvswd_operation *operation, uint32_t address,
                          uint32_t value) {
    (void)operation;
    (void)address;
    (void)value;
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

struct rvswd_transport_result rvswd_operation_write_dmi(
    struct rvswd_operation *operation, uint8_t address, uint32_t value) {
    (void)operation;
    (void)address;
    (void)value;
    return (struct rvswd_transport_result){.ok = true};
}

int main(void) {
    const struct rvswd_target_module *module;
    const struct rvswd_target_profile *profile;

    module = rvswd_target_registry_module_from_chip_id(0x03510611u);
    assert(module != NULL);
    assert(module == rvswd_target_x03x_module());
    assert(module->profile == rvswd_target_x03x_profile());
    assert(module->loader_prepare != NULL);
    assert(module->loader_execute != NULL);
    assert(rvswd_target_registry_module_from_family(
               WCHLINK_TARGET_FAMILY_CH32V30X) == NULL);

    profile = module->profile;
    assert(profile == module->profile);
    return 0;
}
