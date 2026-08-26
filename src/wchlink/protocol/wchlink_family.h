#pragma once

// 目标族编号由 USB 协议分派和 RVSWD 目标 profile 共用，保持单一来源
enum wchlink_target_family {
    WCHLINK_TARGET_FAMILY_V30X = 0x06u,
    WCHLINK_TARGET_FAMILY_CH58X = 0x07u,
    WCHLINK_TARGET_FAMILY_CH59X = 0x0bu,
    WCHLINK_TARGET_FAMILY_X035 = 0x0du,
    WCHLINK_TARGET_FAMILY_L103 = 0x0eu,
};
