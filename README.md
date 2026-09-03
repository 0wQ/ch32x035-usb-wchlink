# CH32X035 USB WCH-LinkE

基于 CH32X035 的 WCH-LinkE 实现，提供 USB 主机通信、目标芯片 RVSWD 调试连接、Flash 操作、目标供电控制和状态指示功能

## 硬件引脚接线

| 功能 | 引脚 | 是否必须 | 说明 |
| --- | --- | --- | --- |
| USB D- | PC16 | ✅ | USB D-  |
| USB D+ | PC17 | ✅ | USB D+ |
| RVSWD SWCLK | PA2 | ✅ | 连接目标芯片 SWCLK |
| RVSWD SWDIO | PA3 | ✅ | 连接目标芯片 SWDIO |
| 按钮 | PA5 | ➖ | 上拉输入，用于 MUX 交换 SBU1/2 |
| WS2816C DIN | PA7 | ➖ | 状态指示 |
| 目标 Type-C SBU1/2 MUX 使能 | PA0 | ➖ | 低电平使能，高电平关闭 |
| 目标 Type-C SBU1/2 MUX 交换 | PA4 | ➖ | 低电平为正常映射，高电平为交叉映射 |
| 目标 Type-C D+ 上拉控制 | PB11 | ➖ | 控制 USB D+ 外部上拉 |
| 目标 Type-C VBUS 开关 | PB12 | ➖ | 高电平打开目标负载开关，低电平关闭 |

3.3 V 和 5 V 电源控制命令共用 PB12 目标电源开关输出

## 已支持目标芯片 Family

- CH32X03X
- CH32L10X
- CH32V20X
- CH32V30X
- CH58X
- CH59X

以上目标芯片 Family 均通过 RVSWD 路径接入，单线协议目前暂未实现
