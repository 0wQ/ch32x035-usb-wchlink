#include "wchlink/rvswd/rvswd_debug.h"
#include "wchlink/rvswd/rvswd_memory.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

static struct {
    uint8_t address;
    uint32_t value;
} writes[8];
static size_t write_count;
static size_t fail_write_index;
static uint32_t abstractcs_value;
static bool abstractcs_read_ok;
static bool cleanup_seen;

static struct rvswd_transport_result success_result(void) {
    return (struct rvswd_transport_result){.ok = true};
}

static struct rvswd_transport_result failure_result(void) {
    return (struct rvswd_transport_result){.ok = false};
}

static void reset_fixture(void) {
    write_count = 0u;
    fail_write_index = SIZE_MAX;
    abstractcs_value = 0u;
    abstractcs_read_ok = true;
    cleanup_seen = false;
}

struct rvswd_transport_result rvswd_operation_write_dmi(
    struct rvswd_operation *operation, uint8_t address, uint32_t value) {
    (void)operation;
    assert(write_count < sizeof(writes) / sizeof(writes[0]));
    writes[write_count].address = address;
    writes[write_count].value = value;
    if (write_count++ == fail_write_index) {
        return failure_result();
    }
    return success_result();
}

struct rvswd_transport_result rvswd_operation_read_dmi(
    struct rvswd_operation *operation, uint8_t address) {
    (void)operation;
    (void)address;
    return failure_result();
}

void rvswd_operation_cleanup_write_dmi(struct rvswd_operation *operation,
                                       uint8_t address, uint32_t value) {
    (void)operation;
    assert(address == RVSWD_DMI_ABSTRACTCS);
    assert(value == 0x00000700u);
    cleanup_seen = true;
}

bool rvswd_debug_wait_abstract_idle(struct rvswd_operation *operation,
                                    uint32_t *abstractcs) {
    if (!abstractcs_read_ok) {
        return false;
    }
    *abstractcs = abstractcs_value;
    operation->abstractcs = abstractcs_value;
    return true;
}

bool rvswd_debug_write_raw_gpr(struct rvswd_operation *operation,
                               uint8_t regno, uint32_t value) {
    (void)operation;
    (void)regno;
    (void)value;
    return false;
}

struct rvswd_transport_result rvswd_transport_read(
    struct rvswd_transport *transport, uint8_t address) {
    (void)transport;
    (void)address;
    return failure_result();
}

static void test_direct_write_success(void) {
    struct rvswd_operation operation = {0};

    reset_fixture();
    assert(rvswd_memory_write32_direct(&operation, 0x20002010u,
                                       0x12345678u));
    assert(write_count == 4u);
    assert(writes[0].address == RVSWD_DMI_ABSTRACTCS);
    assert(writes[1].address == RVSWD_DMI_DATA1);
    assert(writes[1].value == 0x20002010u);
    assert(writes[2].address == RVSWD_DMI_DATA0);
    assert(writes[2].value == 0x12345678u);
    assert(writes[3].address == RVSWD_DMI_COMMAND);
    assert(writes[3].value == 0x02210000u);
    assert(!cleanup_seen);
    assert(operation.memory_code == 0u);
}

static void test_direct_write_cmderr(void) {
    struct rvswd_operation operation = {0};

    reset_fixture();
    abstractcs_value = 0x00000200u;
    assert(!rvswd_memory_write32_direct(&operation, 0x20002010u,
                                        0x12345678u));
    assert(operation.memory_code == 0xc2u);
    assert(operation.abstractcs == abstractcs_value);
    assert(cleanup_seen);
}

static void test_direct_write_transport_failure(void) {
    struct rvswd_operation operation = {0};

    reset_fixture();
    fail_write_index = 3u;
    assert(!rvswd_memory_write32_direct(&operation, 0x20002010u,
                                        0x12345678u));
    assert(operation.memory_code == 0xc1u);
    assert(!cleanup_seen);
}

int main(void) {
    test_direct_write_success();
    test_direct_write_cmderr();
    test_direct_write_transport_failure();
    return 0;
}
