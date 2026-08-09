# xdebug_oc 全能力兼容实施进度

## 当前状态

- Goal：active（thread `019fe602-0198-7f23-a9a1-bb3c6a539dec`）
- Goal 永久门禁：`GOAL-FST-DIRECT-001`；当前 session 原始 `.fst` → Wellen 按需访问 → action 查询/推理，是唯一允许的波形事实路径。该门禁已写入 Goal 权威任务书和架构文档，并作为每批提交审查及最终 `complete` 的否决条件
- 当前阶段：P5 功能批次已覆盖；P6 进行中（Active Driver 与 X Origin）
- 当前任务：继续补齐 P6 的 tagged/pattern matches、更多 NBA/常量组合、复杂 output/inout alias，以及混合类型和调度边界多驱动原版差分；当前 counter `if/else`、APB 嵌套条件、普通 `case/default`、`casez/casex`、`case inside` pattern/range、精确表达式 `case matches`、lowering 后条件、同源行三元分支、嵌套常量 NBA、input/inout alias 上溯、基本多级 inout、基本 output 模块边界、NBA self-RHS 分类、基础双连续、双条件过程及跨实例 output 多驱动歧义已完成，但不得据此宣称 P6 全部关闭
- 全局硬门禁：生产、回归和最终验收只打开 FST 波形；VCD 仅可作为可重复生成 FST 的源文件，禁止作为输入或 fallback
- 2026-08-10 用户再次确认：项目只需要并且也必须完整适配 FST 波形；唯一波形事实路径是当前 session 中由 Wellen 直接按需读取原始 `.fst`。不得建立独立“FST 分析”数据库，不得转成 VCD/JSON/私有索引/离线库/全量内存快照后分析；显式 export 产物永不回灌。该约束已写入 Goal 权威任务书，覆盖旧 Goal 或历史文档中的相反表述
- 2026-08-10 用户进一步锁定职责边界：FST 只是唯一波形输入容器，Wellen 只是保真按需访问层；分析能力属于冻结的 xdebug action 语义及其与 Verilator DesignDB 静态事实的组合。禁止把“必须适配 FST”偷换成“由 FST 自身做分析”，也禁止据此简化 driver/load、active-driver、chain、X-origin、协议、表达式、完整性或错误合同。该定义已加入任务书与 Goal 权威附件，作为逐批门禁和 Goal 完成否决项
- 2026-08-10 Goal 防漂移再确认：用户原意“不得退化到用 FST 做分析，只需要并且也必须适配 FST 波形”已写入 `XDEBUG_FULL_PARITY_GOAL_LOCK.md` 的独立解释锁。今后上下文压缩、阶段切换和交接均须同时保留“FST-only 输入”与“FST 非分析引擎”两项，不得只保留前半句造成架构漂移
- 2026-08-10 漂移复核：修正架构图遗留的 `FST/VCD/GHW` 输入表述为仅 `原始 .fst`，并将 `GOAL-FST-DIRECT-001` 加入 P0–P7 持续检查与 Goal 完成否决项
- xdebug-fst 当前功能与验收提交：`d67ef94`；精确表达式 `case matches` 的 Verilator 修改前证据为 `ea1d3c9b4`、最小实现为 `adc193c2f`；同值 NBA 事件依赖的 Verilator 修改前证据为 `01f9f2a4b`、最小实现为 `6239de45e`；复杂 output 表达式修改前失败证据为 `ae76785`，常量 NBA 修复为 `f3b5143`；过程/常量、多级端口、固件、架构说明与全量门禁证据均已登记
- Wellen 分支：`feature/xdebug-fst-capi`，冻结 revision `066d86ad26e82ae02407ad2a64c5a226b8ebe212`
- Verilator 分支：`feature/design-db-for-xdebug`，冻结 revision `6239de45ef94f88e5b3f4efa4e78356741d58bdc`
- 原版 xdebug runtime revision：`8eecf71271cc523d93bf03f6b9f9b6fa04ed3ee8`
- 原版 xdebug runtime build ID：`8eecf71271cc-c45099040abf3dbe194d3ba27c207d7637b39ba9f9d662fad3d9d50dda99fb2c`
- 原版 schema revision：`c45099040abf3dbe194d3ba27c207d7637b39ba9f9d662fad3d9d50dda99fb2c`
- 冻结 action 数量：严格 73 个；schema 文件：282 个，其中 public action schema：146 个

## 阶段状态

| 阶段 | 状态 | Tag | 验收摘要 |
| --- | --- | --- | --- |
| P0 | 已完成 | `parity-p0` | 基线、依赖锁、Wellen/Verilator 独立回归和漂移检查均通过 |
| P1 | 已完成 | `parity-p1` | 73 action、146 schema、请求/响应校验、canonical JSON/XOUT 与 stdio-loop 全部对齐 |
| P2 | 已完成 | `parity-p2` | registry、真实 UDS engine、来源清单、批量生命周期、异常补偿及 MCP direct/fake-LSF 全部通过；TCP/file 按用户要求裁剪且无 fallback |
| P3 | 已完成 | `parity-p3` | FST-only Wellen 层级、时间、类型、delta、采样、批量与完整性已实现；Rust/C/C++、7 CTest、基线与双 C ABI 门禁全部通过 |
| P4 | 已完成 | `parity-p4` | 两组修改前失败证据后，仅增加 ABI/capability、声明方向、预计算端口边和 driver dependency role；8 个 XDD 与普通回归通过 |
| P5 | 功能批次已覆盖 | `parity-p5` | 发现/静态设计、value/list/event/cursor/RC/expr、signal、verify/window、counter/pulse/handshake、APB、AXI、stream 与 combined 基础合同已迁移；阶段总差分仍随 P6/P7 复核 |
| P6 | 进行中 | `parity-p6` | 已完成多分支 X DFS、时间/限制证据、driver role，以及 counter if/else、APB 嵌套条件、普通 case/default、casez/casex 与 lowering 后同目标嵌套条件的 FST 运行时谓词选择；复杂控制与原版差分继续 |
| P7 | 未开始 | `parity-p7` | 全量差分与最终交付 |

## Commit 记录

- `00b5d9f`：建立完整任务书、阶段边界和分批提交规则。
- `630c436`：冻结原版 xdebug v1 runtime、engine、schema、73 action catalog 与许可证边界。
- Wellen `dba5242`：建立可测试的 Wellen C 波形访问接口。
- Wellen `1d66a9e`：修复 1 基 signal reference 并增加 Rust/C ABI 端到端验证。
- Verilator `80c4226ae`：增加显式、只读、最小化 DesignDB 生成接口。
- Verilator `e04eb0ea8`：修复 DesignDB 全量回归隔离并覆盖七类设计。
- `9a529cc`：统一 `wellenx_capi` 与 Wellen C ABI 的 1 基信号句柄编码。
- `5b2595a`：锁定 Wellen、Verilator revision、ABI header 和 release library。
- `fad3309`：说明 Verilator DesignDB、Wellen 波形后端和 xdebug-fst 组合架构。
- `2f04051`：建立冻结文件、依赖、catalog、schema 和现场原版的自动漂移检查。
- `ce1a300`：迁移 MIT JSON Schema validator、runtime validator 和诊断错误核心。
- `89f740b`：迁移严格 73 action 公共注册表、完整 metadata、过滤和 modes。
- `a97a01b`：统一 canonical response/error envelope，并冻结 215 个 schema 引用示例。
- `eb00b49`：在 handler 前执行 action-specific 严格请求校验，batch 子请求同样 fail closed。
- `bf5c7fc`：在输出前执行响应合同校验，并遍历验证全部冻结请求/响应示例。
- `8994fc6`：对齐 one-shot JSON、默认 XOUT、退出码和 stdio-loop wire protocol。
- `f67e6f9`：迁移多会话注册表、严格 endpoint 合同与 generation 生命周期状态机。
- `42876c0`：实现真实 UDS engine 子进程、公开 session 路由与 generation-safe 资源释放。
- `d5d77ea`：按用户范围裁剪 TCP/file transport，并登记明确拒绝且无 fallback 的门禁。
- `cb024c9`：对齐 session doctor、idle 回收、崩溃清理、fingerprint 复检与 cleanup_failed 补偿。
- `b4a579e`：覆盖 UDS 会话启动、并发、崩溃、资源变化、超时与 stdio 父子进程生命周期。
- `0d7a3ed`：建立真实 xverif MCP direct 与 fake-LSF 生命周期回归。
- `f4a0713`：实现 run manifest 来源校验、同资源 advisory 与 close/kill all 批量语义。
- `a7af5e2`：覆盖 waveform/combined manifest、来源错配和批量会话清理。
- Wellen `c5132ef`：区分根层级和递归层级，发布 scope full name。
- Wellen `76e5077`：通过 C ABI 发布 FST timescale。
- Wellen `066d86a`：保真发布 bit-vector、real、UTF-8 string 与 event 类型和值。
- `be9b36b`、`5edeafc`：统一 Wellen 信号句柄、递归层级、alias 消歧及其回归。
- `0507cb1`、`5061ef9`：实现并验证严格 timescale 时间解析与渲染。
- `974bad8`、`e45eb6a`：实现并验证四态及非位值类型。
- `f61670a`：实现 FST delta、raw/before/after、时钟采样、类型化批量值、变化游标和范围扫描。
- `e1779c9`：建立生产与测试 FST-only 门禁并覆盖真实 FST 边界。
- Verilator `a1f1aba1c`：导出声明方向与预计算跨层端口连接。
- Verilator `6aae8d201`：覆盖 ABI v2、方向、端口边及旧接口兼容。
- `d122c43`：要求 XDD v2 原生方向和连接证据并移除全表推断。
- `d4f4da4`：覆盖旧 bundle 拒绝和 XDD v2 消费。
- Verilator `50d8fff59`：以附加访问器区分 RHS、control 与 statement 依赖角色。
- `4ab367d`：锁定最新 XDD 并消费原生 dependency role。
- `7670e6c`：覆盖同一目标的数据依赖与控制依赖角色。
- `a13e2ef`、`3dfa01d`：对齐并验证发现与静态设计 action。
- `035c835`、`2b2f572`：实现并验证 `value.at` 的 signal/list/APB/AXI/stream、多时间点、时钟采样、X/Z 和严格物理时间。
- `4b42139` 至 `23dd725`：对齐 list 管理、首次差异、预览和 `u64bin.v1` 导出。
- `a7d42ea`、`17c1027`：对齐 cursor 的物理时间、活动状态与元数据。
- `11ecbfc` 至 `04c3da2`：对齐 event 配置、时钟采样表达式查询、聚合与 JSON 导出。
- `da274c6`、`ee37acd`：按正式配置生成并验证 nWave RC 视图脚本。
- `10aa35f`、`5c3771d`：对齐并验证 `expr.eval_at` 的 alias、严格物理时间、clock observation point、operand evidence 与 clock context。
- `0a5675f`、`18cc98f`：对齐并验证 `signal.changes` 的 timeline/summary、基线行、类型化值、真实 transition count 与响应裁剪。
- `d895fac`、`798dcc2`：对齐并验证 `signal.statistics/stability/xz_verify/anomaly.inspect` 的 raw/clock、多信号检查、决定性早停、分析预算与证据裁剪。
- `e531d78`、`f9a0a75`：对齐并验证 counter、sampled pulse 与 valid-ready handshake 的表达式有效条件、拼接计数器、未采样脉冲、payload 风险、stall/data/valid-hold 规则与双层裁剪。
- `0b6a831`、`5c94e86`：对齐并验证 APB 命名配置、时钟采样事务、query/statistics/cursor/window 六项合同及 `value.at(apb)` 值源集成。
- `0e17e7a`、`a12599a`：对齐并验证 AXI 命名配置、五通道事务重建、query/statistics/cursor、latency/osd/pending、stall/outlier/outstanding/pair/export 及 `value.at(axi)` 值源集成。
- `8091689`、`f2f54ff`：对齐并验证 `verify.conditions/window.verify` 的 alias 表达式、clock context、三态结果、always/eventually/never、决定性早停与完整性。
- `57c4603`：建立 counter 同一 reset 控制记录无法区分 then/else 的 active-driver 修改前失败证据。
- Verilator `c3abd3999`：在自身 P4 回归中固化 driver predicate 缺失失败。
- Verilator `02f4a2259`：保持 ABI v2 和既有函数签名，仅附加 activation predicate capability/访问器；8/8 DesignDB 与 2/2 分发门禁通过。
- Verilator `a3adaaabb`：在既有 predicate 字符串内增加内部 `==?z`/`==?x`，精确保留 casez/casex 匹配种类；ABI v2、capability、公开记录布局和普通仿真行为均不变，8/8 DesignDB、2/2 分发门禁与源码构建通过。
- Verilator `ff0c1d016`：仅在 `xdd_trace_driver_predicate` 注释中固化 `==?z`/`==?x` 的消费者语义；函数签名和二进制 ABI 不变，更新 XDD header hash 锁。
- Verilator `a91524d63`：递归拆分 V3Inst 合并的 `AstCond` RHS，恢复各叶子的源位置、RHS/control role 和 activation predicate；不移动 pass、不修改 V3Inst/调度/仿真、不扩展 ABI，8/8 DesignDB 与 2/2 分发门禁通过。
- Verilator `5c19377e3`：仅在 `--design-db` 下于 V3Tristate 删除同强度非三态连续赋值前保存不可变静态描述，并在既有 scope/signal 表建立后通过原 driver/load ABI 重新关联；普通仿真、AST 删除、pass 顺序、ABI/capability 均不变，8/8 XDD、2/2 分发门禁与源码构建通过。
- Verilator `3e7cca4f1`：在既有 predicate 字符串表中以 `==?i` 保留 case inside 的 item-side wildcard，并将 `AstInsideRange` 展开为闭区间上下界；普通仿真、case lowering、ABI/capability 均不变，8/8 XDD、2/2 分发门禁与源码构建通过。
- Verilator `007f1aa5c`、`8a5523487`：先保存 always-driven inout net lowering 前的原始 RHS，再将该“替换型”描述与普通多驱动“追加型”描述分离，只替换同目标的内部 `__strong` driver/load；普通 inout/tristate 仿真、AST lowering、ABI/capability 均不变，8/8 XDD、2/2 分发门禁与源码构建通过。
- `0188cd0`：锁定新 XDD header/revision，严格要求 predicate capability，并仅刷新五组 DesignDB 静态 fixture；所有 FST 保持原样。
- `1529647`：修复括号内逻辑解析及 `!`、`&&`、`||` 的四态 X/Z 传播。
- `e2870e9`：在 active time 由 Wellen 直接读取原始 FST 控制值，筛选 active-driver、chain 与 X-origin 语句；不可判定时 unresolved，不回退静态首项。
- `a0b73bc`：以真实 APB 和普通 case FST 固化嵌套条件、NBA、数组 RHS、端口归一化及 item/default 活跃分支。
- `2815e61`：建立 casez/casex predicate 缺失时 `analysis_complete=false` 的修改前失败证据。
- `9eac4a5`：消费 XDD `==?z`/`==?x`，按 Wellen 直接读取的四态 FST 值精确选择 casez/casex 分支，并更新依赖锁。
- `9e6dbbe`：锁定 Verilator 多驱动静态事实修复，刷新真实 case FST/DesignDB 固件，并按原版合同返回双连续活动候选歧义；全量 pytest 218/218、CTest 8/8 与基线门禁通过。
- `6d5adcd`：消费 DesignDB 的 case inside pattern/range 谓词，以 Wellen 直接读取的真实 FST 值选择分支；全量 pytest 219/219、CTest 8/8 与基线门禁通过。
- `022d316`：锁定 inout lowering 原始 RHS 替换修复，以真实 FST 完成子端口到父级 primary input 的四跳链；全量 pytest 220/220、CTest 8/8 与基线门禁通过。
- `fcd5e06`：在不修改实现的前提下，以真实两级 inout 网络和原始 FST/DesignDB 组合证明六跳父向链；全量 pytest 221/221、CTest 8/8 与基线门禁通过。
- `7e07599`、`970aae1`：先冻结 output 模块边界被 lowering 折叠的失败，再仅组合既有 DesignDB 静态事实恢复五跳链；全量 pytest 222/222、CTest 8/8 与基线门禁通过，Verilator/Wellen 未修改。
- `bac0886`：以真实 FST 覆盖双条件过程 NBA 的唯一活动与双活动歧义；全量 pytest 224/224、CTest 8/8 与基线门禁通过，三方实现和 ABI 无需修改。
- `888de09`、`fc27f1d`：先冻结同源行跨实例 output driver 被合并的失败，再以 consumer-only 实例 identity 恢复两条活动候选；全量 pytest 225/225、CTest 8/8 与基线门禁通过，XDD ABI、Verilator/Wellen 未修改。
- `7c5b0a2`、`f3b5143`：先冻结常量 NBA hop 源行丢失，再保留无 RHS 的活动 NBA statement 且禁止沿 control 继续；全量 pytest 226/226、CTest 8/8 与基线门禁通过，XDD ABI、Verilator/Wellen 未修改。
- `ae76785`、`07bb94c`：先冻结复杂 output 双 RHS 在父 net 过早歧义，再恢复 child output hop 与同实例 input evidence；全量 pytest 227/227、CTest 8/8 与基线门禁通过，XDD ABI、Verilator/Wellen 未修改。

## 测试记录

- Wellen：`cargo test --workspace --all-targets` 通过；核心单元、diff、stream 和新增 C API 测试均无失败。
- Wellen C ABI：`make -C wellen_capi/test` 通过，真实覆盖首个原生 `SignalRef(0)` 对应 C reference 1。
- `wellenx_capi`：`cargo test --manifest-path wellenx_capi/Cargo.toml --all-targets`，2 个测试通过。
- Verilator：7 个相互隔离的 DesignDB 回归全部真实执行通过，未发生共享 prefix/obj_dir 或 skip 冒充通过。
- Verilator：distribution copyright/license 检查通过，`make -C src -j2` 通过。
- xdebug-fst：`cmake -S . -B build && cmake --build build -j2` 通过，配置阶段成功校验全部依赖锁。
- xdebug-fst：`python3 tools/check_compat_baseline.py --original-root ${XDEBUG_ORIGINAL_ROOT}` 通过，确认冻结文件、catalog、schema、依赖和现场原版均未漂移。
- xdebug-fst：`${XFST_CONDA_ENV} -m pytest tests/ -p no:xverif -q`，122 个测试通过。
- P1 CTest：真实加载 105 个 request 示例和 110 个 response 示例，215 个示例逐一通过对应 runtime schema；同时验证冻结原版 actions response。
- P1 protocol：`python3 tools/check_p1_protocol_parity.py --original-root ${XDEBUG_ORIGINAL_ROOT}` 通过，原版与 xdebug-fst 的 73 action catalog、73×2=146 个 schema action 完整 JSON 响应完全一致。
- P1 CLI：one-shot JSON、actions/schema/error XOUT、INVALID_JSON、退出码、stdio-loop ready、payload override、错误双载荷和 quit envelope 与原版比较一致；PID 是唯一易变字段。
- P1 pytest：13 项 CLI/request/catalog 专项测试通过；common/waveform 中已改用正式公共合同的 21 项基础测试在启用 response gate 前通过。
- P2 registry CTest：覆盖 opening→active CAS、重复名称、双 registry 实例、单调 touch、generation mismatch、条件删除和严格 endpoint round-trip。
- P2 UDS CTest：真实 fork server/client 往返通过，覆盖 `0600` socket、换行 JSON framing、非法 JSON 拒绝、listener 隔离、非正 timeout 拒绝和断连写入不触发 SIGPIPE。
- P2 lifecycle CTest：真实 `xdebug-fst --server` 子进程覆盖 TCP/file 明确拒绝且无 fallback、非法 timeout 环境、严格私有控制合同、waveform 与 combined run manifest、来源摘要不一致证据、同资源 advisory、重复名称、双前端并发 open、close/kill all、启动提前退出补偿、公开 action 经 UDS 路由、ownership token mismatch 保活、正确 token kill、engine SIGKILL 后 doctor/gc、FST fingerprint 变化、idle list 回收，以及同一 stdio-loop 内 open/close 的 child reap；最终 socket 与 active registry 均清空。
- P2 MCP direct CTest：真实加载 xverif MCP adapter，使用当前 `xdebug-fst` 完成 73 action one-shot catalog、managed stdio `open/doctor/list`、公开 action UDS 路由和 `close`，wrapper 与 native registry 均清空。
- P2 fake-LSF CTest：使用 xverif 自带 fake bsub/bkill 和当前 `xdebug-fst`，真实覆盖 stdout scheduler noise、job id 识别、managed `open/doctor/close`、bkill 日志与 native registry 清空；未调用真实 LSF，也未切换 backend。
- P3 Wellen：`cargo test -p wellen-capi` 3 项通过，C ABI `make` 通过；根/递归层级、1ns timescale、bit/string/real/event 类型化值均有真实断言。
- P3 xdebug-fst：7/7 CTest 通过；`wellen-fst-backend` 的所有实际波形参数均以 `.fst` 结尾，真实覆盖 1ns/1ps、深层 leaf、alias、64 位 X、Z、UTF-8 string delta、real、event、raw/before/after、时钟采样、批量 load/unload/value、变化游标、范围扫描和完整性诊断。
- P3 阶段总验收：Wellen Rust 3/3、Wellen C ABI、wellenx 2/2、xdebug-fst 7/7 CTest 和 `check_compat_baseline.py` 全部通过；xdebug-fst、Wellen、Verilator 三仓状态均干净，Verilator 仍停留在 `e04eb0ea8`，P3 未对其增加任何修改。
- P4 Verilator：`make -C src -j2` 通过；`t_xdd_trace_simple/full/uart/metadata/ops/trace`、`t_xdd_p3`、`t_xdd_p4` 共 8 个 DesignDB 用例逐一通过；普通非 DesignDB `t_a1_first_cc` 通过。
- P4 xdebug-fst：依赖 revision/header hash 配置门禁通过，构建通过，8/8 CTest 通过；旧 XDD bundle、缺 capability 或缺符号均 fail closed，方向、端口边和 driver role 均使用原生事实。
- P4 基线：`check_compat_baseline.py --original-root ${XDEBUG_ORIGINAL_ROOT}` 通过；五组 DesignDB 固件已重生成，但所有 `.fst` 文件保持未修改，测试继续由 Wellen 直接按需打开 FST。
- P5 已完成批次：发现/静态设计 13 项、`value.at` 16 项、list/event/cursor/RC 22 项专项 pytest 均通过；8/8 CTest 持续通过。所有输入波形路径均以 `.fst` 结尾。
- P5 signal 分析批次：`tests/test_signal_analysis.py` 14/14 通过，覆盖 raw statistics 精确变化数、clock sample contract、`line_limit` 仅裁剪证据、`max_samples` 标记分析不完整、stability 首变化早停、XZ contains/首反例、unknown/glitch/stuck、多信号部分失败与旧平铺请求 schema 拒绝；随后 8/8 CTest 和冻结基线检查均通过。counter 与 X/Z 固件实际打开路径均为 `.fst`。
- P5 verify/window 批次：`tests/test_window.py` 13/13 通过，覆盖单点条件的 pass/fail/unknown、posedge-after 与 clock context、alias 声明约束，以及窗口 always/eventually/never、决定性结论、空采样、`max_samples` inconclusive、`line_limit` 仅裁剪 findings 和旧请求 schema 拒绝；随后 8/8 CTest 和冻结基线检查均通过。全部 operand 和 edge 由 Wellen 直接从 counter `.fst` 采样。
- P5 counter/pulse/handshake 批次：`tests/test_clock_counter.py` 15/15 通过，覆盖表达式 `vld`、拼接 `cnt`、无有效 counter 值、非法时间范围、`max_samples` 分析不完整、`line_limit` 证据裁剪、未采样 valid pulse、payload 规则依赖，以及 handshake 的 intervals/all、valid hold、stall data 与 findings 裁剪；随后 8/8 CTest 和冻结基线检查均通过。实际输入仅为 `counter/waves.fst` 与 `stream/waves.fst`，三项 action 均从当前 session 的 Wellen 后端按需采样。
- P5 APB 批次：`tests/test_protocol.py -k apb` 13/13 与 `test_value_at_apb_source` 1/1 通过，真实从 `apb/waves.fst` 解码两写两读四笔事务，覆盖 config 列表/按名查询、query count/list/index/last、exact/range/mask、statistics、cursor、window、裁剪与缺失配置；随后 8/8 CTest 和冻结基线检查均通过。
- P5 AXI 批次：`tests/test_protocol.py -k axi` 13/13 与 `test_value_at_axi_source` 1/1 通过，真实从重新生成的 `axi/waves.fst` 解码一笔三 beat write 和一笔两 beat read，覆盖 31 个配置叶子、五通道握手、真实 SIZE/BURST、config/query/filter/statistics/cursor、latency/osd/pending、stall、Top-N/threshold、outstanding、pair 与显式 TSV export；随后 8/8 CTest 和冻结基线检查均通过。固件只用 `--trace-fst` 生成，未生成或读取 VCD，Verilator 仓库代码未修改。
- P5 stream 批次：`tests/test_stream.py` 35/35 与 `test_value_at_stream_source` 1/1 通过，真实从 `stream/waves.fst` 读取四笔 40/60/80/120ps、AA/BB/CC/DD ready-transfer 及五笔纯 vld/vld-bp transfer；覆盖冻结命名配置、全部 11 种 query、stall window、SOP/EOP packet、exact/range/mask filter、alias/slice/comparison/concatenation beat field、三类 handshake、动态 validate，以及 transfer/packet/packet_beats preview 和显式最终 export。8/8 CTest 与冻结基线检查继续通过。实现只在请求期间由 Wellen 直接读取当前 FST，`cache_scope` 仅作为扫描范围选择，不建立离线或跨请求波形缓存，Verilator 仓库未修改。channel interleaving 与 packet-stable field 深层组合仍待独立回归，尚未宣称 stream 全合同最终关闭。
- P5 全量 pytest 修改前证据：当前 160/170 通过，剩余 10 项集中在 combined trace 6 项、batch 1 项、session 旧断言 2 项和 waveform 错误旧断言 1 项；本轮已迁移的 stream 用例无失败。旧断言也必须按冻结合同核实后修改，不通过放宽 schema 消除失败。
- P5 全量 pytest 复核：APB、AXI 及各自 `value.at` 值源不再出现在失败列表；剩余 20 项失败集中在尚未迁移的 stream、combined trace，以及仍断言早期 batch/session/error envelope 的旧测试。严格 request/response gate 保持开启，未为这些失败放宽 schema。
- P5 导出边界：`list.export` 的 `u64bin.v1`、`event.export` 的 JSON 和 `nwave.rc.generate` 的 RC 仅是用户显式请求的最终产物，不是 file transport，也从不作为后续分析输入；所有事件发现、采样、首次差异和导出数据收集仍由 Wellen 在当前 FST 会话中按需执行。
- P6 第一批：Verilator 修改前 `t_xdd_p4` 失败在缺少 `xdd_trace_driver_predicate`；最小实现后 trace/simple/full/metadata/operators/UART/P3/P4 8/8、distribution copyright/cppstyle 2/2 通过。xdebug-fst 的 counter 305ps 查询在 300ps active time 读取 `top.reset=0`，只返回 else/NBA 第 12 行；combined 20/20、clock/counter 联合 37/37、全量 pytest 209/209、CTest 8/8 和冻结基线检查均通过。实际波形仍只有 `.fst`，未修改五组 FST，未生成 VCD/JSON/离线索引。
- P6 第二批：不修改 Verilator。真实 APB FST 在 260ps 由 Wellen 直接读取 `presetn/psel/penable/pwrite`，排除复位分支并唯一选中 `prdata <= mem[paddr]` 的 `apb_top.sv:25`，同时覆盖嵌套条件、NBA、数组 RHS 和端口归一化。新增独立普通 case 固件，在 40ps/60ps 的 FST 观测点分别唯一选中当时的 `case_top.sv:14` item 与 `:15` default；第三批扩展固件后对应行号为 `:16/:17`。仓库只保留可重复生成的 `.fst` 和最小 DesignDB `.so`，不保留仿真 ELF、普通 obj_dir 产物、VCD、JSON 波形快照或离线索引。combined 定向测试 22/22、全量 pytest 211/211、CTest 8/8 与冻结基线检查均通过；casez/casex 在该批仍失败关闭，未被近似为普通 case。
- P6 第三批修改前证据：扩展同一最小 case 固件，使 `casez (sel)` 的 `2'b1z` 在 FST 中 `sel=2'b10` 时应选中 item，并使 `casex (sel)` 的 `2'bx1` 在 `sel=2'b01` 时应选中 item。冻结 Verilator `02f4a2259` 按既定安全策略为两类 driver 发布空 predicate，`trace.active_driver` 因而返回 `analysis_complete=false`，定向 combined 回归真实失败。xdebug 已有 source/role/普通 case predicate 和完整四态 FST 值仍无法重建 wildcard 类型，证明缺失的是 DesignDB 的静态 case 匹配种类；下一步只允许扩展 predicate 表达式及其定向回归，不增加 process order/sequential boundary，不触碰普通 Verilator 仿真或优化。
- P6 第三批实现：Verilator `a3adaaabb` 仅在既有 emitter 中发布 `==?z`/`==?x`，xdebug 表达式求值器逐 bit 实现 casez（Z 通配）与 casex（X/Z 通配）的四态规则。真实 case FST 在 60ps/40ps 分别唯一选中 `case_top.sv:28/:32`；GCD FST 的真实 X 值进一步证明 casex 匹配为真而 casez 对同一 X/0 比较为假。combined+clock/counter 定向测试 41/41、全量 pytest 213/213、CTest 8/8 与冻结基线检查均通过；所有运行时值仍由 Wellen 直接按需读取 FST。
- P6 第四批修改前证据：在同一最小 FST 固件加入 `reset / sel==0 / sel==1 / else` 四路同目标嵌套 if。45ps 查询时 FST 中 `reset=0, sel=1`，应唯一选择 `nested_out <= data + 1` 的第 45 行；冻结 Verilator `ff0c1d016` 的 V3Inst 已把 RHS 合并成第 41 行的嵌套 `AstCond`，现有 emitter 把全部条件和叶子压成同一无条件 driver，xdebug 因而错误返回复位行 41，定向 combined 回归真实失败。只读 AST dump 证明 emitter 挂接点仍保留各 `AstCond` 条件和叶子的 41/43/45/47 源位置，因此下一步只允许在现有 emitter 内拆分 RHS 条件叶子，不移动 pass、不修改 V3Inst/调度/仿真，也不增加新 ABI 字段。
- P6 第四批实现：Verilator `a91524d63` 仅在既有 emitter 内拆分条件 RHS 叶子；xdebug 使用 Wellen 从真实 case FST 在 40ps 直接读取的 `reset=0, sel=1`，已唯一返回 `nested_out <= data + 1` 的第 45 行。combined 定向测试 24/24、全量 pytest 214/214、CTest 8/8 与冻结基线检查均通过；依赖 revision 更新但 XDD header/ABI/capability 不变。
- P6 第五批修改前证据：在同一最小 case 固件加入单行 `ternary_out <= sel[0] ? data : 8'h5a`，同时覆盖同源行不同谓词和常量 RHS 叶子。真实 FST 在 40ps 的 `sel[0]=1` 能选中 `data` 分支；60ps 的 `sel[0]=0` 应选中同源行常量分支，但 xdebug 按 `(file,line,kind)` 合并 DesignDB 语句并只保留首个 predicate，实际返回 `total_count=0`，定向回归真实失败。DesignDB 已为两片叶子提供独立谓词和依赖，缺陷局限在 xdebug 语句身份键；下一步只允许将 predicate 纳入聚合身份并补齐回归，不修改 Verilator/Wellen，不改变 FST-only 直接读取链。
- P6 第五批实现：xdebug-fst 仅把 `activation_predicate` 纳入语句聚合身份，保持同源行三元表达式的信号与常量叶子分离。真实 case FST 在 40ps 以 `sel[0]=1` 唯一选择 `data`，在 60ps 以 `sel[0]=0` 唯一选择常量叶子并以 `sel` 控制依赖形成动态证据；combined 25/25、全量 pytest 215/215、CTest 8/8、冻结基线与 CMake 依赖锁门禁通过。Verilator/Wellen、XDD ABI/capability 和唯一 FST 直接读取架构均未修改。
- P6 第六批修改前证据：counter DesignDB 已发布内部输入端口 `top.counter_top.clk` 与父级 `top.clk` 的对称 `port_boundary`，两条 FST 名称由 Wellen 解析为同一保真 alias 值。305ps 的 active-driver chain 应从内部端口上溯一跳并在顶层 primary input 终止；当前 xdebug 在到达 `top.clk` 后又沿对称边折返，形成三跳并误报 `loop_detected`。缺陷局限在 consumer 的跨层输入方向选择；下一步只允许优先向较浅父级遍历并禁止反向折返，不修改 Verilator/Wellen、XDD ABI 或 FST 数据路径。
- P6 第六批实现：xdebug-fst 仅对无 driver 的声明 input 约束端口遍历方向，允许从内部 `top.counter_top.clk` 上溯到较浅的 `top.clk`，并在父级 primary input 终止；不再沿对称 DesignDB 边折返形成假环。combined 26/26、全量 pytest 216/216、CTest 8/8 与冻结基线通过。现有 driver/inout 行为未作无证据改动；Verilator/Wellen、XDD ABI/capability 和 FST-only 直接读取链均未修改。
- P6 第七批修改前证据：冻结原版 `active_trace_chain.cpp` 明确把 assignment 的目标自引用从 RHS 候选排除，并以 `assignment/constant_or_no_rhs_signal` 终止。既有 counter 的 `count <= count + 1` 在 300ps 为活动 NBA；DesignDB 因目标自引用去重仅发布该 `nba` 的 control 依赖，当前 xdebug 误用“没有非自身 RHS”等价于 control-only，305ps 实际返回 `control_only/control_only`。下一步只允许依据 XDD assignment kind 修正 consumer 终止分类，不增加 sequential ABI 字段，不修改 Verilator/Wellen 或 FST 波形链。
- P6 第七批实现：xdebug-fst 使用已有 `nba/proc_assign/cont_assign` kind 区分静态赋值与纯控制证据；活动 assignment 没有可继续的非自身 RHS 时，按冻结原版合同返回 `assignment/constant_or_no_rhs_signal`。counter 305ps 的 `count <= count + 1` 已通过；combined 27/27、全量 pytest 217/217、CTest 8/8 与冻结基线通过。未增加 sequential ABI 字段，Verilator/Wellen 与唯一原始 FST 按需读取链均未修改。
- P6 第八批修改前证据：按冻结原版 `active_semantics_tb.sv` 的双连续赋值语义，在真实 case 固件加入第 54/55 行两个同强度 `assign multiple_driver_out`，chain 必须返回 `ambiguous/multiple_active_candidates` 且 `statement_count=2`。`V3Begin` 后 AST 仍有两条赋值，但 `V3Tristate` 按普通仿真既有规则移除第二条，当前 DesignDB 挂接点只发布第 54 行；xdebug 因静态事实缺失错误沿 `data` 追到 `primary_input`。现有 XDD ABI 足以表示多条 driver，缺失仅发生在 emitter 可见性；下一步只允许在 `--design-db` 时旁路保留被 V3Tristate 删除的同强度非 tristate 静态 driver 描述，不能改变 AST、普通仿真、pass 顺序、ABI/capability 或 FST 运行时值。
- P6 第八批实现：Verilator `5c19377e3` 在上述唯一许可边界内完成修复。真实 `case/waves.fst` 仍由 Wellen 在 45ps 直接按需读取；DesignDB 只补充第 54/55 行两条 `cont_assign` 及各自 `data/sel` 静态 RHS。`trace.active_driver_chain` 现返回 `ambiguous/multiple_active_candidates`、`statement_count=2`、`rhs_signal_count=2`，没有把 FST 用作 HDL 多驱动分析器。定向用例 1/1、combined 28/28、全量 pytest 218/218、CTest 8/8、冻结基线和 CMake 依赖锁门禁通过；XDD header/ABI/capability、Wellen 和生产 FST 读取架构均未改变。
- P6 第九批修改前证据：在同一真实 case FST 固件加入 `case (sel) inside`，分别要求 65ps 的 `sel=2'b10` 选中第 60 行 `2'b1?` 模式、45ps 的 `sel=2'b01` 选中第 61 行闭区间 `[0:1]`。冻结 Verilator `5c19377e3` 已发布两条 NBA 的 RHS/control/source line，但五条相关 driver predicate 全为 `nullptr`；xdebug 按既定安全合同返回 `analysis_complete=false`，定向用例真实失败。FST 已保真包含 `sel` 和 `inside_out` 的运行时值，现有 XDD predicate 字符串 ABI 也足以承载模式/范围表达式，缺失仅是 emitter 对 `caseInside()` 的静态匹配语义；下一步只允许在既有谓词表增加 RHS-only wildcard 与闭区间表达，不修改普通 case lowering、仿真、ABI/capability、Wellen 或 FST 数据路径。Verilator 当前明确不支持通用 `case matches` tagged pattern，不能把 inside 的有限修复冒充 matches 已完成。
- P6 第九批实现：Verilator `3e7cca4f1` 仅在既有 DesignDB emitter 中为 pattern 发布 `==?i`、为 `AstInsideRange` 发布 `>= && <=`，default 谓词否定所有此前 item；xdebug 表达式求值器只把 RHS 的 X/Z 当 inside wildcard，LHS 的未知位不误作通配。真实 case FST 在 65ps/45ps 分别唯一选中第 60/61 行，额外 GCD 四态测试证明未知 LHS 对具体 RHS 为 false、对全 wildcard RHS 为 true。combined+clock/counter 47/47、全量 pytest 219/219、CTest 8/8、冻结基线与 CMake 依赖锁门禁通过。XDD header/ABI/capability、Wellen 和唯一原始 FST 按需读取链均未改变；通用 `case matches` 仍未完成。
- P6 第十批修改前证据：在真实 case FST 固件加入 `assign inout_bus = data`，并把该 net 连接到子模块 `inout bus`。45ps 从 `top.case_top.u_inout.bus` 追踪时，冻结 Verilator `3e7cca4f1` 已发布子端口到父级 net 的原生 `port_boundary`，因此第一跳正确到 `top.case_top.inout_bus`；但 V3Tristate lowering 后该 net 的 DesignDB RHS 变成内部 `inout_bus__strong`，原始第 67 行 `data` 依赖消失，chain 提前以 `assignment/constant_or_no_rhs_signal` 终止而不是继续到 `top.case_top.data → top.data → primary_input`。真实 FST 已保真暴露子端口、父 net 和 data alias，现有 XDD driver ABI 足以表达该源；缺失是 inout lowering 前的静态原始连续赋值事实。下一步只允许复用已有 `--design-db` 旁路描述机制保存这种被改写的原始 RHS，不改变普通 tristate/inout 仿真、AST lowering、pass 顺序、ABI/capability、Wellen 或 FST 数据路径。
- P6 第十批实现：Verilator `007f1aa5c` 先在 `--design-db` 下保存目标为 tristate、赋值本身非 tristate 的原始连续 RHS；真实集成随即揭示原始 `data` 与生成 `__strong` 同时存在会形成假双驱动，故 `8a5523487` 将 lowering 描述标为替换型，仅删除同一目标、同一实例下目标名 `__` 前缀的内部 source/load，再附加原始关系；普通多驱动描述仍为追加型。最终真实 case FST 的 chain 为 `u_inout.bus → inout_bus → case_top.data → top.data`，以 `primary_input` 终止且不折返、不误报歧义。全量 pytest 220/220、CTest 8/8、冻结基线与 CMake 依赖锁门禁通过。运行时四跳值全部由 Wellen 直接从原始 `.fst` 读取；XDD header/ABI/capability 和 Wellen 未改变。
- P6 第十一批：在 case 固件中加入 `nested_inout_bus → inout_mid.bus → leaf_bus → inout_leaf.bus` 的真实两级 inout 网络。45ps 从最深子端口开始，当前锁定的 Verilator `8a5523487` DesignDB 已提供足够的静态端口边和原始 RHS，既有 xdebug consumer 沿 Wellen 直接读取的原始 FST alias 返回 `u_leaf.bus → leaf_bus → u_inout_mid.bus → nested_inout_bus → case_top.data → top.data` 并以 `primary_input` 终止。没有修改 Verilator/Wellen、XDD ABI/capability 或 action 算法，也没有建立 FST 转换、索引、离线数据库或 fallback；纯透传中间端口可能被静态连接合法折叠，因此回归特意使用独立中间 net 保留可验证的两级关系。
- P6 第十二批修改前证据：冻结原版 `tests/active_trace_chain/p0_composability/p0_2_module_boundary/tb.sv` 明确要求从父级 net 进入子模块 output 端口，再沿子模块赋值到 input 端口和父级输入。等价 case 固件在 45ps 查询 `top.case_top.child_output_bus` 时，锁定 Verilator `8a5523487` 的 DesignDB 虽保留端口连接，却把 lowering 后第 98 行 `assign data_o = data_i` 的 RHS 直接规范成 `top.data`；当前 xdebug 因而只返回 `child_output_bus → top.data` 两跳并错误宣称完整，跳过 `u_output.data_o → u_output.data_i → case_top.data` 三个原版要求的层级边界。真实 FST 已保真包含这些 alias，缺失属于 DesignDB 静态语句/端口关系的层级身份，不允许从 FST 值相等反推。定向回归结果为 2 个既有 inout 用例通过、1 个 output 用例失败；下一步必须先证明现有 XDD 记录能否在 consumer 无歧义重建，只有不足时才允许对 Verilator 做最小附加修复。
- P6 第十二批实现：审计 XDD 证明现有记录已分别保留 `data_o → child_output_bus`、`data_i → top.data` 的端口边，以及父 net 第 98 行内部赋值的扁平化 RHS；因此没有修改 Verilator。xdebug consumer 只组合这些静态事实：从非端口 net 唯一进入更深 output port；当 output 的直接 driver 是刚离开的父 net 时，用父 net 的唯一活动赋值在同一实例中定位唯一 input port；input 的扁平化源再优先经过最近祖先 input port。最终链严格为 `child_output_bus → u_output.data_o → u_output.data_i → case_top.data → top.data` 并以 `primary_input` 终止。匹配依据是 DesignDB 方向、连接和层级作用域，不比较 FST 值来发现 alias；Wellen 仍只逐 hop 读取当前原始 FST。combined 32/32 通过，Verilator/Wellen 与 XDD ABI/capability 均未修改。
- P6 第十三批：在 case 固件增加两个独立时钟过程对 `procedural_multi_out` 的 NBA，第 83 行谓词为 `!reset`，第 87 行谓词为 `sel[0]`。Wellen 从原始 FST 直接读取 25ps/45ps 对应 active time 的控制值；25ps 唯一选择第 83 行，45ps 两条均活动并返回 `ambiguous/multiple_active_candidates`、`statement_count=2` 和精确源行集合。当前 DesignDB 已完整表达两条过程 driver 及其 predicate，xdebug 聚合也正确，因此不修改实现、Verilator、Wellen 或 ABI；combined 34/34、全量 pytest 224/224、CTest 8/8 与冻结基线通过。跨层、连续+过程混合及调度区/多时钟竞争仍未关闭。
- P6 第十四批修改前证据：两个 `output_leaf` 实例以不同输入同时驱动父级 `cross_instance_multi_out`。锁定 Verilator `8a5523487` 已为父 net 发布两个 RHS driver，并保留 `u_output_a/u_output_b` 各自 output/input 端口静态边；但两个实例的内部 `assign data_o = data_i` 都落在同一源码第 118 行，当前 xdebug 只以 `(file,line,kind,predicate)` 聚合，45ps 错误返回 `multiple_rhs_sources`、`statement_count=1`，而冻结原版多 driver 合同要求 `multiple_active_candidates`、`statement_count=2`。FST 中一个内部临时 RHS 未单独可读也不能改变静态 driver 数，禁止按值或可读性合并/删除候选。下一步只允许从既有 DesignDB 端口连接推导实例 identity 并加入 consumer 的 statement key；不修改 Verilator/Wellen/XDD ABI，不用 FST 猜实例。
- P6 第十四批实现：xdebug-fst 在内部 `DriverRecord` 增加不进入 XDD ABI 的 `statement_identity`。仅当同一父 net 静态连接至少两个 output port，且同一基础 statement 的全部依赖均可通过各 output 所在实例的 input port 唯一映射到实例作用域时，才以实例作用域扩展聚合键；单实例多 RHS 仍聚成一条语句，映射不完整/不唯一时不任选。45ps 跨实例用例现返回 `multiple_active_candidates`、`statement_count=2`、`rhs_signal_count=2`，既有单表达式双 RHS 用例仍返回 `multiple_rhs_sources`。候选发现完全基于 DesignDB 静态边，FST 仅用于 ambiguity evidence 的逐 RHS 值；全量 pytest 225/225、CTest 8/8 与冻结基线通过，Verilator/Wellen/XDD ABI 均未修改。
- P6 第十五批修改前证据：在一个 NBA 过程中加入 reset 常量第 103 行、`sel[0]` 信号 RHS 第 105 行和 else 常量第 107 行。锁定 DesignDB 已正确发布三个独立 predicate；两条常量叶子以 control 记录保留精确赋值行，信号叶子保留 RHS。5ps 查询能正确判定 reset 常量活动并以 `assignment/constant_or_no_rhs_signal` 终止，但 chain 仅把含 RHS signal 的活动 statement 用于 hop 证据，导致 hop 错误回退到目标声明第 100 行，而非赋值第 103 行；25ps else 常量同类。下一步只修 consumer：所有活动 assignment 都参与 statement/源码选择，只有 dependency role 为 `rhs` 才能成为上游 hop；control 只能作证据，不能伪装数据源。Verilator/Wellen/XDD ABI 不改。
- P6 第十五批实现：chain 现在保留含 RHS、类型为 NBA、或至少有 control 依赖的活动 statement；单个常量 NBA 用 `representative_driver` 取得精确 file/line，但只有代表记录 role 为 `rhs` 时才形成上游 hop。因此 5ps/25ps 常量叶子分别以第 103/107 行终止为 `assignment/constant_or_no_rhs_signal`，45ps 信号叶子仍沿第 105 行 `top.data` 追踪。首次实现把 lowering 生成的裸 `statement`-only `proc_assign` 也计入，导致两级 inout 假歧义；最终规则明确排除这类无 RHS、非 NBA、无 control 的内部节点。combined 36/36、全量 pytest 226/226、CTest 8/8 与冻结基线通过。Verilator/Wellen/XDD ABI 不改，FST 只提供 predicate 和 hop 值。
- P6 第十六批修改前证据：`output_expr_leaf` 内部第 143 行 `data_o = data_i ^ {6'b0,sel_i}` 经 lowering 后在父 `child_output_expr_bus` 上形成 `top.data/top.sel` 两个 RHS；XDD 同时保留 `u_output_expr.data_o → parent net` 及两个 input port 到对应扁平化 source 的静态连接。冻结原版 module-boundary 责任要求先进入子 output，再在内部赋值处报告一个 statement 的 `multiple_rhs_sources`。当前 chain 在父 net 看到双 RHS 后立即歧义，只返回一个父 hop，跳过 `u_output_expr.data_o`，evidence 也暴露扁平化顶层源而非子 input port。下一步只用现有 XDD 静态边：唯一 output 边界优先于父 net lowering driver；进入 output 后将父 statement 的每个 RHS 唯一映射到同实例 input port，再执行原有多 RHS 判定。Verilator/Wellen/XDD ABI 不改，FST 不参与映射。
- P6 第十六批实现：chain 对无 unresolved predicate 的非端口 net，若静态图只有一个更深 output port，则先跨该边界，不在父 net 报 lowering 后歧义。到达 output 且直接 driver 只是刚离开的父 net 时，读取父 net 的唯一活动 statement，将其每个 RHS 全量映射到 output 同实例的唯一 input port；全部成功才替换 signal index 并重新执行原有分组/歧义逻辑。45ps 最终两 hop 为 `child_output_expr_bus → u_output_expr.data_o`，在第二 hop 返回一个 statement、两个 RHS `u_output_expr.data_i/sel_i`。映射候选和唯一性完全来自 DesignDB；FST 只为两个已选 input port 生成运行时 evidence。combined 37/37、全量 pytest 227/227、CTest 8/8 与冻结基线通过，Verilator/Wellen/XDD ABI 不改。
- P6 第十七批：Verilator `ea1d3c9b4` 先以普通仿真和 DesignDB 两条退出码 10 的失败证据证明精确表达式 `case matches` 被错误按 tagged union 无条件拒绝；`adc193c2f` 随后只放行至少一个非 default 精确表达式 item 且无 tagged/pattern 节点的有限子集，并在既有 DesignDB predicate 字符串中发布 `===`。普通精确 matches 仿真、DesignDB、`t_tagged_case`、`t_tagged` 各 1/1，完整 XDD 8/8、distribution 2/2 与源码构建通过；default-only、tagged union/expression/pattern、pattern variable/star 和独立 `matches` 运算符仍拒绝。xdebug-fst 新增独立最小 `matches` 固件，生产测试只打开 `waves.fst`；45ps/65ps 的 item/default 分支由 Wellen 直接读取原始 FST selector 后唯一选中，DesignDB 只提供静态 `===` 谓词，没有 FST 转换、预扫、离线索引、全量快照或 fallback。combined 38/38、全量 pytest 228/228、CTest 8/8、冻结基线和 CMake 依赖锁门禁通过；XDD header 哈希保持不变。
- P6 第十八批修改前证据：在既有最小原始 FST 固件加入 `temporal_out = temporal_q` 与 posedge NBA `temporal_q <= data`；`data` 恒为 `8'h20`，所以 NBA 在 20ps/40ps/60ps 执行但 FST 只在 20ps 记录首次值变化。冻结原版 `active_trace_chain.cpp` 明确用 NPI `activeTime` 表示最近赋值事件并把下一跳时间推进到该事件，故 65ps 查询要求根 hop `active_time=60ps`。当前 xdebug-fst/Wellen 只能从原始 FST 得到 20ps，定向回归真实失败为 `20ps != 60ps`。DesignDB 已有 NBA/RHS/reset predicate/源码行但没有 event-control clock/edge，证明不能仅在 consumer 修正；禁止让 data 每拍变化、使用固定周期、扫描同值信号或任选时钟绕过。下一步只允许先扩展 Verilator 独立回归证明最小 sequential-boundary 事实需求。
- P6 第十八批实现：Verilator `01f9f2a4b` 先要求简单 posedge NBA 发布 `event_posedge(clk)`，并要求既有异步复位过程同时发布 `event_posedge(clk)` 与 `event_negedge(rst_n)`；修改前独立回归稳定失败。`6239de45e` 随后只在 DesignDB emitter 中读取赋值所在 `AstAlways` 的直接敏感信号，通过现有 driver role 字符串发布 `event_*`，不改 ABI 布局/版本、仿真调度或 pass 顺序；复杂敏感表达式不猜测。xdebug action 以该静态事实让 Wellen 在当前原始 `.fst` 上按需倒查实际边沿，并把 60ps 的 NBA 事件时间传播过直接连续 alias；65ps 查询现在得到 `temporal_out(active=60ps) → temporal_q(time/active=60ps) → top.data`。FST 仍只提供时钟/值运行时事实，不承担静态分析；没有转换、固定周期、任选时钟、离线事件索引、全量快照或 fallback。Verilator XDD 8/8、distribution 2/2、xdebug combined 39/39、全量 pytest 229/229、CTest 8/8、冻结基线与 CMake 依赖锁均通过。
- P6 第十九批修改前证据：在同一最小固件增加 `temporal_deep → temporal_mid → temporal_q(NBA)` 两级连续 alias。DesignDB 已精确给出两条唯一 `cont_assign/rhs` 和下游 NBA 的 `event_posedge(clk)`；原始 FST 也保真包含三个信号及 clk，因此无需扩展 Verilator/Wellen。当前 consumer 只向前查看一个直接 alias，65ps 查询虽得到正确四跳与 primary input 终止，却把前三跳 active time 全报为 20ps，而非 60ps。失败证明事件时间传播必须沿 DesignDB 唯一连续 RHS 链有界递归，不能写死一层；禁止按 FST 等值寻找 alias、建立事件索引或扩大 XDD ABI。
- 旧 action 测试现状：P1 的严格 request/response gate 已按计划启用，仍使用 `render_format`、平铺 `begin/end`、旧 config shape 或旧成功响应 shape 的测试会 fail closed；这些不是 P1 协议回退点，将在 P3/P5 对应 action 实现迁移时逐组改正并恢复全量绿色。
- 环境记录：系统 `pytest`/`python3 -m pytest` 缺少 pytest；按仓库 `HANDOFF.md` 使用已记录的 xverif Python 环境运行同一测试层，没有更换 backend、数据或测试内容，也未进行沙箱外重试。

## 剩余差异

P0、P1、P2、P3、P4 已关闭，P5 的功能迁移批次已覆盖，P6 正在执行。当前已完成 active-driver 的 counter `if/else`、APB 嵌套条件、普通 `case/default`、`casez/casex`、`case inside` pattern/range、精确表达式 `case matches` item/default、lowering 后同目标嵌套条件、同源行三元与嵌套常量 NBA、input alias 父级上溯、基本 inout 与真实两级 inout 跨端口上溯、基本 output 模块边界、NBA self-RHS 分类、基础双连续、双条件过程与跨实例 output 多驱动歧义，以及 X-origin 多分支基础语义；但通用 tagged/pattern matches、更多 NBA/常量组合、复杂 output/inout alias 和混合类型/调度边界多驱动的原版差分仍未关闭，因此绝不能宣称完全一致。FST 始终由 Wellen 在会话中按需读取，不转换成 VCD、JSON 波形快照、私有索引或离线分析数据库。2026-08-09 用户明确裁剪 TCP 与 file transport，因此二者不再开发或作为验收门禁；显式 export action 写出的最终产物不属于 transport，且禁止作为分析 fallback。严格 validator 和 response gate 保持开启，不为旧测试放宽 schema。2026-08-10 新增 [`XDEBUG_FULL_PARITY_GOAL_LOCK.md`](XDEBUG_FULL_PARITY_GOAL_LOCK.md) 作为当前 active Goal 的权威执行附件；后续每个批次按 `GOAL-FST-DIRECT-001` 审查唯一 FST 数据流、禁止转换/离线分析/fallback、TCP/file 裁剪及 Verilator 克制修改，任一违反即否决提交与 Goal 完成。

## P4 修改前失败证据

- 定向 fixture：Verilator `t/t_xdd_p3.v`，包含普通 input/output、两级 module port、interface/modport、array 和控制流。
- 现有生成物把 `top.p3_sem_top.u_mid.clk`、`u_mid.out` 及 `u_leaf` 的声明端口发布为 `wire`，XDD 不包含 `xdd_signal_direction`、`xdd_port_connection_*`、ABI version 或 capability 符号；xdebug-fst 只能逐信号扫描 driver/load 表启发式推断方向和跨层连接。
- 修改前新增 `t/t_xdd_p4.py`，要求稳定 ABI/capability、声明方向与预计算 port boundary。执行 `python3 t/t_xdd_p4.py --vlt` 真实失败在缺少 `int xdd_abi_version(void)`，满足“先有失败用例再修改 Verilator”的门禁。
- 一次无效命令 `python3 driver.py --make gmake t_xdd_p3` 把测试名误作额外参数并启动全套调度，已立即中断；它只触及 ignored `obj_*`，未修改源码，结果不计入验收。正确单用例命令 `python3 t/t_xdd_p3.py --vlt` 随后 1/1 通过。
- 结论：方向和 port connection 是当前 XDD 无法由单个记录确定、且 xdebug-fst 只能用全表启发式扫描得到的不可靠事实，符合任务书“允许的最小扩展顺序”第 1 项。P4 先只扩展 ABI version/capability、声明方向和预计算端口边；active-driver 其它语义字段必须另有失败差分才允许加入。
- 第二个定向差分使用同一 `t_xdd_p3.v` 中 `out` 的组合赋值：当前 XDD 把 RHS 数据依赖和外围 `if/case` 控制依赖压成结构完全相同的 driver 记录，消费者无法可靠判断 active-driver 的 `rhs_samples` 与 `control_only` 路径。先扩展 `t_xdd_p4.py` 要求逐 driver 的原生 dependency role，修改前执行真实失败在缺少 `xdd_trace_driver_role`。因此只允许增加该单一字段与 capability；没有证据支持的 process order、sequential boundary 等字段继续禁止加入。
