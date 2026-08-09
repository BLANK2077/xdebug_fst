# xdebug-fst 的 Verilator DesignDB 与 Wellen 波形架构

## 一、文档目的

本文是《xdebug_oc 全能力兼容修复、Goal 执行与分批提交计划》的配套架构文档，解释以下问题：

1. xdebug-fst 为什么同时需要 Verilator 和 Wellen；
2. 我们对 Verilator 做了什么修改，为什么必须这样做；
3. Verilator DesignDB 的生成、编译、装载和查询架构是什么；
4. 为什么对 Verilator 的修改必须保持克制；
5. xdebug-fst 对 Wellen 的能力要求是什么；
6. `wellen_capi` 与 `wellenx_capi` 如何分工；
7. 当前已经实现了什么，仍有哪些差异需要在 P3 至 P7 收敛。

本文描述的是整体目标架构，同时明确标注 P0 至 P2 已落地能力与后续阶段目标，避免把当前的协议、会话兼容误认为全部 73 个分析 action 已经与原版 xdebug 完全一致。

### 1.1 FST-only 架构边界

本架构边界受任务书永久 Goal 约束 **`GOAL-FST-DIRECT-001`** 管辖。它是所有后续实现选择的否决条件，不是可以在 action 迁移过程中临时放宽的偏好。

xdebug-fst 只接收和分析 FST 波形。Wellen 在本方案中的职责是提供 FST 的层级、时间和值变化语义；VCD/FSDB 不属于产品输入，也不得成为测试 fallback。VCD 可以保留为可读的测试波形源描述，但必须先由固定生成链转换成 FST，测试和验收只能打开生成后的 `.fst`。如果转换后的 FST 丢失四态、delta 或类型信息，应修复生成链或 Wellen FST 读取层，不得直接读取 VCD 绕过问题。

这里的“分析 FST”只表示 action 使用从原始 `.fst` 直接取得的波形事实，并不引入名为“FST 分析”的第二套系统。唯一数据流是“当前 session 的原始 `.fst` → Wellen 按需访问 → action 查询/推理”。禁止的数据流包括“FST → VCD/JSON/私有索引/离线数据库/全量内存快照 → action”。本项目只需要并且也必须完整适配 FST 波形；Wellen 即使具备其他格式能力，xdebug-fst adapter 也不得暴露或使用这些能力。

Verilator DesignDB 与这条边界不冲突：它只提供源文件位置、driver/load、端口连接、控制依赖等 FST 天生不携带的静态设计事实。它既不读取 FST，也不替代 Wellen，更不保存或重建波形值。显式 export action 的文件同样只是最终用户产物，不进入上述数据流，也不得被重新加载用于分析。

## 二、为什么需要两个相互独立的事实源

原版 xdebug 同时回答两类本质不同的问题。

第一类是波形事实：

- 某个信号在某个物理时间的值是什么；
- 信号何时发生变化；
- 时钟边沿前、边沿当时和边沿后的值是什么；
- 一段时间内是否出现 X/Z、脉冲、稳定、stall、transaction 或协议事件；
- 多信号是否能在同一观察点进行一致采样。

第二类是设计事实：

- 某个信号由哪些语句和源信号驱动；
- 某个信号被哪些语句或信号消费；
- assignment 是 continuous、procedural 还是 NBA；
- 驱动语句位于哪个源文件和行号；
- 跨端口、跨层级、条件分支和数据依赖如何连接。

FST 只保存仿真过程中选择写入的层级、时间和值变化，不包含完整 HDL AST、赋值语句、控制条件和源代码位置。因此不能只靠 FST 正确实现 `trace.driver`、`trace.load`、`trace.active_driver`、`trace.active_driver_chain` 和 `trace.x_origin`。

反过来，Verilator AST 能描述设计结构和静态关系，但不保存每个时间点的运行值。因此不能只靠 Verilator DesignDB 实现 `value.at`、`signal.changes`、clock sampling、协议 transaction 和运行时 active-driver 判定。

xdebug-fst 因而采用两个事实源：

| 事实源 | 负责内容 | 不负责内容 |
| --- | --- | --- |
| Wellen/FST | 层级、时间表、值变化、四态值、变化索引、采样 | HDL 语句、driver/load、源代码行、控制依赖 |
| Verilator DesignDB | 信号元数据、driver/load、assignment kind、源位置、静态控制依赖 | 仿真时刻的值、某一时刻真正激活的分支 |

最终的 combined action 由 xdebug-fst 在统一 engine 中把两类事实按规范化信号名和观察时间连接起来，而不是要求任一后端承担它不拥有的事实。

## 三、整体架构

```text
                   Verilator 编译/展开 HDL
                            │
                            │ --design-db（显式启用）
                            ▼
               <prefix>__DesignDb.cpp
                            │
                            │ g++ -shared -fPIC
                            ▼
               lib<prefix>__DesignDb.so
                            │
                            │ dlopen / dlsym
                            ▼
                 XddDesignBackend
                            │
                            ├─────────────┐
                            │             │
                            │             ▼
原始 .fst ─────► Wellen ──► WellenFstBackend ──► xdebug action engine
                  │          ▲             │
                  │          │             ├─ waveform actions
                  ├─ wellen_capi            ├─ design actions
                  └─ wellenx_capi           └─ combined actions
                                                │
                                                ▼
                                      xdebug.v1 JSON / XOUT
```

目标公开请求合同沿用原版字段：

- `target.fsdb` 可以指向 FST 文件；
- `target.daidir` 指向一个 Verilator DesignDB bundle；
- bundle manifest 唯一确定要加载的 `.so`；
- 不接受公开字段 `target.design_db`，也不扫描波形邻近目录猜测 `.so`。

### 3.1 DesignDB bundle 合同

`target.daidir` 必须是一个真实存在的目录。目录根部必须包含
`xdebug-design-db.json`，当前严格格式是：

```json
{
  "schema_version": "xdebug.design-db-bundle.v1",
  "library": "libVtop__DesignDb.so"
}
```

manifest 只允许这两个字段。`library` 必须是 bundle 内的相对路径，规范化后必须仍位于 bundle 内，并且必须指向普通 `.so` 文件。绝对路径、逃逸 bundle
的 `..`/符号链接、缺失文件、多个候选的目录扫描都被拒绝。这样做有四个原因：

1. `target.daidir` 保持与原版公开合同一致；
2. 选择结果确定，不依赖目录遍历顺序或命名猜测；
3. registry 记录的是公开 bundle，而 engine 只接收 manifest 解析出的私有库路径；
4. bundle 可独立做版本、完整性和兼容性检查，不把 Verilator 构建目录布局泄漏进 action 层。

### 3.2 Session、engine 与 UDS 架构

P2 已移除旧的“当前进程里覆盖一份全局资源”过渡语义。当前 UDS 路径为：

```text
one-shot / stdio frontend
        │
        ├─ session.open
        │     ├─ 规范化资源并记录 fingerprint
        │     ├─ reserve opening generation
        │     ├─ 原子写 generation marker
        │     ├─ fork + exec 同一 xdebug-fst --server
        │     ├─ 私有 server.ping 校验 generation
        │     └─ CAS opening → active
        │
        └─ target.session_id 请求
              └─ registry 定位 endpoint
                    └─ 0600 AF_UNIX socket
                          └─ 持久 engine（Wellen/XDD 资源只打开一次）
```

registry 用独占文件锁、临时文件、`fsync`、原子 `rename` 和 generation
compare-and-swap 防止同名并发打开、旧进程清理新会话以及时间戳倒退。generation 是
`/dev/urandom` 产生的 256 bit 随机值；managed wrapper 提供的 ownership token
只以 SHA-256 摘要持久化，明文不写 registry、日志或响应。

UDS 使用一行一个 JSON object 的 framing，单帧上限 16 MiB，socket 权限固定为
`0600`。`server.ping` 返回 engine generation，frontend 只有在 endpoint generation
与 registry generation 相同时才把它视为所管理的进程；`session.close` 走私有
`server.quit`，`session.kill` 的信号操作也受同一 generation endpoint 证明约束。
UDS 连接、超时或解析失败直接返回 transport error，绝不自动切换其他 transport。
根据 2026-08-09 用户范围决定，TCP 与 file server 不实现；冻结 schema 中仍保留原版
enum，但请求这两种模式时 fail closed 返回 `TRANSPORT_UNAVAILABLE`。

清理不是“先删 registry 再尽力杀进程”。frontend 先把同一 generation 原子转成
`cleanup_failed`，保留可管理证据；随后优先发送 `server.quit`，必要时只有在 ping
generation 相同或 `/proc/<pid>/cmdline` 同时包含 session id 与 256 bit generation
时才发送 `SIGTERM/SIGKILL`。进程停止、generation marker 对齐、artifact 删除和
registry 条件删除全部成功后，记录才真正消失。这避免 PID 复用时误杀无关进程，也让
中途失败可以由 `session.gc` 重试。

`session.doctor` 不只检查 PID：它依次验证 lifecycle、generation marker、daidir/FST
fingerprint、UDS 节点和 generation ping。`session.list` 根据严格解析的
`XDEBUG_SESSION_IDLE_TIMEOUT_SEC` 回收 idle session，并返回结构化 removal evidence；
非法环境值 fail closed。engine 在 resource 打开后、active CAS 前重新采集 fingerprint，
防止启动窗口内文件被替换。私有 `server.ping/version/quit` 也使用封闭字段合同，额外字段
不会意外触发 quit。

### 3.3 run manifest 与资源来源证明

当调用方提供 `target.run_manifest` 时，session.open 不只检查资源“现在能否打开”，还要求
资源能由一个已发布的运行清单证明。当前严格合同为 `xdebug.run-manifest.v1`，根对象只允许
`schema_version`、`state` 和 `resources`，其中 `state` 必须为 `published`。`resources.fsdb`
始终必需；combined 模式还必须有且只能有 `resources.daidir`，与公开 target 精确对应。

每项资源声明只允许三个字段：相对 `path`、非负 `size_bytes` 和 64 位小写十六进制
`sha256`。相对路径以 manifest 所在目录为根进行 canonicalize，必须与 session.open 已解析的
真实资源路径一致。FST 按文件内容计算 SHA-256；DesignDB bundle 按排序后的目录树计算摘要，
把目录记为 `D\n<relative>\n`，把文件记为 `F\n<relative>\n<content>`。路径、大小或摘要任一
不一致都返回 `RESOURCE_PROVENANCE_MISMATCH`，并携带 manifest、resource、expected/actual
路径、大小或摘要证据，不会尝试旁路 manifest 或猜测另一份资源。

校验发生在 generation reservation 和 fork 之前，因此来源不可信不会遗留 opening record
或子进程。校验成功后，响应中的 `data.run_manifest` 返回已经核验的 canonical manifest；
未提供清单时明确返回 null。engine 启动完成到 active CAS 之前仍会再次检查资源 fingerprint，
两层检查分别防止来源错配和启动窗口内替换。

### 3.4 多会话提示与批量清理

不同 session id 可以显式打开同一份资源，以便调用方独立管理生命周期。为了避免无意间
重复占用资源，成功响应会按原版合同附加 `RESOURCE_SESSION_ALREADY_ALIVE` advisory，区分
`same_fsdb`、`same_daidir` 和 `same_combined_resource`，但不会擅自复用或关闭已有 session。

`session.close` 与 `session.kill` 支持 `target.session_id="all"`。frontend 先在锁保护下读取
当前 generation 集合，再逐项执行同一套 generation-safe 清理，返回 requested/removed 计数
和 `removed_sessions`；只要有一项失败，就返回 `SESSION_CLEANUP_PARTIAL_FAILURE`、失败 id
以及已清理数量。批量模式禁止 ownership token，因为一个 token 只能作为一个精确 session
generation 的条件清理证明。该能力没有放宽 cleanup_failed 保留规则。

GCC 8 对 C++17 `std::filesystem` 仍使用独立的 `libstdc++fs`。CMake 现在对
`xdebug-fst` 显式链接 `stdc++fs`，保证相同源码在当前冻结工具链中可重复配置和链接，
无需更换编译器或绕开构建环境。

## 四、我们对 Verilator 做了什么

### 4.1 新增显式 CLI 开关

在 `V3Options` 中增加 `--design-db` 布尔开关。默认值为 false，只有用户显式启用时才生成 DesignDB。

这样做的原因是：

- 普通 Verilator 用户不应承担额外扫描和文件生成成本；
- 不应改变未使用 xdebug 的模型生成结果；
- DesignDB 是附加调试产物，不是 Verilator 普通仿真语义的一部分；
- 显式开关便于独立回归、性能评估和发生问题时定位边界。

### 4.2 新增只读 emitter

新增文件：

- `src/V3EmitDesignDb.h`
- `src/V3EmitDesignDb.cpp`
- `include/xdd_api.h`

并在 `src/CMakeLists.txt` 中仅登记这一个新 pass 的头文件和源文件。

emitter 在 `V3Scope` 已经建立 `AstVarScope` 之后运行，因为此时能够看到展开后的层级信号；同时它位于可能删除或折叠信号的后续优化之前，以尽量保留调试需要的信号和关系。

这个 pass 是只读的：遍历 AST、构建自己的临时表并写出 C++，不修改 AST，不改变后续优化输入。

### 4.3 收集信号元数据

第一阶段遍历 `AstVarScope`，为可见信号建立稳定索引并记录：

- 层级信号名；
- port/reg/wire 类型；
- bit width；
- 源文件；
- 源代码行号；
- HDL 声明方向（input/output/inout/ref）。

参数和 Verilator 内部生成对象不会作为普通用户信号发布。重复名字只保留一个索引，后续 driver/load 表引用该索引。

### 4.4 收集 driver、load 和控制依赖

第二阶段遍历 assignment 节点：

- 从 LHS 找到 target signal；
- 从 RHS 找到 source signals；
- 区分 `cont_assign`、`proc_assign` 和 `nba`；
- 向上查找所在的 `if` 或 `case`，把条件表达式中的信号加入静态依赖；
- 为每条依赖原生标记 `rhs`、`control` 或 `statement`，避免消费者根据名称或行号猜测；
- 记录 assignment 的源文件和行号；
- 同时反向生成 load 记录。

这些记录回答的是“哪些信号和语句可能影响 target”，不是“某个时刻哪一条分支已经被证明激活”。后者必须由 xdebug-fst 结合 FST 值、条件表达式和采样语义判断。

### 4.5 生成可独立编译的静态数据库

第三阶段生成 `<prefix>__DesignDb.cpp`，其中包括：

- signal table；
- 按名称排序的二分查找索引；
- 按 target 分组的 driver table；
- 按 source 分组的 load table；
- 预计算并去重的跨层 port-boundary table；
- ABI version、capability 位和 signal direction table；
- `xdd_*` C ABI 实现。

生成文件不链接 Verilator compiler 内部对象。它可以单独执行：

```bash
g++ -std=c++17 -shared -fPIC \
    -I<verilator-root>/include \
    -o lib<top>__DesignDb.so \
    <prefix>__DesignDb.cpp
```

最终 `.so` 只包含设计的静态表和很小的查询代码，xdebug-fst 不需要把 Verilator 编译器嵌入运行进程。

### 4.6 最小 XDD C ABI

当前公开 ABI 提供：

- `xdd_abi_version` / `xdd_capabilities`，当前 ABI 为 v2；
- `xdd_init` / `xdd_close`；
- `xdd_signal_count`；
- `xdd_resolve`；
- signal name/type/width/file/line/direction；
- port connection count 和第 N 条连接；
- driver count、第 N 条 driver 及其 `rhs/control/statement` dependency role；
- load count 和第 N 条 load。

使用 C ABI 而不是直接暴露 C++ 容器的原因是：

- 避免 libstdc++ ABI、编译参数和容器布局耦合；
- `dlopen`/`dlsym` 可以在运行时装载具体设计；
- ABI 面积小，便于版本化、hash 锁定和兼容测试；
- xdebug-fst 可以独立于 Verilator compiler 构建。

### 4.7 xdebug-fst 如何消费 DesignDB

`XddDesignBackend` 使用 `RTLD_NOW | RTLD_LOCAL` 装载 `.so`，解析 `xdd_*` 符号并调用 `xdd_init`。

它把 C ABI 转换为 xdebug-fst 内部 `IDesignBackend`：

- 名称解析和 signal metadata 直接映射；
- driver/load 记录和 dependency role 转换为 C++ value objects；
- direction 和 port connection 直接消费 XDD 原生确定性事实，不再全表启发式推导；
- 强制要求 ABI v2 及 direction、port connection、driver role 三项 capability，旧 bundle、缺符号或 capability 不全都明确失败，不自动降级；
- 后续 action 只依赖 `IDesignBackend`，不直接依赖 Verilator 头文件。

这种接口隔离允许我们优先在 xdebug-fst 修正协议、schema、排序、错误合同和组合算法，而不为了上层表现差异频繁修改 Verilator。

## 五、为什么必须克制修改 Verilator

Verilator 是上游大型编译器。对它的修改会影响解析、展开、优化、代码生成、构建时间和大量既有回归。xdebug-fst 的功能缺口并不自动意味着 Verilator 缺能力，缺口可能位于：

- xdebug-fst 没有正确组合已有 XDD 记录；
- 信号名称或 alias 没有正确归一化；
- FST 采样观察点错误；
- response schema 或错误合同不一致；
- active-driver 算法只选了第一条候选记录；
- 测试 fixture 或归一化器掩盖了差异。

因此后续 P4 遵守以下门槛：

1. 先建立原版与 xdebug-fst 的失败差分用例；
2. 证明缺失的是设计静态事实，而不是上层算法错误；
3. 证明现有 XDD ABI 无法表达该事实；
4. 才允许增加最小、附加、向后兼容的字段或表；
5. 每项新增能力有独立 Verilator 回归；
6. 不重构无关 pass，不改变未启用 `--design-db` 的行为。

截至 P4，只提交了一个显式开关、一个只读 emitter、一个小型附加 C ABI 和对应测试，没有改写既有优化、调度或仿真算法。P4 的 direction/port connection 与 dependency role 都先有修改前失败用例；没有失败证据的 process order、sequential boundary 没有加入。

## 六、Verilator 回归为什么要重做

原有未提交的全量测试脚本连续调用四次 `test.compile(verilog_files=...)`，但 Verilator 回归驱动并不消费这个参数。结果是 simple、full、UART 和 metadata 共用了同一个 test name、prefix 和 obj_dir，后一次编译覆盖前一次生成物，测试可能检查错误文件或旧文件。

我们将其拆成独立用例：

- simple continuous assignment；
- full driver/load/control/NBA；
- metadata 与非法索引；
- 多文件 UART；
- operators 与多层层级；
- interface/array/enum 冒烟；
- 基础 emitter 生成测试。

每个用例拥有独立 prefix、obj_dir、生成 C++、`.so` 和 validator。UART 静态 DesignDB 提取使用 `--no-timing`，避免当前未启用 coroutine 的 Verilator 构建把测试标记为 skip；这不会修改 UART RTL，也不改变要提取的静态设计关系。

当前验收结果是 7 个 DesignDB 用例全部真实执行通过，同时通过 Verilator distribution copyright/license 检查。ELF、FST、obj_dir 和临时验证程序均被忽略，不进入 Git。

## 七、xdebug-fst 对 Wellen 的需求

### 7.1 文件与生命周期

xdebug-fst 需要：

- 打开 FST；
- 明确报告打开失败；
- 同一 session 内保持 waveform handle；
- close 时释放 mmap、signal cache 和字符串；
- 多 session 并发时不能依赖无保护的全局可变状态。

即使 Wellen 库本身还支持 VCD/GHW，xdebug-fst 生产 adapter 也只允许 `.fst`，并在调用 Wellen 前拒绝其他后缀；`wellen_capi` 或 `wellenx_capi` 任一 handle 打开失败都会关闭整个 backend，不会换格式、换 backend 或降级继续。

### 7.2 层级和信号解析

为了实现 `scope.roots`、`scope.list`、`signal.resolve`、list 和 protocol actions，需要：

- 枚举所有 top scope；
- 任意深度递归 child scope；
- 枚举每个 scope 的 variable；
- local name 和 full hierarchical name；
- interface、array、struct 的最终 leaf；
- 同一底层 signal 的 alias；
- 稳定且无歧义的 signal reference。

### 7.3 时间与 timescale

为了对齐原版物理时间语义，需要：

- 完整 time table；
- waveform timescale；
- 从物理时间到 time-table index 的严格映射；
- `ps/ns/us` 与裸数字的严格解析；
- `render_time_unit=auto/ps/ns/us`；
- 范围边界、文件起始时间和非整纳秒时间；
- 同一物理时间的 delta-cycle 顺序。

只把字符串中的数字取出来是不正确的：`1ns` 和 `1us` 必须落在不同物理时间。

### 7.4 值类型

xdebug 的 canonical value 不能退化为普通整数。Wellen 层必须保留：

- bit width；
- 2-state、4-state 和 9-state 信息；
- `0/1/x/z` 以及 Wellen 支持的其它状态；
- 宽总线的完整 bit string；
- real；
- string；
- event；
- missing value 与读取错误的区别。

这些事实用于 `value.at`、X/Z 分析、表达式求值、协议字段、counter 和 active-driver 条件判断。

### 7.5 采样语义

原版不仅查询“这个时间之前最后一个值”。它还需要：

- raw observation；
- exact-time match；
- before edge；
- at edge；
- after edge；
- rising/falling clock sampled；
- 同一时间点多个 element/delta；
- 下一次变化位置。

Wellen 的 signal offset、`time_match`、`elements` 和 `next_index` 是实现这些语义的基础；最终观察点规则由 xdebug-fst 的 waveform/clock sampling 层统一实现。

### 7.6 批量和完整性

73 个 action 中大量操作会重复读取同一批信号。后端需要：

- 批量 load/unload；
- 同一时间读取多信号；
- 获取变化 time indices；
- 范围扫描和游标；
- 对 `scan_complete`、`analysis_complete`、`truncated`、returned/total count 提供真实依据。

不能为了返回结果而悄悄截断宽度、变化数量或信号数量，也不能把资源失败当成空结果。

### 7.7 不是把 FST 转成离线分析数据库

FST 是本方案唯一允许的波形输入格式，但“适配 FST”不等于“把 FST 预处理成另一份分析数据库”。Wellen 在会话中打开原始 `.fst`，按 action 需要加载信号、查询时间表、读取值和遍历变化；xdebug-fst 在请求时把这些波形事实与 DesignDB 静态事实组合。不会先把 FST 转成 VCD、JSON、私有索引或全量内存快照，也不会因某项能力缺失改走 VCD/FSDB/backend fallback。

因此两类数据的职责始终分离：Wellen 对 `.fst` 做按需波形访问，Verilator DesignDB 提供不在波形中的静态 HDL 关系，xdebug-fst 执行合同校验与组合推理。DesignDB 不是 FST 分析结果，也不含运行时波形值。

## 八、Wellen 双 C ABI 方案

### 8.1 为什么不让 C++ 直接链接 Rust 内部 API

xdebug-fst 主体是 C++，Wellen 是 Rust。直接依赖 Rust 内部类型会造成：

- Rust ABI 不稳定；
- C++ 无法安全持有 Rust 容器和引用；
- 生命周期、panic 和错误边界难以审计；
- Wellen 升级时 xdebug-fst 会与内部实现强耦合。

因此使用 opaque handle + C ABI，把 Rust 数据所有权留在 Rust 内部。

### 8.2 `wellen_capi`：核心接口

位于独立 Wellen 仓库，负责：

- open/error/close；
- time count 和 time table；
- scope/variable 遍历；
- signal encoding 和 width；
- signal load/unload；
- signal info；
- offset、time match、element、next index；
- bit-vector/real/string/event 编码元数据；
- 容量感知的类型化值访问，保留 UTF-8、f64、event 与完整四态 bit string。

把核心 C API 放在 Wellen 仓库的原因是它描述 Wellen 本身的通用波形能力，可以独立测试，也便于未来减少 xdebug 私有扩展。

### 8.3 `wellenx_capi`：xdebug-fst 的最小扩展

当前 Wellen 核心 C API 已负责层级、时间、类型和值读取；仓内保留的最小扩展只提供：

- signal change time-index 数组。

它不复制层级、时间表和生命周期接口，不发展成第二套 Wellen API。只要核心 `wellen_capi` 将来原生提供等价能力，就可以逐项删除扩展，而不是长期维护两个重叠实现。

### 8.4 统一 1 基信号句柄

P0 审查发现原生 Rust `SignalRef(0)` 是合法首信号，但原 C API 同时用 0 表示“无效”，导致首信号无法访问；旧 C 测试因此跳过了核心取值。

修复后的 C ABI 规则是：

```text
Rust SignalRef index:  0  1  2  ...
C signal reference:   1  2  3  ...
invalid sentinel:     0
```

`wellen_capi` 和 `wellenx_capi` 必须执行完全相同的转换。我们为两边分别增加真实测试，断言 C reference 1 能加载原生首信号、读取变化和取值；卸载后查询必须失败。

### 8.5 `WellenFstBackend` 的职责

C++ adapter 同时持有：

- `WellenDb*`：核心层级、时间和 offset；
- `WellenxDb*`：bit string 和 change indices。

它负责：

- 缓存 time table；
- 建立名称到 signal reference 的索引；
- 对同一批 reference 同步 load/unload 两个 handle；
- 把 C ABI 值转换为内部 waveform value；
- 向 action 层提供统一 `IWaveformBackend`。

双 handle 必须打开同一个文件并使用同一 reference 编码，否则会出现“metadata 来自信号 A、值来自信号 B”的静默错误。这就是 P0 必须先统一 1 基句柄再锁依赖的原因。

## 九、当前状态与仍需修复的边界

### 9.1 P0 已完成

- Wellen 仓库建立 `feature/xdebug-fst-capi`；
- `wellen_capi` 有真实 Rust 和 C ABI 测试；
- Wellen 全 workspace 测试执行通过；
- 原生 signal 0 与 C reference 1 的映射已修复；
- `wellenx_capi` 同步改为 1 基并增加 2 个真实测试；
- Verilator 建立显式 `--design-db`、只读 emitter 和最小 XDD ABI；
- 8 个 Verilator DesignDB 用例和 1 个普通非 DesignDB 用例真实通过；
- Wellen revision、Verilator revision 和 ABI header hash 已写入依赖锁；
- CMake 配置阶段严格校验 revision、header hash 和 release library。

### 9.2 P1、P2 与 P3 当前进展

- P1 的严格 73 action、146 个公开 schema、请求/响应 runtime gate、canonical
  JSON/XOUT 和 stdio-loop 已完成；
- P2 的 registry、generation 状态机、真实 engine 子进程、UDS transport、严格
  DesignDB bundle manifest、ownership token 摘要和 generation 条件清理已落地；
- UDS 回归真实覆盖父子进程 round-trip、`0600` 权限、非法 JSON、重复名称、
  `open/list/doctor/kill`、token mismatch、公开 action 路由及最终资源清理。
- 可选 MCP 集成门禁直接加载相邻 xverif MCP adapter：direct 与 fake-LSF 都使用
  当前 `xdebug-fst --stdio-loop --json`，覆盖 trace metadata、managed ownership token、
  UDS native engine、scheduler noise、job id、bkill 和双层清理；默认开源构建不强依赖
  xverif 源树，验收时显式启用 `XDEBUG_ENABLE_MCP_INTEGRATION_TESTS`。
- P3 已完成 signal reference sentinel、任意深度层级、alias 消歧、FST timescale、
  严格物理时间解析和 auto/ps/ns/us 渲染；类型化值保留 X/Z、64 位宽度、real、
  UTF-8 string 和 event。
- FST 同时间多个 element 按 delta 顺序发布；raw/after 选择稳定后的最后 delta，
  before 选择精确时间点全部 delta 之前的值。后端还提供类型化批量取值、变化序号
  游标和范围扫描，并区分完整分析与响应行数截断。
- 生产 adapter 和测试入口都建立 FST-only 门禁：非 `.fst` 在解析前失败，全部 P3
  固件实际输入均为 FST；VCD 不作为输入或 fallback。
- P4 已原生发布 ABI v2、capability、声明方向、预计算 port connection 和逐 driver
  dependency role。xdebug-fst 要求完整 capability 并 fail closed；未加入无失败证据的
  process order 或 sequential boundary，Verilator 修改保持在 emitter、C ABI header 和
  对应定向测试内。

### 9.3 仍不能宣称完全一致的内容

当前过渡实现仍有明确缺口，后续阶段不得用文档掩盖：

- P3 后端事实已补齐，但 73 个 action 尚需在 P5 全部迁移到统一观察点、类型化批量值
  和完整性接口，不能继续直接选择第一个 delta element；
- interface/array/struct leaf 已用深层 FST hierarchy 回归覆盖，仍需在 P5 对应公开
  scope/signal action 中通过冻结 schema 和原版差分确认响应形状；
- current active-driver 不能只选择第一条可读静态 driver；
- XDD 当前控制依赖只提供静态候选，尚未表达完整条件表达式和嵌套 provenance；
- direction/port connection 仍有上层推导逻辑；
- P2 已完成 UDS idle timeout、完整失败补偿、MCP direct 和 fake-LSF；TCP/file
  已按用户明确要求裁剪，不作为实现或验收项；
- UDS 已打通真实 engine，但大部分 action 的成功 payload 仍需 P3/P5 对齐严格
  response schema，不能把 transport 已通等同于 action 能力已兼容。

特别是 Verilator emitter 当前输出的某些 interface pseudo signal 可能 width 为 0，array 可能只显示聚合名；是否扩展 XDD 必须先用原版差分证明这些事实确实是某个公开 action 的必要输入。

### 9.4 P5 已落地的会话内 FST action 架构

P5 当前已把发现、静态设计、`value.at`、list、event、cursor 和 RC 生成迁移到冻结合同。`value.at`、`list.first_change`、`event.find` 和所有导出数据收集都直接调用当前 `WellenFstBackend`：先按请求涉及的最终叶子信号加载，再在指定物理时间、时钟边沿和 observation point 读取类型化值。命名 list、event config 和 cursor 只保存路径、表达式、采样策略或时间书签，不保存波形值或变化索引。

三类显式文件产物必须与“离线 FST 分析”严格区分：

- `list.export` 按公共合同写出 `u64bin.v1`，用于调用者消费最终列表数据；
- `event.export` 按公共合同写出 JSON 事件结果；
- `nwave.rc.generate` 写出 nWave 视图脚本，并明确要求工具另行打开原始 FST。

这些文件只在请求明确给出输出路径时写出，不参与 session.open，不被任何 action 自动重载，不是 file transport，也不允许在 Wellen 失败时充当 fallback。生产分析唯一波形事实源仍是会话中由 Wellen 直接按需读取的 `.fst`。因此“必须适配 FST 波形”和“允许公共 export action 产生最终结果文件”并不冲突：前者约束分析输入与事实来源，后者只是调用者显式要求的输出。

## 十、后续演进原则

1. `GOAL-FST-DIRECT-001` 始终生效：Wellen 仅从当前 session 的原始 `.fst` 按需提供波形事实，Verilator 负责设计静态事实，xdebug-fst 负责合同和组合推理；
2. 不把 backend 失败伪装成空结果；
3. 不在 backend 之间自动 fallback；
4. 不从 XOUT 反解析结构化事实；
5. 不为上层 schema/排序问题修改 Verilator；
6. 不为临时方便复制 Rust 内部结构到 C++；
7. 先有失败差分，再扩展 XDD；
8. 每次 ABI 变化同时更新 header hash、依赖锁、C/Rust 测试和 xdebug consumer；
9. 只保留仓库既有、由冻结开源 Verilator 可重复生成的 DesignDB 测试 `.so`；不新增仿真 ELF、普通 obj_dir 产物、FSDB、daidir 或 proprietary 内容；
10. 最终验收以严格 73 action、全 schema、UDS/stdio transport、全差分和 clean worktree 为准；TCP/file 是明确登记的用户裁剪项。
11. 不建立 FST 的 VCD/JSON/私有索引/离线数据库/全量内存快照副本用于分析，不重载显式 export 产物，不增加 backend 或 fixture fallback；违反任一项时，即使功能测试通过也不得完成 Goal。

## 十一、相关文件和提交

### xdebug-fst

- `src/backend/wellen_fst_backend.*`
- `src/backend/xdd_design_backend.*`
- `wellenx_capi/`
- `dependencies.lock.json`
- `cmake/DependenciesLock.cmake`
- `cmake/VerifyDependencies.cmake`

### Wellen

- `wellen_capi/src/lib.rs`
- `wellen_capi/include/wellen_capi.h`
- `wellen_capi/test/`
- revision `066d86ad26e82ae02407ad2a64c5a226b8ebe212`

### Verilator

- `src/V3EmitDesignDb.*`
- `include/xdd_api.h`
- `test_regress/t/t_xdd_*`
- revision `50d8fff59df67a2eafcd19676e6ce6cc9827c0b7`

对应提交：

- Wellen `dba5242`：建立可测试的 Wellen C 波形访问接口；
- Wellen `1d66a9e`：增加 Wellen C ABI 端到端验证；
- Wellen `c5132ef`：区分根层级与递归层级并发布稳定 full name；
- Wellen `76e5077`：通过 C ABI 发布 FST timescale；
- Wellen `066d86a`：通过容量感知 C ABI 保真发布 bit/real/string/event；
- Verilator `80c4226ae`：增加最小化 DesignDB 生成接口；
- Verilator `e04eb0ea8`：修复全量回归并覆盖独立设计；
- Verilator `a1f1aba1c`：发布声明方向与预计算跨层端口边；
- Verilator `6aae8d201`：覆盖 ABI v2、方向和端口连接回归；
- Verilator `50d8fff59`：以附加访问器区分 RHS 与控制依赖；
- xdebug-fst `9a529cc`：统一 wellenx 与 Wellen 的信号句柄编码；
- xdebug-fst `5b2595a`：锁定 Wellen 与 Verilator 兼容版本。
- xdebug-fst `f61670a`：补齐 FST delta、观察点、批量游标与扫描完整性；
- xdebug-fst `e1779c9`：建立生产与回归 FST-only 硬门禁。
