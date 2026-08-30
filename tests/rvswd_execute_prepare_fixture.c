#include "wchlink/rvswd/rvswd_memory.h"
#include "wchlink/rvswd/rvswd_types.h"
#include "wchlink/target/rvswd_target_x03x.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

struct prepare_operation {
    bool read;
    uint32_t address;
    uint32_t value;
};

static struct prepare_operation operations[16];
static size_t operation_count;

bool rvswd_memory_read32(struct rvswd_operation *operation,
                         const struct rvswd_target_profile *profile,
                         bool target_identified, uint32_t address,
                         uint32_t *value) {
    uint32_t read_value = address == 0x40021000u ? 0x6b83u : 0xffffffffu;

    (void)operation;
    (void)profile;
    (void)target_identified;
    assert(operation_count < sizeof(operations) / sizeof(operations[0]));
    operations[operation_count++] = (struct prepare_operation){
        .read = true,
        .address = address,
        .value = read_value,
    };
    *value = read_value;
    return true;
}

bool rvswd_memory_write32(struct rvswd_operation *operation, uint32_t address, uint32_t value) {
    (void)operation;
    assert(operation_count < sizeof(operations) / sizeof(operations[0]));
    operations[operation_count++] = (struct prepare_operation){
        .address = address,
        .value = value,
    };
    return true;
}

static void expect_operation(size_t index, bool read, uint32_t address,
                             uint32_t value) {
    assert(index < operation_count);
    assert(operations[index].read == read);
    assert(operations[index].address == address);
    assert(operations[index].value == value);
}

int main(void) {
    struct rvswd_operation operation = {0};
    struct rvswd_target_profile profile = {
        .wchlink_family = WCHLINK_TARGET_FAMILY_X03X,
    };

    assert(rvswd_target_x03x_loader_prepare(&operation, &profile, 1u));
    assert(operation_count == 11u);
    expect_operation(0u, true, 0x40021000u, 0x6b83u);
    expect_operation(1u, false, 0x40021000u, 0x6b83u);
    expect_operation(2u, false, 0x40021004u, 0x50u);
    expect_operation(3u, false, 0x40021014u, 0u);
    expect_operation(4u, false, 0x40021018u, 0u);
    expect_operation(5u, false, 0x40021020u, 0u);
    expect_operation(6u, false, 0x40022000u, 0x12u);
    expect_operation(7u, false, 0x40022010u, 0x8080u);
    expect_operation(8u, true, 0x4002201cu, 0xffffffffu);
    expect_operation(9u, true, 0x40022020u, 0xffffffffu);
    expect_operation(10u, false, 0xe000f000u, 0u);

    operation_count = 0u;
    operation = (struct rvswd_operation){0};
    return 0;
}
