#include "wchlink/flash/rvswd_flash_ch58x_59x.h"
#include "wchlink/protocol/wchlink_family.h"
#include "wchlink/rvswd/rvswd_debug.h"
#include "wchlink/rvswd/rvswd_memory.h"
#include "wchlink/target/rvswd_target_ch58x.h"
#include "wchlink/target/rvswd_target_ch59x.h"
#include "wchlink/target/rvswd_target_l103.h"
#include "wchlink/target/rvswd_target_registry.h"
#include "wchlink/target/rvswd_target_v20x.h"
#include "wchlink/target/rvswd_target_v30x.h"
#include "wchlink/target/rvswd_target_x03x.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

static uint32_t rvswd_test_memory_chip_id;
static uint64_t rvswd_test_time_us;
static uint8_t rvswd_test_memory_type_encoded;
static uint8_t rvswd_test_v20x_status_reads;
static uint8_t rvswd_test_v20x_reset_count;
static bool rvswd_test_v20x_reset_success;
static size_t rvswd_test_write_count;
static struct {
    uint32_t address;
    uint32_t value;
} rvswd_test_writes[8];
static size_t rvswd_test_dmi_write_count;
static struct {
    uint8_t address;
    uint32_t value;
} rvswd_test_dmi_writes[6];

void bsp_delay_us(uint32_t delay_us) {
    rvswd_test_time_us += delay_us;
}

uint64_t bsp_time_us(void) {
    return rvswd_test_time_us;
}

static bool rvswd_test_memory_read32(struct rvswd_operation *operation,
                                     uint32_t address, uint32_t *value) {
    (void)operation;
    if (value != NULL && address == 0x1ffff704u) {
        *value = rvswd_test_memory_chip_id;
    } else if (value != NULL && address == 0x1ffff7e0u) {
        *value = 0xffff0040u;
    } else if (value != NULL && address == 0x1ffff7e8u) {
        *value = 0x490dabcd;
    } else if (value != NULL && address == 0x1ffff7ecu) {
        *value = 0xb359be7fu;
    } else if (value != NULL && address == 0x1ffff7f0u) {
        *value = 0xe339e339u;
    } else if (value != NULL && address == 0x1ffff800u) {
        *value = 0xc03f5aa5u;
    } else if (value != NULL && address == 0x4002201cu) {
        *value = 0u;
    } else if (value != NULL && address == 0x40022010u) {
        *value = rvswd_test_write_count < 5u ? 0u : 4u;
    } else if (value != NULL && address == 0x4002200cu) {
        *value = rvswd_test_v20x_status_reads++ == 0u ? 1u : 0x20u;
    }
    return true;
}

bool rvswd_memory_read32_synchronized(struct rvswd_operation *operation,
                                      uint32_t address, uint32_t *value) {
    return rvswd_test_memory_read32(operation, address, value);
}

bool rvswd_memory_read32_access_memory(struct rvswd_operation *operation,
                                       uint32_t address, uint32_t *value) {
    return rvswd_test_memory_read32(operation, address, value);
}

bool rvswd_memory_read32(struct rvswd_operation *operation,
                         const struct rvswd_target_profile *profile,
                         bool target_identified, uint32_t address,
                         uint32_t *value) {
    (void)profile;
    (void)target_identified;
    return rvswd_test_memory_read32(operation, address, value);
}

bool rvswd_memory_write32(struct rvswd_operation *operation, uint32_t address,
                          uint32_t value) {
    (void)operation;
    assert(rvswd_test_write_count < sizeof(rvswd_test_writes) /
                                       sizeof(rvswd_test_writes[0]));
    rvswd_test_writes[rvswd_test_write_count].address = address;
    rvswd_test_writes[rvswd_test_write_count].value = value;
    rvswd_test_write_count++;
    return true;
}

bool rvswd_memory_write32_slow(struct rvswd_operation *operation,
                               uint32_t address, uint32_t value) {
    return rvswd_memory_write32(operation, address, value);
}

// fixture 不模拟 Program Buffer，强制 CH32V20X 连续写入回退到已验证的 Access Memory 路径
bool rvswd_memory_write_streaming(struct rvswd_operation *operation,
                                  uint32_t address, const uint8_t *data,
                                  uint32_t length) {
    (void)operation;
    (void)address;
    (void)data;
    (void)length;
    return false;
}

void rvswd_operation_cleanup_write_dmi(struct rvswd_operation *operation,
                                       uint8_t address, uint32_t value) {
    (void)operation;
    (void)address;
    (void)value;
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
    *memory_type = rvswd_test_memory_type_encoded;
    return true;
}

bool rvswd_flash_set_memory_type(struct rvswd_operation *operation,
                                 const struct rvswd_target_profile *profile,
                                 bool extended, uint8_t memory_type) {
    (void)operation;
    (void)profile;
    (void)extended;
    rvswd_test_memory_type_encoded = memory_type;
    return true;
}

bool rvswd_reset_and_halt(struct rvswd_operation *operation) {
    (void)operation;
    rvswd_test_v20x_reset_count++;
    return rvswd_test_v20x_reset_success;
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
    assert(rvswd_test_dmi_write_count <
           sizeof(rvswd_test_dmi_writes) / sizeof(rvswd_test_dmi_writes[0]));
    rvswd_test_dmi_writes[rvswd_test_dmi_write_count].address = address;
    rvswd_test_dmi_writes[rvswd_test_dmi_write_count].value = value;
    rvswd_test_dmi_write_count++;
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

// CH32V203 fixture 锁定身份、ESIG、loader contract 和已确认的 ROM/RAM 配置能力
static void assert_v20x_identity_and_chip_info(
    const struct rvswd_target_module *module) {
    assert(module != NULL);
    assert(module->capabilities != NULL);
    assert(module->capabilities->packet_mode == RVSWD_PACKET_SHORT);
    assert(module->capabilities->chip_info_layout ==
           RVSWD_TARGET_CHIP_INFO_ESIG);
    assert(module->capabilities->memory_streaming);
    assert(module->probe != NULL);
    assert(module->probe->read_chip_id != NULL);
    assert(module->profile != NULL);
    assert(module->profile->identity != NULL);
    assert(module->profile->identity->esig_flash_size_address == 0x1ffff7e0u);
    assert(module->profile->identity->esig_uid_low_address == 0x1ffff7e8u);
    assert(module->profile->identity->esig_uid_high_address == 0x1ffff7ecu);
    assert(module->profile->identity->esig_uid_tail_address == 0x1ffff7f0u);
    assert(module->memory != NULL);
    assert(module->memory->read32 != NULL);
    assert(module->memory->write32 != NULL);
    assert(module->memory->write != NULL);
    assert(module->profile->loader != NULL);
    assert(module->profile->loader->code_address == 0x20000000u);
    assert(module->profile->loader->data_address == 0x20001000u);
    assert(module->profile->loader->stack_top == 0x20002800u);
    assert(module->profile->loader->checksum_address == 0x20002010u);
    assert(module->profile->loader->initialize_mode == 0x01u);
    assert(module->profile->loader->program_verify_mode == 0x1cu);
    assert(!module->profile->loader->repeat_initialize);
    assert(module->loader != NULL);
    assert(module->loader->prepare != NULL);
    assert(module->loader->execute != NULL);
    assert(module->flash != NULL);
    assert(module->flash->erase_all != NULL);
    assert(module->flash->read_protected != NULL);
    assert(module->flash->write_protected != NULL);
    assert(module->flash->set_read_protected != NULL);
    assert(module->flash->set_option_bytes == NULL);
    assert(module->flash->read_memory_type != NULL);
    assert(module->flash->set_memory_type != NULL);
    assert(module->control != NULL);
    assert(module->control->reset_and_halt != NULL);
    assert(module->control->soft_reset_and_run != NULL);
    assert(module->control->reset_and_run != NULL);
}

// CH32V203 单字 Access Memory 写不插入 ABSTRACTCS 事务
static void assert_v20x_memory_write_sequence(
    const struct rvswd_target_module *module) {
    static const uint8_t expected_addresses[] = {
        RVSWD_DMI_DATA1, RVSWD_DMI_DATA0, RVSWD_DMI_COMMAND,
    };
    static const uint32_t expected_values[] = {
        0x20001000u, 0x12345678u, 0x02210000u,
    };
    struct rvswd_operation operation = {0};

    rvswd_test_dmi_write_count = 0u;
    assert(module->memory->write32(&operation, 0x20001000u, 0x12345678u));
    assert(rvswd_test_dmi_write_count == sizeof(expected_addresses) /
                                             sizeof(expected_addresses[0]));
    for (size_t index = 0u; index < rvswd_test_dmi_write_count; ++index) {
        assert(rvswd_test_dmi_writes[index].address == expected_addresses[index]);
        assert(rvswd_test_dmi_writes[index].value == expected_values[index]);
    }
}

// fixture 不实现 Program Buffer，连续写入失败后必须清理状态并完整回退到三笔 Access Memory 写
static void assert_v20x_memory_streaming_fallback(
    const struct rvswd_target_module *module) {
    static const uint8_t data[] = {0x78u, 0x56u, 0x34u, 0x12u};
    static const uint8_t expected_addresses[] = {
        RVSWD_DMI_DATA1, RVSWD_DMI_DATA0, RVSWD_DMI_COMMAND,
    };
    static const uint32_t expected_values[] = {
        0x20001000u, 0x12345678u, 0x02210000u,
    };
    struct rvswd_operation operation = {0};

    rvswd_test_dmi_write_count = 0u;
    assert(module->memory->write(&operation, 0x20001000u, data, sizeof(data)));
    assert(rvswd_test_dmi_write_count == sizeof(expected_addresses) /
                                             sizeof(expected_addresses[0]));
    for (size_t index = 0u; index < rvswd_test_dmi_write_count; ++index) {
        assert(rvswd_test_dmi_writes[index].address == expected_addresses[index]);
        assert(rvswd_test_dmi_writes[index].value == expected_values[index]);
    }
}

// CH32V203 全擦不在启动前等待 STATR.BSY，完成后才等待该位清零
static void assert_v20x_erase_sequence(
    const struct rvswd_target_module *module) {
    static const uint32_t expected_addresses[] = {
        0x40022004u, 0x40022004u, 0x40022024u, 0x40022024u,
        0x40022010u, 0x40022010u, 0x40022010u,
    };
    static const uint32_t expected_values[] = {
        0x45670123u, 0xcdef89abu, 0x45670123u, 0xcdef89abu,
        0x00000004u, 0x00000044u, 0x00000000u,
    };
    struct rvswd_operation operation = {.memory = module->memory};

    rvswd_test_time_us = 0u;
    rvswd_test_v20x_status_reads = 0u;
    rvswd_test_v20x_reset_count = 0u;
    rvswd_test_v20x_reset_success = true;
    rvswd_test_write_count = 0u;
    assert(module->flash->erase_all(&operation, module->profile));
    assert(rvswd_test_v20x_status_reads == 2u);
    assert(rvswd_test_v20x_reset_count == 1u);
    assert(rvswd_test_write_count == sizeof(expected_addresses) /
                                         sizeof(expected_addresses[0]));
    for (size_t index = 0u; index < rvswd_test_write_count; ++index) {
        assert(rvswd_test_writes[index].address == expected_addresses[index]);
        assert(rvswd_test_writes[index].value == expected_values[index]);
    }

    rvswd_test_v20x_status_reads = 0u;
    rvswd_test_v20x_reset_count = 0u;
    rvswd_test_v20x_reset_success = false;
    rvswd_test_write_count = 0u;
    assert(!module->flash->erase_all(&operation, module->profile));
    assert(operation.flash_code == 0x1fu);
    assert(rvswd_test_v20x_reset_count == 1u);
}

int main(void) {
    const struct rvswd_target_module *module;
    const struct rvswd_target_profile *profile;
    struct rvswd_operation operation = {0};
    uint32_t chip_id = 0u;

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
               WCHLINK_TARGET_FAMILY_CH32V20X) == rvswd_target_v20x_module());
    assert(rvswd_target_registry_module_from_family(
               WCHLINK_TARGET_FAMILY_CH32V30X) == rvswd_target_v30x_module());
    assert(rvswd_target_registry_module_from_family(
               WCHLINK_TARGET_FAMILY_CH58X) == rvswd_target_ch58x_module());
    assert(rvswd_target_registry_module_from_family(
               WCHLINK_TARGET_FAMILY_CH59X) == rvswd_target_ch59x_module());
    assert(rvswd_target_registry_module_from_chip_id(0x10320710u) ==
           rvswd_target_l103_module());
    module = rvswd_target_l103_module();
    assert(module->profile != NULL);
    assert(module->profile->loader != NULL);
    // 官方 CH32L103 program+verify loader 调用使用 mode 0x1c
    assert(module->profile->loader->program_verify_mode == 0x1cu);
    assert(rvswd_target_registry_module_from_chip_id(0x20310510u) ==
           rvswd_target_v20x_module());
    assert(rvswd_target_registry_module_from_chip_id(0x2034051cu) ==
           rvswd_target_v20x_module());
    assert(rvswd_target_registry_module_from_chip_id(0x2080051cu) == NULL);
    module = rvswd_target_v20x_module();
    rvswd_test_memory_chip_id = 0x20310510u;
    assert(module->probe->read_chip_id(&operation, &chip_id));
    assert(chip_id == rvswd_test_memory_chip_id);
    operation.memory = module->memory;
    {
        bool protected = true;
        uint8_t memory_type = 0xffu;

        assert(module->flash->read_protected(&operation, module->profile,
                                             &protected));
        assert(!protected);
        assert(module->flash->read_memory_type(
            &operation, module->profile, true, &memory_type));
        assert(memory_type == 0u);
        assert(module->flash->read_memory_type(
            &operation, module->profile, false, &memory_type));
        assert(memory_type == 0u);
        assert(module->flash->set_memory_type(
            &operation, module->profile, true, 2u));
        assert(rvswd_test_memory_type_encoded == 5u);
        assert(module->flash->set_memory_type(
            &operation, module->profile, false, 2u));
        assert(rvswd_test_memory_type_encoded == 5u);
        assert(!module->flash->set_memory_type(
            &operation, module->profile, true, 4u));
    }
    assert_v20x_erase_sequence(module);
    assert_v20x_memory_write_sequence(module);
    assert_v20x_memory_streaming_fallback(module);
    rvswd_test_memory_chip_id = 0x2080051cu;
    assert(!module->probe->read_chip_id(&operation, &chip_id));
    assert(rvswd_target_registry_module_from_chip_id(0x30700528u) ==
           rvswd_target_v30x_module());
    assert(rvswd_target_registry_module_from_chip_id(0x82000000u) ==
           rvswd_target_ch58x_module());
    assert(rvswd_target_registry_module_from_chip_id(0x92000000u) ==
           rvswd_target_ch59x_module());
    assert(rvswd_target_registry_module_from_family(0x00u) == NULL);

    for (size_t index = 0u;
         index < rvswd_target_registry_module_count(); ++index) {
        module = rvswd_target_registry_module_at(index);
        if (module == rvswd_target_v20x_module()) {
            assert_v20x_identity_and_chip_info(module);
        } else {
            assert_module_operations(module);
        }
    }

    profile = module->profile;
    assert(profile == module->profile);
    return 0;
}
