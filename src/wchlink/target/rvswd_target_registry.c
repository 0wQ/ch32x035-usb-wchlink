#include "wchlink/target/rvswd_target_registry.h"

#include "wchlink/protocol/wchlink_family.h"
#include "wchlink/target/rvswd_target_ch58x.h"
#include "wchlink/target/rvswd_target_ch59x.h"
#include "wchlink/target/rvswd_target_l103.h"
#include "wchlink/target/rvswd_target_v20x.h"
#include "wchlink/target/rvswd_target_v30x.h"
#include "wchlink/target/rvswd_target_x03x.h"

#include <stddef.h>

enum { RVSWD_TARGET_REGISTRY_MODULE_COUNT = 6u };

// 按真实 ChipID 调用各族 matcher，未知目标不回退到其他 profile
const struct rvswd_target_module *rvswd_target_registry_module_from_chip_id(
    uint32_t chip_id) {
    for (size_t index = 0u; index < rvswd_target_registry_module_count();
         ++index) {
        const struct rvswd_target_module *module =
            rvswd_target_registry_module_at(index);
        if (module->matches_chip_id != NULL &&
            module->matches_chip_id(chip_id)) {
            return module;
        }
    }
    return NULL;
}

// 按 WCH-LinkE family 字段返回候选目标模块
const struct rvswd_target_module *rvswd_target_registry_module_from_family(
    uint8_t family) {
    for (size_t index = 0u; index < rvswd_target_registry_module_count();
         ++index) {
        const struct rvswd_target_module *module =
            rvswd_target_registry_module_at(index);
        if (module->family == family) {
            return module;
        }
    }
    return NULL;
}

// 返回当前固件注册的目标族数量，供自动探测遍历
size_t rvswd_target_registry_module_count(void) {
    return RVSWD_TARGET_REGISTRY_MODULE_COUNT;
}

// 按稳定索引返回目标模块，越界索引返回空指针
const struct rvswd_target_module *rvswd_target_registry_module_at(
    size_t index) {
    switch (index) {
        case 0u:
            return rvswd_target_x03x_module();
        case 1u:
            return rvswd_target_l103_module();
        case 2u:
            return rvswd_target_v20x_module();
        case 3u:
            return rvswd_target_v30x_module();
        case 4u:
            return rvswd_target_ch58x_module();
        case 5u:
            return rvswd_target_ch59x_module();
        default:
            return NULL;
    }
}
