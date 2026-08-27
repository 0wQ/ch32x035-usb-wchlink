#pragma once

#include "wchlink/target/rvswd_target_result.h"

#include <stdint.h>

struct wchlink_target_ports;

// DMI port 只执行按值 transaction，不暴露 transport packet mode 和 retry 状态
struct rvswd_target_result wchlink_target_ports_read_dmi(
    struct wchlink_target_ports *ports, uint8_t address);
struct rvswd_target_result wchlink_target_ports_write_dmi(
    struct wchlink_target_ports *ports, uint8_t address, uint32_t value);
struct rvswd_target_result wchlink_target_ports_resume_dmi(
    struct wchlink_target_ports *ports, uint32_t dmcontrol);
