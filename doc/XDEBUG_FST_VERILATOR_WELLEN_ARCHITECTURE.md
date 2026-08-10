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

本架构边界受任务书永久 Goal 约束 **`GOAL-FST-DIRECT-001`** 及其权威执行附件 [`XDEBUG_FULL_PARITY_GOAL_LOCK.md`](XDEBUG_FULL_PARITY_GOAL_LOCK.md) 管辖。它是所有后续实现选择的否决条件，不是可以在 action 迁移过程中临时放宽的偏好。

xdebug-fst 只接收 FST 波形，并把原版 xdebug 的调试分析能力完整适配到这些 FST 波形事实；FST 格式本身不承担分析。Wellen 在本方案中的职责仅是提供 FST 的层级、时间和值变化语义；VCD/FSDB 不属于产品输入，也不得成为测试 fallback。VCD 可以保留为可读的测试波形源描述，但必须先由固定生成链转换成 FST，测试和验收只能打开生成后的 `.fst`。如果转换后的 FST 丢失四态、delta 或类型信息，应修复生成链或 Wellen FST 读取层，不得直接读取 VCD 绕过问题。

这里的“分析 FST”只表示 action 使用从原始 `.fst` 直接取得的波形事实，并不引入名为“FST 分析”的第二套系统。唯一数据流是“当前 session 的原始 `.fst` → Wellen 按需访问 → action 查询/推理”。禁止的数据流包括“FST → VCD/JSON/私有索引/离线数据库/全量内存快照 → action”。本项目只需要并且也必须完整适配 FST 波形；Wellen 即使具备其他格式能力，xdebug-fst adapter 也不得暴露或使用这些能力。

Verilator DesignDB 与这条边界不冲突：它只提供源文件位置、driver/load、端口连接、控制依赖等 FST 天生不携带的静态设计事实。它既不读取 FST，也不替代 Wellen，更不保存或重建波形值。显式 export action 的文件同样只是最终用户产物，不进入上述数据流，也不得被重新加载用于分析。

架构审查必须同时验证 `GOAL-FST-DIRECT-001` 的两项不可拆分断言：唯一波形输入确实是原始 `.fst`，且分析责任确实留在 xdebug action 语义与必要的 Verilator DesignDB 静态事实中。只证明文件后缀为 `.fst` 不足以通过审查；若 action 被降级成只枚举 FST 信号或值变化、Wellen 被扩张为调试分析器，仍属于架构漂移。

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
- 为 `if/else`、普通 `case/default` 以及 `casez/casex` 生成使用完整层级信号名的
  activation predicate；其中内部 `==?z`/`==?x` 运算符保留两种四态通配规则；
- 记录 assignment 的源文件和行号；
- 同时反向生成 load 记录。

predicate 仍是静态设计事实，不包含任何运行时值。`casez/casex` 只发布匹配种类与
item 模式，实际 expression 值仍由 Wellen 从 FST 读取；尚未精确表达的 case inside/matches
发布空 predicate 并要求消费者失败关闭。这些记录回答的是
“哪些信号和语句可能影响 target”以及“激活该语句需要满足什么静态条件”，不是“某个
时刻哪一条分支已经被证明激活”。后者必须由 xdebug-fst 在 active time 通过 Wellen 直接
读取原始 FST 中的控制值并执行四态求值。

### 4.5 生成可独立编译的静态数据库

第三阶段生成 `<prefix>__DesignDb.cpp`，其中包括：

- signal table；
- 按名称排序的二分查找索引；
- 按 target 分组的 driver table；
- 与 driver table 等长的 activation predicate table；
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
- driver count、第 N 条 driver、其 `rhs/control/statement` dependency role 及 activation predicate；
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
- driver/load、dependency role 和 activation predicate 转换为 C++ value objects；
- direction 和 port connection 直接消费 XDD 原生确定性事实，不再全表启发式推导；
- 强制要求 ABI v2 及 direction、port connection、driver role、driver predicate 四项 capability，旧 bundle、缺符号或 capability 不全都明确失败，不自动降级；
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

截至当前 P6 批次，只提交了一个显式开关、一个只读 emitter、一个小型附加 C ABI 和对应测试，没有改写既有优化、调度或仿真算法。P4 的 direction/port connection 与 dependency role 都先有修改前失败用例。P6 又先用 counter fixture 证明同一 reset 控制记录无法区分 then/else，随后只增加并行 predicate table、单一 capability 和只读访问器；ABI 仍为 v2，既有函数签名与公开记录布局不变。没有失败证据的 process order、sequential boundary 仍未加入。

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

当前验收结果是 8 个 XDD/DesignDB 用例全部真实执行通过，同时通过 Verilator distribution copyright/license 检查。ELF、临时 FST、obj_dir 和临时验证程序均被忽略，不进入 Git；xdebug-fst 只提交既有最小 fixture 的原始 FST 和可独立装载 DesignDB bundle。

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
  dependency role；P6 的独立失败证据后又附加逐 driver activation predicate。
  xdebug-fst 要求完整 capability 并 fail closed；未加入无失败证据的
  process order 或 sequential boundary，Verilator 修改保持在 emitter、C ABI header 和
  对应定向测试内。

### 9.3 仍不能宣称完全一致的内容

当前过渡实现仍有明确缺口，后续阶段不得用文档掩盖：

- P3 后端事实已补齐，但 73 个 action 尚需在 P5 全部迁移到统一观察点、类型化批量值
  和完整性接口，不能继续直接选择第一个 delta element；
- interface/array/struct leaf 已用深层 FST hierarchy 回归覆盖，仍需在 P5 对应公开
  scope/signal action 中通过冻结 schema 和原版差分确认响应形状；
- active-driver 已禁止选择第一条静态 driver，并能用真实 FST 控制值判定已覆盖的
  `if/else`、APB 嵌套条件、普通 `case/default`、`casez/casex`、`case inside` 及 V3Inst 折叠后的
  同目标嵌套条件分支，并能对基础双连续赋值报告两条活动候选；基本 input/inout alias
  及带独立中间 net 的真实两级 inout 链已覆盖，复杂 output/inout alias、
  条件/过程/跨层多 driver 和更多 NBA 边界仍须逐项差分，不能据当前用例宣称全部关闭；
- XDD 已表达普通 `if/else`、普通 `case/default`、`casez/casex` predicate，并在当前
  emitter 内拆分 V3Inst 合并的 `AstCond` RHS，恢复叶子源位置与条件；case inside 已覆盖
  item-side wildcard 与闭区间；精确表达式 case matches 复用 `===`，tagged/pattern
  matches 仍明确 unsupported，不恢复或猜测；
- direction/port connection 仍有上层推导逻辑；
- P2 已完成 UDS idle timeout、完整失败补偿、MCP direct 和 fake-LSF；TCP/file
  已按用户明确要求裁剪，不作为实现或验收项；
- UDS 已打通真实 engine，但大部分 action 的成功 payload 仍需 P3/P5 对齐严格
  response schema，不能把 transport 已通等同于 action 能力已兼容。

特别是 Verilator emitter 当前输出的某些 interface pseudo signal 可能 width 为 0，array 可能只显示聚合名；是否扩展 XDD 必须先用原版差分证明这些事实确实是某个公开 action 的必要输入。

### 9.4 P5 已落地的会话内 FST action 架构

P5 当前已把发现、静态设计、`value.at`、list、event、cursor、RC、expression、signal、verify/window、counter、sampled pulse、valid-ready handshake、APB 和 AXI 迁移到冻结合同。`value.at`、`list.first_change`、`event.find` 及所有分析/导出数据收集都直接调用当前 `WellenFstBackend`：先按请求涉及的最终叶子信号加载，再在指定物理时间、时钟边沿和 observation point 读取类型化值。命名 list、event config 和 cursor 只保存路径、表达式、采样策略或时间书签，不保存波形值或变化索引。

第五十七批又把 Wellen 类型能力贯通到公开 `value.at` 回归：原始 string FST 的同时间 delta
由 raw observation 选择 settled 最后值，UTF-8 和尾部空格保持不变；real FST 作为 typed
数值发布，不伪造逻辑位宽；event FST 保持独立 event kind，不退化为 missing 或 X。这里的
选择和类型来自 Wellen 当前请求内访问，action 只投影冻结 LogicValue envelope；没有预扫、
中间转换、类型旁路或离线缓存。

第五十八批把 `signal.changes` 也切到 Wellen 已有的类型化 `scan_changes` 事实。time table 是
物理时间轴，但同一个 time index 可以对应多个有序 delta；action 不能再次按 time index
采 settled 值并假装那就是完整变化序列。现在 action 保留每条 change 记录的顺序和 typed
value，只负责物理窗口过滤、起点 initial 合成、transition 计数和 response projection。
`line_limit` 不传给后端作为分析预算，因此 total/complete 仍基于完整扫描，裁剪只发生在响应。

`counter.statistics` 在每个选定时钟边沿直接计算 `vld` 信号或 alias 表达式，并拼接 `cnt` 叶子值；`signal.sampled_pulse.inspect` 将 raw valid/payload 变化与同一窗口内的 sampled edge 对齐；`protocol.handshake.inspect` 在采样流上维护 valid 等待、stall、ready-only 区间与 data 稳定状态。这些都是请求期间的有界 action 状态，不是 FST 预处理结果，session 结束后不会形成可重载波形数据库。

APB 命名配置只保存时钟、复位和总线叶子信号路径以及采样规则，不保存事务或波形值。`apb.query`、`apb.statistics`、`apb.transaction.cursor` 和 `apb.transfer_window` 每次请求都直接扫描当前 FST 中选定的时钟边沿，并在请求期间推导已经完成的 APB 事务；推导结果不会持久化成可重载的事务数据库。`value.at` 的 APB 值源同样只按配置展开最终叶子信号，再从当前 Wellen backend 读取指定 observation point 的值。

AXI 命名配置同样只保存 clock/reset、edge/sample point 和五个 channel 的 31 个最终叶子路径。每个 AXI action 都从当前 session 的 Wellen backend 重新采样选定时钟边沿，在请求内按 AXI4 规则把 AW、W、B 以及 AR、R 握手临时配对；ID FIFO、W beat FIFO、outstanding 深度、latency 样本和 pending 状态只在该请求的有界内存中存在，不持久化为事务索引或离线波形库。`axi.export` 写出的 TSV/CSV/meta 是调用者显式要求的最终产物，任何 action 都不会重新加载它们。

stream 命名配置只保存 signal alias、clock/edge/sample point、reset、vld 及可选 rdy/bp、sop/eop 与 field 表达式。transfer、stall、packet、filter、动态 validate 和 export 每次请求都直接从当前 session 的 Wellen backend 读取原始 FST 中涉及的叶子，在请求期间形成有限的 sample、transfer、stall window 与 packet；`cache_scope` 在该实现中只约束本次扫描范围，不产生可跨请求重载的基础分析缓存。当前已支持全部 11 种 query、exact/range/mask packet filter、alias/slice/comparison/concatenation beat field、纯 vld、vld/rdy 与 vld/bp 流控，以及 transfer/packet/packet_beats preview 或显式最终文件。显式 `stream.export` 的 meta 标记事实源为 `current_session_fst`，任何 action 都不会回灌这些文件。channel interleaving 与 packet-stable field 的更深组合仍须独立回归，因此当前批次不冒充 stream 全合同最终关闭。

P6 的 active-driver 数据流同样没有增加第二套波形系统：DesignDB 只给出语句、依赖、
源码位置和静态 activation predicate；`trace.active_driver`、chain 与 X-origin 在请求期间
提取 predicate 引用的最终叶子信号，通过当前 `WellenFstBackend` 直接按需加载原始
`.fst`，在目标变化的 `active_time` 做四态求值，并只沿谓词为真的语句继续。谓词缺失、
解析失败、FST 信号缺失，或非 wildcard predicate 的控制值无法归约为已知真假时返回
unresolved/ambiguity evidence；`casez/casex` 的 X/Z 则严格按对应通配语义求值。不读取
VCD/JSON/export，不建立 predicate-value cache 或离线 FST 索引，也不回退到静态首项。

`trace.x_origin` 还必须区分“谓词的运行时值确实未知”和“缺少足够证据求值”。前者要求
predicate 已由 DesignDB 完整发布、表达式解析成功、每个叶子已唯一映射到当前 FST，且
Wellen 在指定 active time 成功返回四态值；只有最终表达式因 X/Z 无法归约时，action 才把
该语句标为 waveform-unknown，并枚举 DesignDB 已发布的 control/RHS 依赖，再用 Wellen
逐个确认哪些上游在当前时间实际含 X。后者包括空 predicate、解析失败、信号缺失、端口
映射歧义或加载失败，仍必须 unresolved/fail closed，绝不能伪造 X 分支。P6 第四十批用
同一既有 `GCD.vcd.fst` 原始 FST 的 `T_14/y/x` 在 0ps 均为 X 的事实验证这一边界：静态
predicate 和 dependency role 来自独立 DesignDB 测试变体，运行时四态值由 Wellen 按需
读取，control/RHS 分支、DFS、完整性和响应合同全部由 xdebug action 决定。FST 没有分析
predicate，也没有生成任何中间波形、索引、数据库或快照。

当证据缺失导致 opaque unresolved 时，公开响应也必须服从冻结合同：每条链保留
`status=unresolved` 与 `termination_detail=predicate_unresolved`，summary 在没有任何
完成链时使用 schema 允许的 `termination=pending`，同时报告
`evidence_status=unresolved`、`analysis_complete=false`；若另有完成链则为 `partial`。
不得通过放宽 schema、把 summary 写成未登记枚举，或把缺失信号伪装成 X 来消除错误。

零 driver 证据同样不能从 FST 值推导终止类型。`trace.active_driver_chain` 对 DesignDB 中
没有 driver 的内部 wire/output 必须返回 `unresolved`；只有 DesignDB 声明方向证明当前
信号为 input-like，并且静态端口边不存在可继续的父级连接时，才返回 `primary_input`。
Wellen 即使能从原始 FST 读取该内部信号的完整值，也没有权限把它分类成外部来源。
单步 `trace.active_driver` 的空 paths 可按其独立合同报告 `no_driver`，不得把两个 action 的
termination 规则混为一套。

`trace.x_origin.limits.max_time_steps` 约束 action DFS 实际访问的不同 X onset 状态。每个
候选上游仍由 DesignDB 静态依赖选出，再由 Wellen 在当前状态时间按需取值并向前查找该
信号连续为 X 的 onset；action 只把本次 DFS 已进入的 onset 加入有界 visited set。它不是
Wellen 对整份 FST 的预扫时间表，也不是持久化事件索引。预算耗尽时保留待继续信号及其
onset，返回 `limit/max_time_steps` 和不完整性证据。

纯 module/interface port hop 是可见的路径证据，但不是新的 X 语义分支。X-origin 因此以
非 `port` relation、信号和 X onset 构造语义 chain identity；复合 relation 只移除其中的
`port` token，保留 `rhs`、`control` 等因果角色。物理 alias 变体先按该 identity 合并，
`max_chains` 再作用于语义链；响应仍保留被选中物理路径的全部 port hop。不同 RHS、control
或 onset 不得合并，FST 值相等也不参与身份判断。当前证据关闭基础汇聚路径与 chain limit
交互；基础 node 预算汇聚与 `interface_modport_member` 的 node 预算组合已关闭，复杂
嵌套/数组 interface、ref、端口反馈以及 node/time/depth/loop 联合预算仍须独立差分。

物理 alias 路径还可能在 DFS 中重新汇聚。请求内 explored-state identity 使用已有非透明
语义前缀、当前 incoming 的非 port relation、current signal 和数值 X onset；因此不同
alias 中间节点仍会保留，汇聚后的相同状态只探索一次，并在 `max_nodes`、
`max_time_steps` 计数前去重。集合只包含身份字符串，只在单次 action 调用期间存在，不保存
波形值、不跨 session/request、不落盘或序列化，也不能由后续 action 重载，因而不是 FST
索引、缓存或离线分析数据库。超过 max-depth 的 frontier 先形成明确限制证据，不进入集合。

当前路径 visited 与全局 explored-state 语义不同：visited 命中表示本条因果链将闭环，必须
形成完成的 `loop_detected` chain，而不是静默过滤后把当前节点误报成 X origin；全局集合
命中则表示另一条物理路径已经探索过同一语义状态，可以抑制重复工作。loop child 不重复
追加目标 hop，`current` 指向闭环目标，origin 为空；summary 只存在 loop chain 时返回
`loop_detected`，但没有找到 X origin，因此 evidence status 仍保持 unresolved。是否成环由
DesignDB 静态依赖和 action 的 `(signal,onset)` 路径状态决定，不由 FST 值相等推断。

反馈环并不排斥同一静态 statement 的其他 X 依赖。第 48 批用独立测试 DesignDB 将一个
已访问依赖和一个未访问依赖同时发布给 action：前者形成完成的 `loop_detected` chain，
后者继续 DFS 并形成 `origin_found/candidate_x_source`。两链必须同时保留，summary 因实际
找到一个来源而采用 `origin_found`，不能因为存在 loop 把整体降成 unresolved，也不能因
找到正常来源而删除环证据。该测试复用 Wellen 仓库现有原始 GCD FST；DesignDB 决定静态
依赖，action 决定路径访问状态与汇总合同，Wellen 只按需确认各候选在对应时间含 X。因此
它继续满足 `GOAL-FST-DIRECT-001`，没有从 FST 值推断环或依赖，也没有建立转换和索引。

分支预算与深度预算可以同时生效。第 49 批证明：`max_chains` 只决定保留哪些语义分支，
被省略分支仍作为 pending/branch event 证据挂在保留链上；保留链随后独立受 `max_depth`
约束并生成可续跑 frontier。summary、limited/completed 计数、frontier 和建议动作必须反映
两种限制的组合，不能让后应用的 chain 裁剪擦除较早形成的 depth 状态，也不能把 frontier
误报为已找到来源。所有候选仍由 DesignDB 静态依赖选出，Wellen 只在当前原始 FST 中按需
提供 frontier 值与时间，预算和续跑协议完全由 action 负责。

X-origin 的用户查询时间和逐信号 X 首发时间属于不同事实。第 50 批锁定：summary/query
保留用户给定 `query_time`，每个 hop/current 则由 Wellen 对 DesignDB 已选定信号按需向前
查找其自身连续 X 区间的 onset；根和上游可以具有不同 onset。action 以这些时刻组织 DFS
状态和限制计数，不能把 query time 写成所有 hop 的 onset，也不能把根 onset 复制给不同
宽度或不同历史的上游。query 对象不因此扩 schema 字段，首发时间仍只属于 chain 证据。
这是一组按需读取，不是对整份 FST 建立事件时间表或持久化索引。

primitive output 不能仅因特殊静态 kind 或声明方向未知而被当成外部 primary input。第 51 批
由测试 DesignDB 明确发布 primitive RHS 与源码位置，active-driver chain 先沿该静态证据
进入真实上游，再仅在无父连接的 input-like 信号处终止。Wellen 只读取这两个 DesignDB
已确定信号的原始 FST 值，不参与判断 primitive、端口方向或源码位置。冻结 hop schema 以
file/line 和 signal path 表达公开证据，不为测试私自增加 kind 字段。

X mask 是确定性响应事实，不是装饰文本。Wellen 返回当前信号的实际位串和宽度，action
逐位把 X 类状态映射为 1、其余映射为 0，并使用 `<width>'b<bits>` 输出；query、current、
hop 和 frontier 必须共用同一规则，不能复用根信号宽度渲染不同宽度上游。冻结目录中的
早期 example 若仍含无宽度文本，不得覆盖当前冻结 runtime 及其回归明确要求的带宽格式，
也不得为了匹配示例而放宽或归一化掉这一确定性字段。

同一源文件行也不能直接等同于同一条动态语句。lowering 后的三元表达式可能把信号 RHS
叶子与常量 RHS 叶子保留在同一 `(file,line,kind)` 下，但每片叶子具有不同的静态
activation predicate。xdebug-fst 因此用 `(file,line,kind,predicate)` 作为语句身份，
先保持 DesignDB 叶子分离，再由 Wellen 在目标 `active_time` 直接读取 FST 控制值选择唯一
活动叶子。常量叶子没有 RHS 信号时，路径使用其控制依赖作为动态证据；它不会伪造一个
波形数据源，也不会扫描 FST 来反推 HDL 语句。这个修复完全位于 xdebug-fst consumer，
无需修改 Verilator/Wellen、XDD ABI 或 FST 文件格式。

跨层端口边由 DesignDB 以可从两端查询的静态连接发布，FST 中同一物理信号则可能由
Wellen 暴露为多个保真的层级 alias。对无 driver 的声明 input，active-driver chain 只能
从更深层端口向更浅层父级连接上溯；到达父级 primary input 后终止，不能因为连接可从
两端查询就反向折返并制造假环。xdebug-fst 在 consumer 侧比较规范信号路径的层级深度，
只对 input 应用这一方向约束；已有 driver 和 inout 保持原行为，等待各自失败证据。
DesignDB 仍只提供静态边，Wellen 仍只提供该 alias 在原始 FST 中的值，两者都不承担链路
分析或方向推理。

inout net 的 always-driven 连续赋值会被 V3Tristate 改写成内部 strength 网络。DesignDB
若只观察 lowering 后 AST，就会把 `inout_bus__strong` 暴露为 RHS；若简单追加原始 RHS，
又会制造两个活动 driver。为此 `007f1aa5c/8a5523487` 把旁路描述分为追加型与替换型：
普通同强度多驱动使用追加型；非三态 RHS 驱动 tristate/inout net 使用替换型，在发射阶段
只移除同一目标下以目标全名加 `__` 开头的内部生成 source/load，再附加原始 HDL RHS。
目标声明位置和实例全名共同约束替换范围，不触碰其他真实 driver。Wellen 仍只读取原始
FST 中子端口、父 net 与输入 alias 的值，端口方向和 RHS 来自 DesignDB，四跳链的选择与
终止仍由 xdebug action 完成。

第十一批进一步验证层级组合，而没有增加新实现：case 固件中的父级
`nested_inout_bus` 连接 `inout_mid.bus`，中间模块再以独立连续赋值驱动 `leaf_bus` 并连接
`inout_leaf.bus`。从最深端口回溯时，DesignDB 的静态边和原始 RHS 依次给出
`u_leaf.bus → leaf_bus → u_inout_mid.bus → nested_inout_bus → case_top.data → top.data`；
Wellen 只在每个 hop 的 active time 从同一原始 `.fst` 按需取值。纯端口透传可能被
Verilator 合法折叠成直接连接，所以独立中间 net 是为了让两级边界可观测，并非建立新的
波形事实或分析缓存。现有 Verilator `8a5523487`、Wellen `066d86a` 与 xdebug consumer
已经通过该用例，本批不修改三者代码，也不把这一单向上溯证据夸大为复杂双向 inout 已完成。

output 方向不能照搬上述父向 input/inout 规则。冻结原版的 module-boundary 回归要求从
父级连接 net 进入子模块 output，再沿子模块内部 assignment 追到 input 及其父级连接。
第十二批等价固件证明，当前 lowering 后 driver 把 `output_leaf.data_o = data_i` 的 RHS
直接规范成 `top.data`，导致 action 从 `child_output_bus` 一步跨过整个实例层级。即使 FST
中所有 alias 值一致，也绝不能据此补造端口 hop；后续只能消费 DesignDB 已有的精确静态
边，或在证明信息已经丢失后，以不改变普通 Verilator 的最小 DesignDB 专用事实保留修复。

实际 XDD 审计显示无需扩大 Verilator：`data_o` 仍静态连接父级
`child_output_bus`，`data_i` 仍静态连接扁平化源 `top.data`，父 net 的 driver 仍带内部
赋值第 98 行和同一个扁平化 RHS。consumer 因此增加三个受唯一性约束的静态图步骤：

1. 非端口 net 只在恰有一个更深 output port 反向连接时进入该 port；
2. output port 的 driver 若只是刚离开的父 net，则读取父 net 的唯一活动静态赋值，并在
   output 所在实例内寻找连接该 RHS 的唯一 input port；
3. input port 的 RHS 若越过祖先实例端口，则在所有连接同一静态源的 input port 中选择
   作用域为当前实例祖先且层级最近者。

这些选择只使用 DesignDB 的 direction、port connection、driver、predicate 和层级路径。
Wellen/FST 仅验证选定 hop 在 active time 可读取并呈现其值，绝不参与候选发现、同值搜索
或层级推断。任何候选不唯一的扩展场景都不会由本算法任选其一，必须先建立新差分并另行
定义完整性/歧义合同；不能把本批基本单输入/单输出模块边界算法外推到复杂 output 表达式
或多驱动。

两个独立过程对同一 reg 的 NBA 不需要新增波形或静态模型。第十三批的 DesignDB 以两个
`nba` statement 分别保存第 83/87 行、RHS 与 `!reset`/`sel[0]` predicate；action 在各自
active time 仍由 Wellen 直接读取原始 FST 控制值。只有一个谓词为真时返回唯一 driver，
两者同时为真时保留两个 statement group 并报告活动候选歧义。这里没有按最终 FST 值判断
“哪个 NBA 赢”，因为原版合同要求暴露同时活动的静态语句；FST 仍不是 HDL 调度分析器。
该基础证明不覆盖不同 clock、连续+过程混合或跨实例多驱动。

跨实例复用同一模块源码行时，`(file,line,kind,predicate)` 不再是完整 statement identity。
第十四批中 `u_output_a/u_output_b` 的内部赋值同为第 118 行，父 net 却有两个独立静态
driver；当前合并后错误变成一个 statement 的两个 RHS。现有 XDD 已能通过每个 output
port 的父 net 连接、同实例 input port 的扁平化源区分两个实例，因此应由 consumer 把该
静态实例作用域加入聚合 identity。FST 不包含 HDL statement identity，也不能用内部临时
信号是否被 dump 来增删 driver。

修复不扩展 XDD：`statement_identity` 只存在于 xdebug-fst 内部 DriverRecord。consumer 先
从目标父 net 的多个 output port 得到实例作用域，再从各实例 input port 的静态连接建立
`flattened source → instance scope` 映射；只有一个基础 statement 的每条依赖都有唯一
映射且最终出现多个实例时才写入 identity。聚合键于是成为
`(file,line,kind,predicate,statement_identity)`。同一实例表达式的多个 RHS 映射到相同
identity，仍正确保持一个 statement；任何不完整或多义映射保持未标注，绝不使用 FST
值或 dump 可见性补全。active-driver、chain 与 X-origin 共用同一分组入口。

常量 NBA 叶子没有 RHS signal，但仍是完整 assignment statement。第十五批证明 DesignDB
已通过该叶子的 control dependency 保存精确 file/line/predicate；chain 若只保留含 RHS 的
group，会正确终止却丢失源码行。consumer 的责任边界是：所有活动 assignment group 都
参与 statement identity、ambiguity 和 hop source evidence；只有 group 的 RHS record 能
形成下一跳。control record 可作为 representative evidence，但绝不能成为数据上游。

实际实现还必须排除 lowering 噪声：两级 inout 固件包含无 RHS、无 control、仅
`statement` role 的生成型 `proc_assign`。因此 chain 的可见活动 assignment 集合为“含
RHS，或 kind 为 NBA，或含 control dependency”；这既保留常量 NBA，也不把裸生成节点
算成第二条用户 driver。代表记录只供 source evidence，是否递归仍严格检查 role=`rhs`。

复杂 output 表达式要求边界优先级。第十六批中父 net 的两个 lowering RHS 与子模块内部
一个双 RHS assignment 是同一静态语句的不同层级视图；若在父 net 立即报告歧义，会丢失
原版要求的 child output hop。XDD 已足够：output port 连接父 net，两个 input port 分别
连接扁平化 source，父 driver 保存同一 file/line/predicate。consumer 应先沿唯一更深
output port 进入实例，再以同实例 input port 替换父 statement 的 RHS signal index，最后
在子 output 节点执行既有 statement grouping 与 ambiguity evidence。全部候选来自静态
DesignDB；Wellen 只在映射完成后读取所选 port 的原始 FST 值。

边界优先不是无条件覆盖：父级 activation predicate unresolved 时仍按原合同停止；只有
唯一更深 output 静态候选且对应 FST port 可读取时才跨界。output 侧的父 statement 必须
唯一，每个 RHS 必须在该实例内恰好命中一个 input port，映射采用全有或全无事务语义，
禁止部分替换。映射后的 group 保留父 statement 的 file/line/kind/predicate，只替换 RHS
signal index，因此 ambiguity 分类仍由统一逻辑决定，不产生第二套 output 专用分析器。

NBA 自引用还要求区分“没有非自身 RHS”与“只有控制语句”。Verilator emitter 会避免把
目标自身重复发布为 RHS，但仍以 `nba`、`proc_assign` 或 `cont_assign` 标明静态赋值类型；
xdebug-fst 因此在活动谓词已由 FST 值判真的前提下，将这类无可继续 RHS 的节点终止为
`assignment/constant_or_no_rhs_signal`，与冻结原版排除目标自引用后的合同一致。不能因为
剩余记录恰好只有 control role 就降级成 `control_only`。该判断来自 DesignDB assignment
kind，不是从 FST 值变化猜测 HDL 时序；Wellen 只提供 active time 和对应波形值。

NBA 的“最近赋值事件时间”与“最近值变化时间”必须继续分开。冻结原版 active trace 的
`activeTime` 来自赋值事件，即使连续多个 posedge 写入相同值也会前进；标准 FST 对普通
signal 只保存值变化，因此 Wellen 对不变值只能返回首次变化时间。第十八批等价固件在
20ps、40ps、60ps 均执行 `temporal_q <= data`，而 `data` 恒为 `8'h20`：65ps 查询时原版
语义要求 active time 为 60ps，原始 FST 对 `temporal_q/temporal_out` 本身只能证明 20ps。
Verilator `01f9f2a4b` 先以简单 posedge 和异步 reset 独立失败回归证明 statement-local
event-control 必要；`6239de45e` 随后只在既有 DesignDB emitter 中读取赋值祖先
`AstAlways` 的直接 `AstSenItem`，以 `event_posedge`、`event_negedge`、`event_bothedge`、
`event_changed` driver role 关联敏感信号。记录沿用既有字符串 ABI，不改变结构、版本、
capability、仿真调度、pass 顺序或普通仿真；非直接信号的复杂敏感表达式不近似发布。

xdebug 将同一 file/line/kind/predicate 的 `event_*` 与 RHS/control 聚为同一静态 statement，
只对 DesignDB 已明确命名的事件信号调用 Wellen。Wellen 仍直接访问当前 session 原始 `.fst`，
从目标查询时刻向前按需检查该信号的 Before/Raw 值并匹配边沿；action 在最近事件时刻求值
activation predicate。当前直接 `temporal_out = temporal_q` alias 会继承下游唯一 NBA 的
60ps 因果事件，下一 hop 也以 60ps 查询 `temporal_q`，随后沿静态 RHS 到 `top.data`。
DesignDB 决定“哪条语句、哪些事件源”，FST/Wellen 只提供“这些信号何时真实跳变”，action
决定 active-driver 时间与链语义；因此没有把 FST 升格为分析器，也没有固定周期、任意时钟、
同值扫描、转换、离线事件索引、全量波形快照或 fallback。

直接一层闭环后，第十九批用 `temporal_deep → temporal_mid → temporal_q(NBA)` 证明静态
连续链深度不能写死。修改前 DesignDB 已有两条唯一连续 RHS 与下游 `event_posedge(clk)`，
Wellen 也能读取原始 FST 中所有相关信号，但 consumer 只前看一层，故前三跳仍错误停在
20ps。后续实现只能在请求期间沿 DesignDB 唯一连续 RHS 做有界、带环检测的静态遍历，再
对明确事件源查询原始 FST；不能从波形值相等反向发现连接，不能产生持久事件索引。
最终 consumer 复用统一 statement 聚合与 predicate 求值，仅在活动 statement 唯一、类型为
`cont_assign` 且 RHS 唯一时递归；预算直接取请求 `max_nodes`，signal visited set 防止组合
环。到达唯一 NBA 的 `event_*` 才返回因果时间。未决谓词、多 statement、多 RHS、非连续
类型、缺失波形或环均失败关闭该时间传播路径，不改变既有驱动歧义报告，也不触发其他
backend。两级 alias 因而从同一原始 FST 得到 60ps，而 Verilator/Wellen 无需再修改。

纯自保持 NBA 是不同的时间语义：`q <= q` 虽在敏感事件执行，却不应覆盖此前真正提供数据
的 assignment。第三十七批不从 FST 的“目标没有变化”推断 self-hold，而是复用 DesignDB
已有 load 静态事实；只有 load 的 consumer 与 target 相同、文件/行与活动 NBA 完全一致，
且该源码行只有一个 statement identity，才安全认定为纯自保持。同源行条件/三元混合叶子
会因身份不唯一继续失败关闭，避免把常量叶子误判为 self-hold。action 随后以 `max_nodes`
为预算，逐个回看该 statement 已明确的 `event_*`，每个候选时刻仍由 Wellen 从当前原始
`.fst` 按需读取时钟 Before/Raw 值并重新求 activation predicate。65ps 查询因此跳过 60ps
的 `hold_q <= hold_q`，恢复 40ps 的 `hold_q <= data`；预算耗尽返回 `limit/max_nodes`。
Verilator/Wellen/XDD ABI 均未修改，也没有按值相等猜测、预扫事件或建立离线索引。

门控条件为假且没有 else assignment 时，时钟边沿不是目标赋值事件。第三十八批的
`gated_q` 在 40ps 执行数据 NBA，60ps 只有 posedge 而 predicate 为假；原始 FST 对目标的
最近实际观察点仍是 40ps，action 在该时刻组合 DesignDB predicate 后自然恢复数据来源。
这里不调用纯 self-hold 回溯，因为不存在活动的 `q <= q` statement。两类语义的区别来自
DesignDB 静态 statement/predicate 是否活动，FST/Wellen 仍只提供目标观察点和已指定时钟
边沿，不能用“值没变”把二者合并成同一波形启发式。

同一源码行的三元 self/data 叶子不能继续依赖 load 的 file/line：两片叶子行号相同但
predicate 不同。Verilator `986322540` 只在条件拆分后的 RHS 叶子本身恰好是赋值目标时，
通过既有 driver role 字符串附加 `self_rhs`，同时保留该叶子的 predicate；包含目标的
`q+1` 等计算表达式不发布该角色。xdebug statement 聚合天然按 predicate 分组，只把
`self_rhs` 当纯保持证明，不加入普通 RHS 或 X-origin 上游枚举。这样 45ps 可跳过 40ps
self 叶子并恢复 20ps data 叶子，而同源行常量/计算叶子不会被误判。该改动仅发生在显式
`--design-db` emitter，C ABI 函数签名、普通仿真和 Wellen 不变；Wellen 仍只对 DesignDB
已经指定的时钟从当前原始 `.fst` 按需读取边沿。

异步复位审计又把 `negedge async_reset_n` 安排在 30ps 的时钟下降沿，确保它不是 posedge 的
替身；复位 NBA 写入同值，使目标 FST 不产生 30ps 变化。DesignDB 同一 statement 的
`event_posedge(clk)` 与 `event_negedge(async_reset_n)` 都参与候选，action 选择原始 FST 中
最近真实事件 30ps，并在该时刻求值复位 predicate。该用例无需新增实现即通过，进一步
证明事件源选择来自静态敏感列表，事件发生来自 FST 运行时边沿，二者职责没有漂移。

混合 blocking/NBA 写入还要求按 SystemVerilog 调度区域而非静态记录数量判定最终驱动。同一
目标在同一时隙有且仅有一条已证明活动 NBA、且没有未决 NBA 时，该 NBA 在 NBA 区域覆盖
所有非 NBA 写入；多个活动 NBA 或任何未决 NBA 仍必须保持歧义。consumer 的统一归一化先
保留 force 优先级，再只在上述充分条件下筛除非 NBA statement，并同时用于单步、chain、
X-origin 和连续 alias 的事件时间前瞻。真实 `mixed_q = 8'h33; mixed_q <= data;` 固件证明
DesignDB 已有 proc_assign、nba、RHS 和 event_posedge 全部静态事实，无需扩展 Verilator；
Wellen 只读取原始 FST 中 60ps 的 clk 边沿，既不读取瞬态 blocking 写入，也不以最终值选择
语句。最终 chain 沿第 68 行 NBA 到 data，并把 60ps 因果时间传播到下游 alias。

force 审计证明静态 driver kind 也不能由 FST 值反推。Verilator `90d5aa2ae` 的失败回归先
锁定 `AstAssignForce` 被错误降级为 `proc_assign`，`07d076a82` 只复用既有 kind 字符串发布
`force`，不改变 ABI 布局、force lowering 或仿真调度。真实 FST 仍只呈现 force 后的运行时
值；当前 xdebug 在 force 与底层 NBA 同时活动时误报双 driver 歧义，而原版要求 force 优先
并终止。后续 consumer 必须依据 DesignDB kind 处理优先级，不能靠波形值猜测强制状态。
最终 chain 在统一 predicate 求值后只保留活动 force groups：一个 force 覆盖底层普通
assignment，RHS 不成为下一跳并以 `force` 终止；多个 force 仍按静态多候选报告歧义。
release 本身不伪装成 driver；release 后 force predicate 在最新事件时为 false，底层 NBA
重新成为活动 statement。这样 DesignDB 提供类型和条件，FST 只提供条件值与事件时刻，
action 实现原版优先级，职责仍严格分离。

`trace.x_origin` 使用独立 DFS，不能因为 chain 已支持 force 就视为自动兼容。第二十二批在
既有 GCD 原始 `.fst` 上仅增加测试 XDD 的 force 静态记录；修改前 action 把当前 X 信号
退化为 `candidate_x_source`。冻结原版要求活动 force 直接证明 `force_x` origin，且不得沿
force RHS。修复仍只消费 DesignDB kind 与原始 FST X 值，不增加波形或静态数据通道。
最终 DFS 在 predicate 求值后先查活动 force，命中即用 force statement 更新当前 hop 源码，
并生成 `kind=force/reason=force_x/evidence_status=proven` 的当前信号 origin；普通 unresolved、
RHS/control 枚举均不再执行。该优先级与冻结原版一致，同时保持 FST 只提供 X 值事实。
单步 `trace.active_driver` 另有独立响应投影，不能由 chain/X-origin 的修复代替。第二十三批
修改前真实查询仍把 force 和底层 NBA 同时作为普通路径，错误报告 assignment。公开合同
要求 termination 保留 force 且路径只来自活动 force statement；实现仍应复用同一
DesignDB group/predicate 结果，不扫描目标 FST 值辨认强制状态。
最终单步 handler 只在 predicate 已求值后筛选 force，存在活动 force 时仅投影 force
statements 并返回 `force/force`；多个 force 不合并，仍由公开 `max_results` 控制投影数量。
这与 chain 的终止和 X-origin 的 proven force_x 各自保持独立响应合同。

基础双连续多驱动暴露了一个不同层次的静态事实缺口：`V3Tristate` 为保持既有普通仿真
语义，会在 DesignDB emitter 运行前删除非首条同强度、非三态连续赋值。FST 只记录最终
运行时值，既不包含被删除的 HDL 语句，也不能证明该值由几条静态赋值共同驱动；因此绝不
允许由 Wellen 扫描 FST 去反推多驱动。Verilator `5c19377e3` 只在显式 `--design-db` 时，
于删除前保存目标局部名及声明位置、赋值源位置和 RHS 局部信号名组成的不可变描述；正常
scope/signal 表建立后，再按目标声明位置和实例前缀将其映射回既有 signal index，并复用
原有 driver/load ABI 附加 `cont_assign/rhs/predicate=1` 记录。描述保存不阻止 AST 删除，
不移动 pass，不改变仿真、XDD header、ABI 或 capability；实例映射同时要求局部名和声明
文件/行号匹配，避免同名信号串扰。

xdebug-fst 消费这些记录时仍按冻结 action 语义聚合静态 statement；目标 45ps 的运行时
观察点仍由 Wellen 直接从当前原始 `.fst` 取得。两条无条件 predicate 均为活动后，chain
依据两条静态 statement 返回 `ambiguous/multiple_active_candidates`，并报告语句数、RHS
信号数和源码行。这里 DesignDB 解决“有哪些 HDL 候选”，FST/Wellen 解决“当前波形事实是
什么”，xdebug action 解决“合同要求如何判定与报告”；三者职责没有合并，也没有新增
FST 转换、离线索引、全量快照或 fallback。

`case inside` 继续沿用这条职责边界。DesignDB 用内部 `==?i` 表示只允许 item 侧 X/Z
通配，并把 `AstInsideRange` 转成包含上下界的比较合取；default 静态谓词否定全部此前
item。xdebug 表达式求值器在 active time 用 Wellen 直接读取的 FST expression 值执行
这些运算，未知 LHS 不会像 casex 一样被误当通配。该扩展复用既有 predicate 字符串 ABI，
不新增 capability，也不把区间匹配下沉到 Wellen。

`case matches` 按独立证据逐层关闭。Verilator `adc193c2f` 先放行精确表达式 item；
`a5232efb6` 把直接顶层点星 wildcard 规范化为恒真的四态自比较；`7c4d19ee2` 再允许不含
绑定、嵌套 wildcard 或 tagged 节点的 assignment pattern，并在 Width 中用 case expression
dtype 复用既有 packed pattern 展开。`e5b1a28e2` 进一步只放行独立 `matches` 中同样不含
PatternVar、PatternStar 或 tagged 节点的精确标量和 assignment pattern，将其规范化为
`AstEqCase` 四态精确比较；它是 Verilator 前端的有限语言能力，不是 FST/Wellen 分析能力，
`5d4e40132` 再把独立 `matches` 的直接顶层点星规范化为一次 `==? 'x`，对任意四态值恒真且
保持左侧表达式恰好求值一次；该放行不递归进入 pattern。上述语法切片均不冒充已经完成
xdebug 动态 action 差分。普通仿真继续走既有 case lowering；DesignDB 在既有
predicate 字符串中发布展开后的 `===`，default
否定此前精确 item。xdebug 在 active time 仍通过 Wellen 直接读取当前原始 `.fst` 的 selector
并执行四态谓词求值，FST 不是 pattern 或 driver 分析器。Verilator `da63eb075` 进一步只
移除仅 `default` 形式必须存在精确 item 的误门禁；普通 lowering 和 DesignDB 都把该唯一
分支表示为恒真静态谓词。xdebug 在 45ps/65ps 仍按相同职责组合谓词与 Wellen 对当前原始
FST 的按需读取，没有让 FST 识别 `matches`。tagged union、tagged expression、tagged pattern、
pattern variable、嵌套 wildcard，以及带绑定或 tagged 的独立 `matches` 继续明确不支持，
不得把本批次描述成通用 pattern matching 已完成，也不得近似成 case inside。

条件 output 的跨层链继续复用上述双事实架构，而不要求扩展 Verilator。lowering 后的
activation predicate 可能引用不在 FST 中保存的内部 `__vcellinp__` 信号，但既有 DesignDB
同时保留该信号与声明 input port 的精确 `port_boundary`，原始 FST 也保留可直接读取的
模块端口。xdebug action 只有在遍历静态端口边后得到恰好一个位宽一致且 Wellen 可读的候选
时，才把表达式树中的内部名替换为端口名；零个或多个候选一律保持
`predicate_unresolved`，不会按名称相似度、值相等或第一个可读项选择。

进入子 output 后，父 net 的唯一活动 statement 若含 RHS，仍执行同实例 input 的全有或全无
映射；若该活动分支是常量、没有 RHS，则保留同一 statement 的 file/line/kind/predicate
作为子 output hop 证据并就地终止，不能退回父 net 形成 alias 环。45ps 信号分支因此沿
`conditional_output_bus → data_o → data_i → case_top.data → top.data` 到达 primary input，
25ps 常量分支在 `data_o` 的第 180 行终止。这里 DesignDB 决定端口等价关系和静态谓词，
Wellen 只从当前原始 `.fst` 按需读取 `sel_i` 与 hop 值，xdebug action 决定活动分支和终止
合同；没有 FST 转换、预扫、离线索引、全量快照或 fallback，也没有修改 Verilator/Wellen。

output 边界优先还有一个严格的多驱动否决条件。父 net 上恰好一个活动 statement 时，扁平化
RHS 与子 output 是同一语句的两层静态视图，可以先进入子实例；父 net 上存在两个及以上
活动 statement 时，它们可能分别来自子 output 与父模块赋值，必须在当前 net 直接按统一
合同报告 `multiple_active_candidates`。`output_mixed` 固件让第 13/21 行两个 RHS 在 5ps
故意相同，现有 DesignDB 已完整发布两条语句和端口边；修改前 consumer 错误清空歧义并在
alias 间形成环。最终修复只增加 `groups.size()==1` 门禁，不改变单 output 多 RHS或条件
output 行为。静态语句数量来自 DesignDB，FST 的相同值既不合并候选也不决定优先级。

显式文件产物必须与“离线 FST 分析”严格区分：

- `list.export` 按公共合同写出 `u64bin.v1`，用于调用者消费最终列表数据；
- `event.export` 按公共合同写出 JSON 事件结果；
- `nwave.rc.generate` 写出 nWave 视图脚本，并明确要求工具另行打开原始 FST。
- `axi.export` 与 `stream.export` 按公共合同写出用户指定的事务或传输表及 meta。

这些文件只在请求明确给出输出路径时写出，不参与 session.open，不被任何 action 自动重载，不是 file transport，也不允许在 Wellen 失败时充当 fallback。生产分析唯一波形事实源仍是会话中由 Wellen 直接按需读取的 `.fst`。因此“必须适配 FST 波形”和“允许公共 export action 产生最终结果文件”并不冲突：前者约束分析输入与事实来源，后者只是调用者显式要求的输出。

### interface/modport 成员边界为什么需要最小 Verilator 补充

原版 active-driver composite 语义要求链显式展示 sink/source 两侧的 interface 成员，例如
`parent output → sink output → sink.bus.data → shared bus.data → source.bus.data → parent input`。
仅有共享 `bus.data` 不足以回答它经由哪个实例、哪个 modport 和哪个方向跨过边界；零宽
interface port 本身也没有成员宽度或可采样值。原始 FST 确实保留
`u_sink.bus.data`、`u_source.bus.data` 和共享 `bus.data` alias，但这些 alias 只证明这些名称
在某时刻具有何值，不能证明 HDL 静态连接、modport 方向或 driver 归属。若通过扫描 FST
同值 alias 重建结构，就会违反 `GOAL-FST-DIRECT-001`，把 FST 错当分析引擎。

Verilator 的最小改动因此选在事实仍精确存在、但即将消失的时间点：`V3Scope` 已建立
`AstVarScope` 后，`V3LinkDot` 尚未删除 `AstAliasScope` 前。只有启用 `--design-db` 时才执行
只读 capture，记录 modport 实例端口、实际 interface scope、`AstModportVarRef` 成员和声明
方向；正常 AST 不被修改。最终 emitter 在既有 XDD v2 中追加波形可寻址的实例成员 signal，
用 `interface_modport_member` port connection 连接共享成员，并让 source/output 成员复用
共享成员已有的静态驱动描述。没有新增 ABI 函数、结构体字段或 capability，也没有移动普通
lowering、优化、调度、仿真和 FST trace 流程。修改前失败证据为 `ac880d295`，最小实现为
`d5f5b21fd`；11 个 XDD 回归和 3 个普通 interface/modport 回归通过。

xdebug consumer 不按名称猜边界。根/父 output 只有在谓词完全可解、恰好一个活动
`cont_assign`、且 DesignDB 连接给出唯一最深 output 时才进入子实例；从子 output 回映输入
时，允许已发布的 `<instance>.<interface-port>.<member>` 位于实例下一层，但仍要求与父 RHS
连接后只有一个候选。任何不唯一或不可读情况保持失败关闭；NBA/过程 assignment 继续使用
冻结终止语义。真实 30ps 六跳链已按上述规则闭环。

Wellen 对这一能力的要求只有波形侧事实：输入必须是原始 `.fst`，FST 必须保存 DesignDB 所
指向的实例成员 alias，且 Wellen 能按需 resolve/sample 每一个明确 hop；Wellen 不解析
SystemVerilog interface、不推断 modport、不枚举同值信号寻找结构，也不建立离线事件库或
全量快照。当前 fixture 直接用锁定 Verilator `--trace-fst --design-db` 同次生成，生产测试只
保留 599-byte `waves.fst`、最小 DesignDB `.so` 和 manifest。没有 VCD/JSON 转换、私有索引、
TCP、fileport 或 fallback。

`ref` 端口不需要新的 Verilator 静态事实。既有 XDD v2 已把声明发布为 direction=3，并给出
父 net、子 ref port、output 和 source 的连接/driver。xdebug 仅在 output 同实例 RHS 映射中
把 direction=3 与 input 一起作为候选，仍要求唯一；从 ref/inout 返回父 alias 后沿父 net
driver 继续，避免双向边反射回刚离开的 child output。Wellen 仍只对 DesignDB 选定的五个
FST 名称按需取值，不以值相等识别 ref。对应失败证据/固件/实现为 xdebug-fst `31b5ad2`、
`8b3e0f8`、`50ed974`，Verilator 和 Wellen 均未修改。

X-origin 对 modport 的基础预算验证不需要再修改三方架构。第五十四批复用现有 GCD 原始
`.fst`，以独立测试 DesignDB 将静态端口边的 kind 精确设为
`interface_modport_member`；Wellen 仍只按 action 选定的名称读取 0ps 四态值。consumer
继续把该 kind 投影为公开 `relation=port`，port hop 保留为物理路径证据，但不进入 RHS/
control 语义身份，并在 `max_nodes=6` 计数前合并汇聚探索态。结果是 `io_a` 与 `y` 两条
完整来源链且无 limitation。这里没有扫描 FST alias、按相同值推断 modport、预建 onset 表、
私有索引或离线数据库；也没有修改生产 consumer、Verilator、Wellen 或 XDD ABI。该测试
证明的是已发布 modport 静态边在 action 中的 alias/budget 语义，不等于已经覆盖任意真实
嵌套/数组 interface、ref X-origin、端口反馈或联合预算场景。

基础 ref 端口反馈沿用同一层次边界。DesignDB 发布三个 direction=3 信号和静态 port 环，
Wellen 只对这些已选名称从当前 GCD 原始 `.fst` 读取 X/onset；action 的路径 visited 才负责
判环。双向连接回到直接父节点只是 alias 反向边，必须忽略，否则合法 module/modport 边都会
产生假 loop；返回更早、非直接父节点的已访问 `(signal,onset)` 才进入既有 `loop_sources`。
第五十五批以三节点环证明最终响应保留物理 port hop、current 回到根节点、无 origin，并以
`loop_detected` 完整终止。初始两节点模型在相邻 modport 回归中暴露为过宽后已收紧，避免
为了让新测试通过而破坏普通 alias。该修复只在 consumer 中增加局部分类，不修改 Verilator、
Wellen、XDD ABI 或 FST 数据路径；带 driver/分支和联合预算的复杂反馈仍待验证。

反馈环的 node 预算仍由 action 请求内计数器承担。第五十六批在同一三节点 ref port 环上把
`max_nodes` 设为 2：root 与第二节点计数后，第三节点在追加 hop 和执行闭环判定前形成明确
frontier，响应为 `limit/max_nodes`、零 origin；默认预算则完整返回 loop。Wellen 没有为了
预算预扫 FST，也没有建立图或事件索引。该证据关闭基础 ref 反馈/node 组合，time/depth/
chain 与 driver/分支反馈的联合语义仍待差分。

### 9.8 ASan/UBSan 是构建门禁，不是波形后端

P7 第一批在顶层 CMake 增加互斥的 `XDEBUG_ENABLE_ASAN` 和 `XDEBUG_ENABLE_UBSAN`。开关在
所有 target 之前统一施加编译/链接参数，覆盖主程序、C++ 测试和运行时装载的测试 DesignDB
共享库；ASan 与 UBSan 使用独立绝对构建目录，避免混合运行时或 CMake cache 污染归因。
两套构建都继续链接同一冻结 Wellen C ABI、wellenx 和 Verilator DesignDB ABI，没有产生
sanitizer 专用 backend 或数据路径。

ASan 在 `detect_leaks=1` 与遇错即停配置下、UBSan 在 `halt_on_error=1` 和栈输出配置下，
分别通过 CTest 6/6 与 pytest 266/266。测试中的波形输入仍是原始 `.fst`：Wellen 只按 action
请求读取所需信号、时间和 delta，xdebug action 继续负责协议、筛选、推理和响应，DesignDB
继续只提供静态 HDL 事实。sanitizer 运行时不读取波形，也不允许借机引入 FST 转换、预扫、
私有索引、离线数据库、全量快照、export 回灌或 fallback，因此不改变
`GOAL-FST-DIRECT-001` 的架构边界。

宿主最初只有 GCC 8 linker script，缺少其指向的 `libasan.so.5.0.0` 与
`libubsan.so.1.0.0`。经用户授权安装 ABI 匹配且 GPG 验证通过的运行库后，保持 GCC 8.5、
相同源码、相同 fixture 和相同测试层级重新执行并通过。该环境修复不能被解释为功能修复，
也不覆盖仍待执行的并发、崩溃、重复生命周期和长期资源泄漏门禁。

### 9.9 Session 稳定性与波形分析职责隔离

P7 第二批把并发和资源门禁放在 frontend/UDS/engine 生命周期层，而不是 Wellen 或 action
分析层。8 个并行 frontend 共享同一个文件锁保护的 registry，但每个 session 拥有独立
generation、engine PID 和 UDS socket；并发 doctor 只通过对应 socket ping 对应 generation，
并发 close 只按 generation 清理自身记录和 artifact。长驻 stdio frontend 的 27 轮同名
open/doctor/close 则证明 generation 可更新、child 可回收、session id 可复用且父进程 FD 不
增长。既有 `SIGKILL → doctor unhealthy → gc` 用例继续证明异常 engine 不会被健康结果掩盖。

RSS 门禁区分普通 allocator 与 ASan quarantine：普通/UBSan 增长上限为 4 MiB，ASan 为
64 MiB，同时强制 LeakSanitizer；FD 在所有模式都要求计量前后精确相等。该差异只反映检测
器自身的内存保留策略，不更换 backend、fixture 或测试目标。普通 CTest 9/9、ASan 7/7、
UBSan 7/7 均通过。

所有 session 仍打开同一原始 `.fst`，engine 内部继续由 Wellen 按 action 请求读取波形。
registry、UDS、PID、FD 和 RSS 只管理进程与资源，不能发布 driver/load、X-origin、协议或
表达式分析事实；没有 FST 转换、预扫索引、离线数据库、全量快照、TCP/fileport 或 fallback。

### 9.10 Action 覆盖审计是证据索引，不是分析后端

P7 第三批在 pytest runner 边界增加可选 NDJSON trace。它只复制测试已经发送和收到的
xdebug.v1 JSON，并关联 pytest node；不接触 FST 文件、不调用 Wellen、不读取 DesignDB，也
不参与生产 dispatch。审计工具离线读取这份测试日志，按十类验收维度建立“观察到/缺失”
索引；生成文件只位于 `/tmp`，不作为 action 输入、缓存、export 或后续波形分析数据库。

第一份 925-event 基线证明 73/73 action 都有成功和非法请求运行证据，同时暴露资源缺失
18/73、空结果 9/73、边界时间 22/73、多结果 35/73、limits 20/73、truncation 16/73、
completeness 37/73、X/Z 7/73 的观察覆盖。数字只反映现有 pytest，不等于字段语义一致；
即使一个 action 十列都有记录，也仍须原版归一化差分。资源/时间无关 action 的 N/A 只能由
冻结 schema 与原版行为裁定，工具不会擅自缩小任务。

因此该 trace 与 FST 分析完全隔离：生产事实路径仍是原始 `.fst → Wellen 按需读取 → action`，
静态事实仍只来自 DesignDB；审计日志不得回灌 action，不得成为波形索引、离线数据库或
fallback。

### 9.11 Resource applicability 由 schema 与原版裁定

P7 第四批不再把十个维度机械套到所有 action。66 个 managed-resource action 用各自冻结
合法 example 替换成缺失 session，真实到达 frontend resource route 并统一返回
`SESSION_NOT_FOUND`；`session.open` 用缺失原始 `.fst` 返回 `WAVEFORM_OPEN_FAILED`。
这 67 项是 observed resource errors。

六个 `requires=none` action 的冻结 schema 在资源查找前禁止 `target.session_id`，只读原版与
候选完整响应 6/6 精确一致，因此 resource_missing 明确为 N/A。N/A 只保存在独立 applicability
清单并带差分工具证据，审计报告与 observed 分列；工具不能根据 category 猜测 N/A。

这一裁定仍不改变波形架构。缺失 session 发生在 frontend/registry，缺失 FST 发生在 session
资源打开边界；没有生成替代波形、切换 backend 或让 action trace 参与分析。运行时事实路径
仍只有原始 `.fst → Wellen 按需读取 → action`。

### 9.12 空结果与截断证据不得由字段名伪造

P7 第五批把 empty_result 的真实运行覆盖从 9 项提高到 19 项。新增 registry 空集合、空 signal
list、空 cursor、合法地址过滤零 transaction、合法 event 表达式零匹配和有效时间窗零 transfer；
每项都在成功响应中出现冻结 cardinality 字段与空数组，资源/schema 错误不能计入。

截断分类同时对齐冻结合同：`response_truncated=true`、非空 `truncation_scopes` 或显式 limit
终止才计为 truncation；普通 `limitations`（例如 design 不可用说明）不能单独计入。用 1019 次
public exchange 重算后 truncation 为 16/73。19/73 和 16/73 都只是 observed，剩余 action
仍须分别证明适用性或 N/A，不能由 schema 出现数组或 limit 字段自动裁定。

这些证据仍来自 action 对原始 FST 的真实执行；trace 只在 `/tmp` 做测试审计，不回灌 Wellen，
也不成为波形索引或离线分析数据库。

### 9.13 主结果集合与空查询语义继续收紧

P7 第六批进一步识别冻结合同中的 `total_count=0`、`found=false` 与 action 主结果集合；但明确
排除空 `issues`、`recommended_actions`、`constraints` 等辅助诊断/建议，避免把“配置成功且无
告警”伪装成“查询无结果”。count-only 响应若 `total_count>0, returned_count=0` 也不算空。

随后在原始 FST 上增加 AXI cursor 末尾、超高 latency 阈值、事务前 timeline/pair，stream
首个 transfer 前 query/export，以及首个采样边沿前 sampled-pulse/handshake。1046-event
trace 的 empty_result 为 33/73、boundary_time 24/73、multiple_results 42/73；truncation
仍为 16/73。数字仍是 observed，不是兼容率或 applicability 完成率。

本批没有建立波形副本或私有索引。所有空结果来自 Wellen 对当前 session 原始 `.fst` 的按需
扫描；DesignDB 和生产 action 未修改。

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
- revision `d5f5b21fdfbc02d20fba05b6571cabafc227f6ab`

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
- Verilator `c3abd3999`：建立 then/else activation predicate 缺失的修改前失败证据；
- Verilator `02f4a2259`：附加驱动谓词 capability 和只读访问器，并对无法精确表示的构造失败关闭；
- Verilator `a3adaaabb`：在同一谓词表中保留 `casez/casex` 四态通配类型，ABI 与 capability 不变；
- Verilator `ff0c1d016`：在 XDD 头文件中明确内部通配运算符语义，仅改注释；
- Verilator `a91524d63`：仅在 emitter 内拆分 V3Inst 合并的条件 RHS，恢复叶子行号和谓词；
- Verilator `5c19377e3`：仅为 `--design-db` 旁路保留 V3Tristate 删除的同强度连续多驱动静态描述，普通仿真与 XDD ABI 不变；
- Verilator `3e7cca4f1`：在既有谓词字符串中发布 case inside 的 item-side wildcard 与闭区间语义，普通仿真与 XDD ABI 不变；
- Verilator `007f1aa5c`、`8a5523487`：恢复 always-driven inout lowering 前原始 RHS，并替换而非叠加内部 strength 驱动；
- Verilator `ea1d3c9b4`、`adc193c2f`：先记录精确表达式 `case matches` 被无条件拒绝的普通仿真与 DesignDB 失败，再仅放行该有限子集并发布 `===` predicate；tagged/pattern 能力保持不支持；
- Verilator `01f9f2a4b`、`6239de45e`：先证明同值 NBA 缺少赋值事件源，再仅由 DesignDB emitter 发布赋值所在直接敏感信号的 `event_*` 静态角色；不修改仿真调度、ABI 布局或 pass 顺序；
- Verilator `90d5aa2ae`、`07d076a82`：先证明 force 被降级为普通赋值，再仅在既有 kind 字符串中恢复 `force` 类型；不改变 force/release lowering、仿真调度或 ABI 布局；
- Verilator `8623446e6`、`a5232efb6`：先证明顶层点星 pattern wildcard 被 LinkParse 明确拒绝，再仅将无绑定的直接 item wildcard 规范化为 case 表达式与自身的四态精确比较；源码顺序与 X/Z 恒真语义有独立普通仿真覆盖，嵌套 pattern、变量绑定和 tagged union 继续拒绝；
- Verilator `9cc890153`、`7c4d19ee2`：先证明 packed struct assignment pattern 仅被总括 LinkParse 门禁阻断，再允许无绑定 pattern 从 case expression 取得 dtype 并复用现有展开；位置式、成员命名式和 default 均有普通仿真覆盖，不扩展 tagged/binding 语义；
- Verilator `fab41bf9e`、`e5b1a28e2`：先证明独立 `matches` 的精确标量和 packed assignment pattern 被总括门禁拒绝，再只对不含绑定、通配和 tagged 节点的 RHS 复用 `AstEqCase` 四态精确比较；PatternVar、PatternStar、TaggedExpr 与 TaggedPattern 保持失败关闭，DesignDB header/ABI 未变；
- Verilator `1eb25de82`、`5d4e40132`：先证明独立 `matches` 的直接顶层点星仍被双重门禁拒绝，再用一次全 X RHS 通配比较实现恒真语义，并以带副作用函数验证左侧只求值一次；嵌套 wildcard、binding 和 tagged 继续失败关闭，DesignDB header/ABI 未变；
- Verilator `1ae90d55d`、`487482500`、`da63eb075`：先以普通仿真和 DesignDB 锁定仅 `default` 的 `case matches` 被总括门禁拒绝，并隔离独立四态函数参数限制；随后只取消“至少一个精确 item”的要求，保留 tagged、binding 与嵌套 wildcard 的失败关闭，普通 lowering、XDD header/ABI 和 Wellen 均不变；
- Verilator `b3ed369d7`、`986322540`：先证明同源行三元 self/data 叶子缺少 predicate-local 自引用事实，再仅为直接 self 叶子发布 `self_rhs` 角色；它不是上游数据依赖，`q+1` 不误标，普通仿真与 C ABI 函数签名不变；
- Verilator `ac880d295`、`d5f5b21fd`：先证明 DesignDB 缺少实例侧 modport 成员边界，再仅在 `--design-db` 下于 LinkDot 前只读捕获 interface alias，并通过既有 XDD v2 表追加成员 signal/connection/driver；AST、普通仿真、FST 生成和 ABI/capability 不变；
- xdebug-fst `9a529cc`：统一 wellenx 与 Wellen 的信号句柄编码；
- xdebug-fst `5b2595a`：锁定 Wellen 与 Verilator 兼容版本。
- xdebug-fst `f61670a`：补齐 FST delta、观察点、批量游标与扫描完整性；
- xdebug-fst `e1779c9`：建立生产与回归 FST-only 硬门禁。
- xdebug-fst `0188cd0`：锁定并严格消费 DesignDB driver predicate capability；
- xdebug-fst `1529647`：修复 predicate 所需的括号逻辑与四态 X/Z 语义；
- xdebug-fst `e2870e9`：在 active time 直接读取 FST 控制值并筛选 active-driver、chain 与 X-origin 分支。
- xdebug-fst `a0b73bc`：以 APB 嵌套条件和普通 case/default 的真实 FST 回归扩展已证能力边界；
- xdebug-fst `2815e61`：建立 casez/casex 静态匹配种类缺失的修改前失败证据；
- xdebug-fst `9eac4a5`：按 `==?z`/`==?x` 与 Wellen 四态 FST 值判定 wildcard case 分支；
- xdebug-fst `ec8323c`：建立 lowering 后同目标嵌套条件身份丢失的修改前失败证据；
- xdebug-fst `5e64ec1`：锁定 Verilator 条件叶子恢复 revision 并通过全量门禁；
- xdebug-fst `ffbc888`：建立同源行三元信号/常量叶子被错误合并的修改前失败证据；
- xdebug-fst `1119706`：将 predicate 纳入语句身份并以真实 FST 选择同源行三元叶子；
- xdebug-fst `09fd61a`：建立内部 input alias 沿对称端口边反向折返的修改前失败证据；
- xdebug-fst `4a1a4c0`：只允许 input 端口向较浅父级上溯并在 primary input 终止；
- xdebug-fst `72ede46`：建立 NBA 目标自引用被误判为 control-only 的修改前失败证据；
- xdebug-fst `97ed236`：按静态赋值 kind 对无非自身 RHS 的 NBA 返回 assignment 终止。
- xdebug-fst `9e6dbbe`：锁定 Verilator 多驱动修复并以真实 FST/DesignDB 组合恢复双连续活动候选歧义。
- xdebug-fst `6d5adcd`：以 RHS-only 四态通配与闭区间求值消费 case inside 谓词，运行时值仍直接来自原始 FST。
- xdebug-fst `d67ef94`：锁定精确表达式 `case matches` 的有限 Verilator 子集，并以独立原始 FST 在 active time 验证 item/default；FST 只提供 selector 波形事实。
- xdebug-fst `2f56de3`、`3c34f7b`：先记录独立精确/顶层通配 `matches` 尚未进入固件的 `SIGNAL_NOT_FOUND` 失败，再用锁定 Verilator 同步生成原始 FST 与 DesignDB；45ps/65ps 精确真/假和顶层点星恒真均由 action 组合静态谓词与 Wellen 按需波形事实完成，三方实现和 ABI 无需再改；
- xdebug-fst `116761c`、`519e7e9`：先记录同一 posedge 过程连续两条 NBA 尚未进入固件的失败，再以同步原始 FST/DesignDB 证明两条 assignment handle 在 60ps 同时活动；action 按冻结原版合同保留第 88/89 行双候选歧义，不按源码顺序或最终值任选，三方实现和 ABI 无需修改；
- xdebug-fst `3654e70`、`54f332b`：先记录同一子实例两个 output 端口共同驱动父 net 尚未进入固件的失败，再以同步原始 FST/DesignDB 验证同实例端口 identity；action 保留第 160/161 行两条活动 statement，不按端口顺序、FST 值或可读性合并候选，三方实现和 ABI 无需修改；
- xdebug-fst `b46b5cd`、`b6f2617`：先记录条件 output 未进入固件的失败，再同步原始 FST/DesignDB 并仅在 consumer 内以唯一、同宽、可读的 DesignDB 端口边解析 lowering 谓词信号；信号分支跨 input 上溯，常量分支保留第 180 行终止，Verilator/Wellen/XDD ABI 均未修改；
- xdebug-fst `1fb4532`、`4cd2214`：以独立原始 FST/DesignDB 固件先证明父子 output 两条活动赋值被唯一边界错误覆盖，再仅将边界优先收紧到单一活动 statement；相同 FST 值不合并静态候选，Verilator/Wellen/XDD ABI 均未修改；
- xdebug-fst `813ee36`、`f8d0aee`：先以旧固件的 `SIGNAL_NOT_FOUND` 保存仅 `default` 的 `case matches` 动态缺口，再用干净 Verilator `da63eb075` 同步刷新原始 FST 与 DesignDB；action 无修改即在 45ps/65ps 唯一返回第 95 行，证明分析仍由 DesignDB 静态谓词和冻结 action 承担，Wellen/FST 只提供按需运行时事实；
- xdebug-fst `282ed4d`、`e700d34`：先保存 NBA 纯自保持固件缺口，再同步原始 FST/DesignDB 暴露 60ps self-hold 错误覆盖 40ps 数据赋值；consumer 只凭精确且唯一的 DesignDB self load 证明做有界事件回溯，Wellen 仍只按需读取当前 FST 时钟事实，Verilator/Wellen/XDD ABI 未修改；
- xdebug-fst `df90d59`、`d2f3f74`：以无 else 的门控 NBA 区分“时钟发生但 statement 未赋值”和活动 self-hold；同步原始 FST/DesignDB 后现有 action 直接保留 40ps 数据来源，三方实现与 ABI 无需修改；
- xdebug-fst `e5da919`、`22e810d`：先保存同源行三元 self-hold 动态缺口，再锁定 predicate-local `self_rhs` 并同步原始 FST/DesignDB；action 跳过 40ps self 叶子、恢复 20ps data 叶子，Wellen 只提供按需边沿事实；
- xdebug-fst `022d316`：锁定 inout lowering 原始 RHS 替换语义，并以真实 FST 完成跨端口四跳链。
- xdebug-fst `fcd5e06`：以带独立中间 net 的真实两级 inout 固件验证六跳父向链，三方实现和 ABI 均无需修改。
- xdebug-fst `7e07599`、`970aae1`：冻结 output 边界折叠失败，并仅组合既有 XDD 端口/驱动事实恢复原版五跳模块链。
- xdebug-fst `bac0886`：以原始 FST/DesignDB 组合验证双条件过程 NBA 的唯一活动和双活动歧义，无实现或 ABI 修改。
- xdebug-fst `888de09`、`fc27f1d`：冻结同源行跨实例 output driver 误合并，并以 consumer-only 静态实例 identity 恢复两条活动候选。
- xdebug-fst `7c5b0a2`、`f3b5143`：冻结常量 NBA hop 源行丢失，并在不沿 control 追踪的前提下恢复活动 assignment 源码证据。
- xdebug-fst `ae76785`、`07bb94c`：冻结复杂 output 在父 net 过早报告多 RHS，并恢复 child output 边界与同实例 input evidence。
- xdebug-fst `7b99803`、`29f70d2`、`43f8a0b`：先冻结 interface/modport 跨边界失败，再锁定最小 Verilator 静态事实与原始 FST 固件，最后用唯一连续 output/成员连接恢复六跳 sink/shared/source 链；Wellen 只按需读取已选 FST alias。
- xdebug-fst `31b5ad2`、`8b3e0f8`、`50ed974`：先冻结 ref 固件缺口，再用锁定 Verilator 原始 FST/DesignDB 暴露 direction=3 被忽略和 alias 反射假环，最后以 consumer-only 唯一映射恢复五跳链。
