#pragma once

#include "wchlink/rvswd/rvswd_types.h"

#include <stdbool.h>
#include <stdint.h>

// Debug Module 寄存器地址由 transport 统一定义，上层流程只引用有语义的名称
enum rvswd_dmi_address {
    RVSWD_DMI_DATA0 = 0x04u,
    RVSWD_DMI_DATA1 = 0x05u,
    RVSWD_DMI_CONTROL = 0x10u,
    RVSWD_DMI_STATUS = 0x11u,
    RVSWD_DMI_HARTINFO = 0x12u,
    RVSWD_DMI_ABSTRACTCS = 0x16u,
    RVSWD_DMI_COMMAND = 0x17u,
    RVSWD_DMI_ABSTRACTAUTO = 0x18u,
    RVSWD_DMI_PROGBUF0 = 0x20u,
    RVSWD_DMI_PROGBUF1 = 0x21u,
    RVSWD_DMI_PROGBUF2 = 0x22u,
    RVSWD_DMI_PROGBUF3 = 0x23u,
    RVSWD_DMI_PROGBUF4 = 0x24u,
    RVSWD_DMI_PROGBUF5 = 0x25u,
    RVSWD_DMI_WCH_CONFIG = 0x7du,
    RVSWD_DMI_WCH_SHADOW = 0x7eu,
    RVSWD_DMI_WCH_CHIP_ID = 0x7fu,
    RVSWD_DMI_WCH_DMOD = 0x80u,
};

// resume completion 只接管纯 resume 语义，允许保留 hart selection 字段
enum rvswd_dmcontrol_value {
    RVSWD_DMCONTROL_DMACTIVE = 1u << 0u,
    RVSWD_DMCONTROL_RESUMEREQ = 1u << 30u,
    RVSWD_DMCONTROL_RESUME_ALLOWED = 0x47ffffc1u,
};

enum rvswd_dmstatus_value {
    RVSWD_DMSTATUS_RUNNING = 3u << 10u,
    RVSWD_DMSTATUS_RESUMEACK = 3u << 16u,
};

// 每个 target ports 实例独占一个 transport，packet mode 和 GPIO timing 不跨实例共享
struct rvswd_transport {
    enum rvswd_packet_mode packet_mode;
    bool fast_timing;
};

// raw long probe 只描述一次物理帧采样，不应用 DMI 重试和状态分类
struct rvswd_transport_probe_result {
    uint32_t value;
    uint8_t address;
    uint8_t status;
};

// DMI 结果与一次 read/write 调用绑定，失败诊断不会被后续事务覆盖
struct rvswd_transport_result {
    uint32_t value;
    uint8_t status;
    bool ok;
    bool retryable;
};

void rvswd_transport_init(struct rvswd_transport *transport);
void rvswd_transport_disconnect(struct rvswd_transport *transport);
void rvswd_transport_set_packet_mode(struct rvswd_transport *transport, enum rvswd_packet_mode mode);
enum rvswd_packet_mode rvswd_transport_packet_mode(const struct rvswd_transport *transport);
void rvswd_transport_set_fast_timing(struct rvswd_transport *transport, bool enabled);
void rvswd_transport_wakeup(struct rvswd_transport *transport, bool stop_condition);

struct rvswd_transport_result rvswd_transport_read(struct rvswd_transport *transport, uint8_t address);
struct rvswd_transport_result rvswd_transport_write(struct rvswd_transport *transport, uint8_t address, uint32_t value);
// 执行一次短帧交换并返回原始状态，连接探测使用该接口推进 DMI 管线
struct rvswd_transport_result rvswd_transport_probe_short(struct rvswd_transport *transport, bool read, uint8_t address, uint32_t value);
struct rvswd_transport_probe_result rvswd_transport_probe_long(struct rvswd_transport *transport, uint8_t operation, uint8_t address, uint32_t value, uint8_t host_parity);
