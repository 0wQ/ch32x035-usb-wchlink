#include "wchlink/target/rvswd_target_registry.h"

#include "wchlink/protocol/wchlink_family.h"
#include "wchlink/target/rvswd_target_x03x.h"

#include <stddef.h>

const struct rvswd_target_module *rvswd_target_registry_module_from_chip_id(
    uint32_t chip_id) {
    if (rvswd_target_x03x_matches_chip_id(chip_id)) {
        return rvswd_target_x03x_module();
    }
    return NULL;
}

const struct rvswd_target_module *rvswd_target_registry_module_from_family(
    uint8_t family) {
    if (family == WCHLINK_TARGET_FAMILY_X03X) {
        return rvswd_target_x03x_module();
    }
    return NULL;
}
