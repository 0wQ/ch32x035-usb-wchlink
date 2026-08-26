# CH5xx 量产保护与 Debug 关闭计划

## 状态

- 文档状态：规划
- 实现状态：未支持
- 适用目标：CH58x、CH59x，首批实板验收为 CH582 和 CH592
- 当前结论：不能复用 CH32 Option Bytes 的 `protect/unprotect` 算法

## 问题与结论

当前工程已经支持 CH582、CH592 的识别、RVSWD、内存访问、Flash 擦写和复位，但不支持
它们的量产保护操作。实板执行基础 WCH-Link 保护命令时得到：

```text
protect   -> 0x55 / 0x22
unprotect -> 0x55 / 0x25
```

其中 `0x22` 和 `0x25` 分别来自当前 CH32 Option Bytes backend 的“不支持目标”和“写入
目标失败”错误。legacy 与重构工程都明确拒绝在 `ch5xx_protocol` 上运行 CH32 保护算法，
因此这不是重构回归。

CH32 的基础保护命令修改 RDP Option Byte，解除保护仍可经 RVSWD 执行，并由目标硬件擦除
主 Flash。CH58x、CH59x 的量产安全操作是关闭 Debug 接口，关闭后同一 RVSWD 链路不再具备
恢复能力。两者在命令语义、恢复路径和数据保留风险上都不同，不能让 `wlink protect` 在
CH5xx 上静默改为关闭 Debug，也不能把 `wlink unprotect` 描述为可逆操作。

本功能需要先取得官方 LinkE 的完整行为证据，并在同一块目标板上证明 ISP 恢复闭环，随后
才能实现。当前只落规划，不写入未经验证的目标配置序列。

## 已确认的证据

### 官方寄存器定义

`openwch/ch583` 固定提交
[`bd508ad7ceed48377619837051412a651952857f`](https://github.com/openwch/ch583/tree/bd508ad7ceed48377619837051412a651952857f)
的 `CH583SFR.h` 定义：

```text
R8_GLOB_CFG_INFO  0x40001045  RO
RB_CFG_ROM_READ   0x01        1 表示允许外部编程器读取 Flash
RB_CFG_BOOT_EN    0x08        1 表示 bootloader 已启用
RB_CFG_DEBUG_EN   0x10        1 表示 Debug 已启用
```

源码位置：
[`CH583SFR.h:241`](https://github.com/openwch/ch583/blob/bd508ad7ceed48377619837051412a651952857f/Application/wristband/firmware/Demo_Firmware/StdPeriphDriver/inc/CH583SFR.h#L241-L245)

这只能证明三个状态位的读取语义。`R8_GLOB_CFG_INFO` 是只读寄存器，公开头文件没有给出
从 RVSWD 关闭或重新开启 Debug 的写入接口，不能从这些位反推出量产配置算法。

### 官方 LinkE 协议证据

现有 LinkE 固件分析已经确认独立协议组：

```text
81 0e 01 01  Disable Debug
```

该命令与协议组 `0x06` 的 CH32 读保护、解保护命令相互独立。当前证据只确认命令入口和
CH5xx 的适用范围，尚未确认以下内容：

- 官方 LinkE 的完整命令回复和 USB 断开时序
- 目标配置写入地址、解锁序列和访问宽度
- 是否需要复位或重新上电才生效
- 命令对 `RB_CFG_ROM_READ`、`RB_CFG_BOOT_EN` 的联动影响
- ISP 重新开启 Debug 时是否擦除主 Flash

相关本地分析见：

- [LinkE 固件中的 CH5xx 分派与 loader 证据](../../../ch32x035-usb-legacy/docs/references/notes/wch-linke-ch5xx-firmware-evidence.md)
- [WCH-LinkE Option Bytes 分派](../../../ch32x035-usb-legacy/docs/references/notes/wch-linke-option-byte-dispatch.md)

### 当前实板边界

CH582 使用当前重构固件已经完成 10/10 冷启动连接、整片擦除、编程、完整回读、严格
`cmp` 和 reset 后复连。相同固件下的保护命令仍按设计拒绝：

- [CH582 重构候选硬件回归](../../../../debug-archive/20260827-rvswd-refactor-ch582-regression/README.md)

MRS 在 CH592 上的历史结果为 `query_rprotect=4`，该返回值不能按 CH32 的 `1=protected`、
`2=unprotected` 解释。当前协议层为兼容 LinkE，在 CH5xx 的协议组 `0x06` 查询中报告
未保护；这个回复不等于已经查询 `RB_CFG_ROM_READ` 或 `RB_CFG_DEBUG_EN`。

## 安全语义

必须把以下状态分开表达：

| 状态 | 来源 | 含义 |
| --- | --- | --- |
| External read | `RB_CFG_ROM_READ` | 外部编程器是否可读取 Flash |
| Debug access | `RB_CFG_DEBUG_EN` | RVSWD Debug 是否启用 |
| Bootloader | `RB_CFG_BOOT_EN` | ISP 恢复入口是否启用 |
| CH32 RDP | CH32 Option Bytes | 仅适用于对应 CH32 profile |

`Debug access=disabled` 不得被编码成普通的 `read_protected=true`，因为普通保护 API 暗示存在
同一路径的解除操作。CH5xx 关闭 Debug 后，恢复必须转到 ISP/bootloader，并且恢复是否
擦除 Flash 需要实板确认。

执行关闭 Debug 前必须满足：

1. 已通过真实 ChipID 锁定 CH58x 或 CH59x profile，family hint 不能授权破坏性操作
2. 能读取 `R8_GLOB_CFG_INFO`，且 `RB_CFG_DEBUG_EN=1`
3. `RB_CFG_BOOT_EN=1`
4. 同一目标板已经证明可以进入 ISP、被 `wchisp` 识别并重新开启 Debug
5. 测试使用可牺牲目标，目标 Flash 已保存固定镜像和 SHA-256

即使 `RB_CFG_BOOT_EN=1`，固件也无法证明板级 BOOT、RESET 和电源控制确实可用。正式工具
仍需显式确认恢复条件，探针不得在 flash、erase、reset、disconnect 或自动连接流程中隐式
关闭 Debug。

## 目标代码结构

保护策略不继续堆入 `flash/rvswd_flash_option.c`。计划增加独立的 security 责任域：

```text
src/wchlink/
  protocol/
    wchlink_wire.*
  session/
    wchlink_command_security.c
  target/
    wchlink_target_security.h
  security/
    rvswd_security.h
    rvswd_security_ch5xx.c
```

职责划分如下：

| 模块 | 职责 |
| --- | --- |
| `protocol/wchlink_wire` | 定义 `0x0e` wire 常量并编码已抓包确认的回复 |
| `session/wchlink_command_security` | 校验完整帧、连接状态和 capability，映射结果并终止旧 session |
| `target/wchlink_target_security` | 提供 target port，不暴露 transport、DMI 或寄存器细节 |
| `security/rvswd_security_ch5xx` | 读取安全状态，执行已验证的 CH5xx Debug 关闭序列 |
| `target/rvswd_target_profile` | 只声明目标使用哪个 security backend |

profile 使用明确的 backend 类型，不再借用 `ch5xx_protocol` 或填充无效的 CH32 Option Bytes
字段来表达安全能力：

```c
enum rvswd_security_backend {
    RVSWD_SECURITY_NONE,
    RVSWD_SECURITY_CH32_OPTION_BYTES,
    RVSWD_SECURITY_CH5XX_PRODUCTION,
};
```

具体寄存器地址和 bit mask 保持在 backend 私有实现中。公共状态使用独立枚举表达
`unknown/enabled/disabled`，避免用一个 `protected` 布尔值同时表示 External read、Debug
和 CH32 RDP。

公共操作不能设计成可逆的 `set_protected(bool)`，至少应拆为：

```text
query_security_state()
disable_debug()
```

CH5xx backend 不提供 `enable_debug()`。恢复动作属于 ISP 工具和 bootloader，不经过已经关闭
的 RVSWD target port。

## 协议处理原则

- 协议组 `0x06` 保持现有 CH32 Option Bytes 语义，CH5xx 继续拒绝写操作
- 协议组 `0x0e` 只接受抓包确认的精确帧 `81 0e 01 01`
- 未连接、未知 ChipID、仅有 family hint、非 CH5xx profile 和畸形帧均不得访问目标
- 不把 `wlink protect` 映射为 `Disable Debug`
- 不提供伪造的 `unprotect` 成功回复
- 不根据 RVSWD 在关闭后的超时单独判断成功，成功判据由官方回复和冷启动状态共同定义
- 命令完成后无条件使当前 target session 失效，后续命令必须重新连接

官方 LinkE 若在目标关闭 Debug 前先发送成功回复，本实现必须保持相同顺序；若官方命令不
回复，也必须保持无回复语义。该选择必须来自 USB 抓包，不能根据上位机当前未使用返回值
自行决定。

## 分阶段实施

### 阶段 1：固定官方行为和恢复路径

对 CH582、CH592 分别建立独立调试归档，记录：

1. 固定测试镜像的长度和 SHA-256
2. 关闭前 `R8_GLOB_CFG_INFO` 原始值
3. 官方 LinkE 的 `0x0e` USB 请求、回复和耗时
4. 关闭期间可见的 RVSWD/DMI 事务和最后一个有效回复
5. 重新上电后的 LinkE、项目探针和 MRS 连接结果
6. 进入 ISP 的硬件条件和 `wchisp` 完整输出
7. 重新开启 Debug 后的状态位和 Flash 完整回读结果

同时继续反汇编官方 LinkE 的 `0x0e` handler，确认其目标写入序列。抓包与反汇编必须相互
吻合后才进入破坏性实现。

### 阶段 2：只读状态与 capability

先提交不改变 wire 行为的结构变更：

- profile 增加 typed security backend
- CH58x、CH59x 选择 CH5xx backend
- 实现 `R8_GLOB_CFG_INFO` 只读解析
- 未知 profile 和读取失败返回明确 target error
- host fixture 覆盖三个状态位的独立组合

本阶段不处理 `0x0e`，不改变 `protect/unprotect` 结果。

### 阶段 3：wire 命令骨架

- 增加协议组 `0x0e` 的精确长度和 payload 校验
- 增加不支持目标、无真实 ChipID、状态未知和 bootloader 关闭的拒绝 fixture
- 使用假的 target port 验证成功、失败和 session 失效状态
- backend 仍返回 unsupported，不在实板写入配置

### 阶段 4：CH582 破坏性实现

只在阶段 1 已证明的序列上实现 CH5xx backend。先使用可牺牲 CH582 验证：

1. 项目探针关闭 Debug
2. 冷启动后 RVSWD 确认不可连接
3. ISP 仍可识别目标
4. ISP 重新开启 Debug
5. 冷启动后项目探针恢复连接
6. 完整回读固定镜像，记录保留或擦除事实

命令返回成功但冷启动后 Debug 仍开启，或命令返回失败但 Debug 已关闭，都视为实现失败。

### 阶段 5：CH592 独立验收

CH592 不因与 CH582 共用 CH5xx Flash backend 而自动视为通过。必须重复阶段 4 的完整闭环，
并单独记录配置位、ISP 恢复、Flash 数据结果和所有命令输出。

### 阶段 6：全量回归

- CH32X035、CH32L103、CH32V307 的 status、protect、冷启动、unprotect、冷启动
- CH582、CH592 的 status、erase、program、完整 verify、reset
- malformed 和 unsupported wire fixture
- Debug、Release 构建
- `git diff --check` 和结构检查

## 验收标准

功能只有同时满足以下条件才可标记完成：

- 官方 LinkE 的 `0x0e` 请求、回复和目标序列已有可复查证据
- CH582 与 CH592 都完成“关闭 Debug、冷启动失败、ISP 恢复、冷启动成功”的独立闭环
- 每次恢复后都按原镜像长度完整回读并执行严格 `cmp`
- 是否擦除 Flash 已分别记录，不能只依赖工具提示
- `RB_CFG_ROM_READ`、`RB_CFG_BOOT_EN`、`RB_CFG_DEBUG_EN` 的前后值已记录
- CH5xx 的 `protect/unprotect` 不会意外触发 Debug 关闭
- CH32 三类 Option Bytes 保护闭环无回归
- 破坏性实现只存在于 CH5xx security backend，session 和 wire 层不直接访问目标寄存器

## 明确不做

- 不猜测未公开的配置寄存器或密钥
- 不用普通 Flash 擦写命令模拟关闭 Debug
- 不在当前 RVSWD session 中实现虚假的 `enable_debug`
- 不把 CH582 的结果直接外推为 CH592 已通过
- 不仅凭命令返回成功判断量产保护已经生效
- 不在缺少 ISP 恢复证据的板上执行首次关闭 Debug
