#include "wchlink/rvswd/rvswd_debug.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

enum {
    TEST_MAX_READ_RESULTS = 320u,
    TEST_MAX_EVENTS = 330u,
};

enum test_event_kind {
    TEST_EVENT_READ,
    TEST_EVENT_WRITE,
    TEST_EVENT_CLEANUP_WRITE,
};

struct test_event {
    enum test_event_kind kind;
    uint8_t address;
    uint32_t value;
};

static struct rvswd_transport_result test_read_results[TEST_MAX_READ_RESULTS];
static struct rvswd_transport_result test_write_results[TEST_MAX_EVENTS];
static struct test_event test_events[TEST_MAX_EVENTS];
static size_t test_read_result_count;
static size_t test_read_result_index;
static size_t test_write_result_count;
static size_t test_write_result_index;
static size_t test_event_count;
static uint64_t test_time_us;

// 结果队列按底层调用顺序返回，事件队列独立记录实际访问顺序和清理路径
static struct rvswd_transport_result test_success(uint32_t value) {
    return (struct rvswd_transport_result){
        .value = value,
        .ok = true,
    };
}

static struct rvswd_transport_result test_failure(bool retryable) {
    return (struct rvswd_transport_result){
        .status = retryable ? 3u : 2u,
        .retryable = retryable,
    };
}

static void test_reset(void) {
    test_read_result_count = 0u;
    test_read_result_index = 0u;
    test_write_result_count = 0u;
    test_write_result_index = 0u;
    test_event_count = 0u;
    test_time_us = 0u;
}

// 每个用例只声明会被消费的结果，额外访问会由 mock 中的边界断言直接失败
static void test_push_read(struct rvswd_transport_result result) {
    assert(test_read_result_count < TEST_MAX_READ_RESULTS);
    test_read_results[test_read_result_count++] = result;
}

static void test_push_write(struct rvswd_transport_result result) {
    assert(test_write_result_count < TEST_MAX_EVENTS);
    test_write_results[test_write_result_count++] = result;
}

static void test_record_event(enum test_event_kind kind, uint8_t address,
                              uint32_t value) {
    assert(test_event_count < TEST_MAX_EVENTS);
    test_events[test_event_count++] = (struct test_event){
        .kind = kind,
        .address = address,
        .value = value,
    };
}

static void test_expect_event(size_t index, enum test_event_kind kind,
                              uint8_t address, uint32_t value) {
    assert(index < test_event_count);
    assert(test_events[index].kind == kind);
    assert(test_events[index].address == address);
    assert(test_events[index].value == value);
}

uint64_t bsp_time_us(void) {
    return test_time_us;
}

// 虚拟时钟由 delay 推进，使 3 ms 超时用例无需依赖主机实际调度
void bsp_delay_us(uint32_t us) {
    test_time_us += us;
}

struct rvswd_transport_result rvswd_operation_read_dmi(
    struct rvswd_operation *operation, uint8_t address) {
    struct rvswd_transport_result result;

    assert(operation != NULL);
    assert(test_read_result_index < test_read_result_count);
    test_record_event(TEST_EVENT_READ, address, 0u);
    result = test_read_results[test_read_result_index++];
    operation->dmi_status = result.status;
    operation->retryable = !result.ok && result.retryable;
    return result;
}

// 普通写和 cleanup 写分开记录，避免失败分支用普通事务伪装完成清理
struct rvswd_transport_result rvswd_operation_write_dmi(
    struct rvswd_operation *operation, uint8_t address, uint32_t value) {
    struct rvswd_transport_result result;

    assert(operation != NULL);
    assert(test_write_result_index < test_write_result_count);
    test_record_event(TEST_EVENT_WRITE, address, value);
    result = test_write_results[test_write_result_index++];
    operation->dmi_status = result.status;
    operation->retryable = !result.ok && result.retryable;
    return result;
}

void rvswd_operation_cleanup_write_dmi(struct rvswd_operation *operation,
                                       uint8_t address, uint32_t value) {
    assert(operation != NULL);
    test_record_event(TEST_EVENT_CLEANUP_WRITE, address, value);
}

static void test_resume_success_after_busy(void) {
    struct rvswd_operation operation = {0};
    uint32_t dmstatus = 0u;

    test_reset();
    test_push_write(test_success(0u));
    test_push_write(test_success(0u));
    test_push_write(test_success(0u));
    test_push_read(test_failure(true));
    test_push_read(test_success(0x00000382u));
    test_push_read(test_success(0x000c0c82u));

    assert(rvswd_debug_resume(&operation, 0x40000001u, &dmstatus));
    assert(dmstatus == 0x000f0c82u);
    assert(test_event_count == 6u);
    test_expect_event(0u, TEST_EVENT_WRITE, RVSWD_DMI_WCH_DMOD, 0u);
    test_expect_event(1u, TEST_EVENT_WRITE, RVSWD_DMI_CONTROL,
                      0x40000001u);
    test_expect_event(2u, TEST_EVENT_READ, RVSWD_DMI_STATUS, 0u);
    test_expect_event(3u, TEST_EVENT_READ, RVSWD_DMI_STATUS, 0u);
    test_expect_event(4u, TEST_EVENT_READ, RVSWD_DMI_STATUS, 0u);
    test_expect_event(5u, TEST_EVENT_WRITE, RVSWD_DMI_CONTROL, 0x00000001u);
}

static void test_resume_write_failure_cleans_request(void) {
    struct rvswd_operation operation = {0};
    uint32_t dmstatus = 0u;

    test_reset();
    test_push_write(test_success(0u));
    test_push_write(test_failure(false));

    assert(!rvswd_debug_resume(&operation, 0x40000001u, &dmstatus));
    assert(test_event_count == 3u);
    test_expect_event(2u, TEST_EVENT_CLEANUP_WRITE, RVSWD_DMI_CONTROL,
                      0x00000001u);
}

static void test_resume_nonretryable_read_cleans_request(void) {
    struct rvswd_operation operation = {0};
    uint32_t dmstatus = 0u;

    test_reset();
    test_push_write(test_success(0u));
    test_push_write(test_success(0u));
    test_push_read(test_failure(false));

    assert(!rvswd_debug_resume(&operation, 0x40000001u, &dmstatus));
    assert(test_event_count == 4u);
    test_expect_event(3u, TEST_EVENT_CLEANUP_WRITE, RVSWD_DMI_CONTROL,
                      0x00000001u);
}

static void test_resume_timeout_cleans_request(void) {
    struct rvswd_operation operation = {0};
    uint32_t dmstatus = 0u;

    test_reset();
    test_push_write(test_success(0u));
    test_push_write(test_success(0u));
    for (size_t index = 0u; index < 300u; ++index) {
        test_push_read(test_success(0x00000382u));
    }

    assert(!rvswd_debug_resume(&operation, 0x40000001u, &dmstatus));
    assert(test_read_result_index == 300u);
    assert(test_time_us == 3000u);
    test_expect_event(test_event_count - 1u, TEST_EVENT_CLEANUP_WRITE,
                      RVSWD_DMI_CONTROL, 0x00000001u);
}

static void test_resume_clear_failure_retries_cleanup(void) {
    struct rvswd_operation operation = {0};
    uint32_t dmstatus = 0u;

    test_reset();
    test_push_write(test_success(0u));
    test_push_write(test_success(0u));
    test_push_write(test_failure(false));
    test_push_read(test_success(0x000c0c82u));

    assert(!rvswd_debug_resume(&operation, 0x40000001u, &dmstatus));
    assert(test_event_count == 5u);
    test_expect_event(4u, TEST_EVENT_CLEANUP_WRITE, RVSWD_DMI_CONTROL,
                      0x00000001u);
}

static void test_resume_rejects_invalid_control(void) {
    struct rvswd_operation operation = {0};
    uint32_t dmstatus = 0u;

    test_reset();
    assert(!rvswd_debug_resume(&operation, 0x00000001u, &dmstatus));
    assert(!rvswd_debug_resume(&operation, 0x40000003u, &dmstatus));
    assert(!rvswd_debug_resume(&operation, 0xc0000001u, &dmstatus));
    assert(!rvswd_debug_resume(&operation, 0x60000001u, &dmstatus));
    assert(!rvswd_debug_resume(&operation, 0x40000009u, &dmstatus));
    assert(test_event_count == 0u);
}

int main(void) {
    test_resume_success_after_busy();
    test_resume_write_failure_cleans_request();
    test_resume_nonretryable_read_cleans_request();
    test_resume_timeout_cleans_request();
    test_resume_clear_failure_retries_cleanup();
    test_resume_rejects_invalid_control();
    return 0;
}
