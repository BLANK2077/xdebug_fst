# xdebug_oc 全能力兼容修复、Goal 执行与分批提交计划

## 最新验收口径：能力一致、信息语义一致（2026-08-13 用户确认）

用户已明确取消“输出必须与原版完全一致”的过严要求。本文历史章节中出现的“完全一致”、
“逐字段一致”、“逐字一致”、“全部归一化 JSON 相等”或类似表述，自本节起统一按以下最新
口径解释；若历史文字与本节冲突，以本节为准：

1. 最终目标是 **能力覆盖一致**：原版公开的 73 个 action 所代表的查询、分析、会话、导出和
   transport 能力，在本项目已确认的 FST-only、TCP/file 裁剪范围内均有可工作的对应实现。
2. 最终结果要求 **关键信息语义一致**：对同一有效请求，目标对象、时间、值、范围、计数、
   driver/load 关系、控制条件、来源路径、完整性、截断状态和失败原因等会影响用户判断的事实
   必须等价；不得遗漏关键事实、制造错误事实，或把未知/未完成伪装成确定/完整。
3. 不再要求非语义展示完全相同，包括 JSON object 字段顺序、无序集合的输出顺序、summary
   和 warning 的逐字措辞、建议动作的文字或排列、等价的时间/数值渲染、实现 build id、耗时、
   临时路径，以及不影响能力和结论的冗余诊断元数据。
4. xdebug v1 请求合同和公开 action 名仍作为调用兼容入口；响应 schema 继续用于保证本实现
   自洽和稳定，但不再以“与原版每个可选字段及输出形状逐项相同”作为完成条件。实现可保留
   有用的附加诊断字段，也可不复制不影响结论的展示字段。
5. 后续“原版差分”改为能力与信息语义差分：比较请求是否可执行、关键事实集合是否等价、
   completeness/limited/truncated 是否如实，以及错误是否属于同一原因类别；不得再用整份
   归一化 JSON 相等作为唯一或强制门禁。
6. 这一放宽只移除表面一致性负担，不缩减 73 个 action 的能力族，不放宽
   `GOAL-FST-DIRECT-001`，不恢复 TCP/file，不允许 fallback，也不降低 active-driver、chain、
   X-origin 等分析结果的事实正确性要求。

据此，本文后续所有待办和最终审计都应优先回答“用户能否完成同一种工作、得到等价的关键
事实和结论”，而不是“两个实现是否生成相同 JSON 文本”。

### P6 PatternVar 动态闭环补充（2026-08-13）

PatternVar lowering 产生的局部绑定变量可以存在于 Verilator DesignDB，但不一定写入原始
FST。此时 xdebug action 不得把缺少中间量波形误报为常量赋值，也不得从上下游值相等猜测
关系。允许的处理只有：DesignDB 已证明该中间量为纯组合 cont/proc assignment、存在真实 RHS、
不含 self-RHS、event、NBA 或 force 时，consumer 才能把它作为静态透明节点展开；活动分支
谓词和最终可观测信号值仍必须由 Wellen 从当前 session 原始 FST 按需读取。

本合同已由 xdebug-fst `869e932`/`70a7672` 闭环：45ps/65ps 的两条 binding 分支均可由
active-driver 选中，chain 越过不可观测 binding 后到达可观测 packet，X-origin 的同构四态
回归到达真实来源。该规则不得推广到时序状态，也不得解释成由 FST 推断 PatternVar 关系。

### P6 ref driver/反馈/来源分支补充（2026-08-13）

端口 hop 可以对 alias 的物理路径身份透明，但不能使不同终端事实透明。若两条 X-origin 链
共享相同普通 driver 前缀，一条经 ref 端口形成 loop、另一条经 ref 端口到达真实 X 来源，
coalescing 必须同时保留 `loop_detected` 与 `origin_found`；只允许合并“同一终端、同一 onset、
同一终止语义”的物理 alias 变体。`max_chains`、`max_depth` 联合限制必须继续报告被省略来源、
frontier 和可续跑参数。本合同由 `36f2b6c` 的原始四态 FST 回归锁定。
`3080a4c` 又锁定 `max_nodes+max_chains`：全局 node budget 耗尽不得删除尚未返回的 ref 来源，
保留链必须携带 node frontier，省略链必须进入 pending/branch event，两种 limitation 均须公开。

Goal 不可漂移约束：[`XDEBUG_FULL_PARITY_GOAL_LOCK.md`](XDEBUG_FULL_PARITY_GOAL_LOCK.md)。该文档是当前 active Goal 的权威执行附件，固定 FST 唯一输入、Wellen 直接按需读取、禁止转换/离线分析/fallback、TCP/file 裁剪和 Verilator 克制修改等否决条件。

配套架构说明：[`XDEBUG_FST_VERILATOR_WELLEN_ARCHITECTURE.md`](XDEBUG_FST_VERILATOR_WELLEN_ARCHITECTURE.md)。该文档说明 Verilator DesignDB 修改的范围、原因和数据流，以及 xdebug-fst 对 Wellen 波形能力的需求、双 C ABI 方案和后续收敛边界。

## 零、不可漂移的 FST-only 输入边界（2026-08-09 用户确认）

本节及其权威执行附件 [`XDEBUG_FULL_PARITY_GOAL_LOCK.md`](XDEBUG_FULL_PARITY_GOAL_LOCK.md) 在当前 Goal 中的永久约束编号为 **`GOAL-FST-DIRECT-001`**。后续计划、实现、测试、提交、交接摘要和验收报告必须使用这个编号引用同一条约束；不得通过改名、拆分阶段或更换术语弱化其含义。

本任务、当前 Goal 及 P0–P7 的实现与验收只适配 **FST 波形**。这是覆盖全文所有阶段、提交和完成条件的最高优先级硬约束：

1. xdebug-fst 的生产波形输入只允许 FST；不得把 VCD、FSDB 或其他波形格式接入后端。
2. 单元测试、集成测试、差分测试和最终验收实际打开的波形文件必须是 `.fst`，不得直接打开 VCD 来替代缺失或错误的 FST 能力。
3. VCD 只可作为生成测试 FST 的源描述，且生成过程必须固定、可重复；VCD 读取结果不得作为功能通过证据。
4. 某个 FST 固件不能保留 X/Z、delta-cycle、real、string、event 或层级信息时，必须修复 FST 固件生成链或 Wellen FST 读取能力；不得切换到 VCD、FSDB、其他 backend 或其他 fixture 作为 fallback。
5. 所有测试清单和最终报告必须列出实际输入路径，并设置“全部为 FST”的自动门禁；发现非 `.fst` 波形输入立即失败。
6. 本约束同时修订 Goal 中较早记录的 transport 范围：按用户后续指令，TCP 与 file transport 不实现、不验收，选择时必须明确失败且不得 fallback；会话只保留已规划的 UDS、MCP direct 与 fake-LSF 路径。
7. “只适配 FST”不得退化为把 FST 预处理或转换成离线分析数据库：生产 action 必须由 Wellen 在会话中直接打开原始 `.fst` 并按需读取层级、时间和值变化，再与独立的 Verilator DesignDB 静态事实组合；不得把 FST 转换为 VCD、JSON、私有索引或全量内存快照作为事实来源。

当前 Goal 的 objective 以本任务书为执行来源；Goal 系统不支持在 active 状态原地改写 objective，因此本节是对 Goal 的强制范围增补与后续用户指令的权威记录，不得因旧 objective 文本仍提及 TCP/file 而恢复这些已裁剪能力。

### 术语与实现不得漂移（2026-08-10 用户再次确认）

“适配 FST 波形”只有一种允许的解释：FST 是 xdebug-fst 唯一且必须完整支持的波形输入容器，Wellen 在当前会话内直接、按需读取原始 `.fst` 中的层级、物理时间、delta、类型和值变化，action 在这些事实上完成查询与推理。这里的“分析 FST”不得被解释成建立一个独立的 FST 分析后端、预先扫描全文件、生成中间数据库，或先导出再分析。

必须始终区分“波形格式适配”与“调试语义分析”：FST 仅承担运行时波形事实的输入与保存格式，Wellen 仅承担这些事实的保真按需访问；driver/load、active-driver、chain、X-origin、协议、表达式、窗口和统计等分析能力仍由冻结的 xdebug action 语义及其与 Verilator DesignDB 静态事实的组合实现。不得把任何 action 的算法降级为“由 FST 格式本身完成分析”，不得以 FST 中已有的信号列表或值变化替代原版要求的设计关系、控制依赖、完整性判断和错误合同。本文后续出现的“FST 分析”“分析 FST”或类似简称，一律只能理解为“xdebug action 直接使用当前 session 原始 FST 提供的波形事实执行分析”，不能理解为另一套分析架构。

因此以下规则同时成立，任何后续实现、测试、文档、提交和验收都不得改变：

1. 只需要并且也必须完整适配 FST 波形；不扩展 VCD、FSDB、GHW 或其他波形输入。
2. 所有波形分析事实必须从当前 session 中由 Wellen 直接按需读取的原始 `.fst` 获得。
3. 不允许把 FST 转成 VCD、JSON 波形、私有索引、离线数据库或全量内存快照后再执行 action。
4. Verilator DesignDB 只补充 FST 本身不包含的 HDL 静态关系，不是波形分析替代物，也不得缓存或重建整份 FST 波形。
5. `list.export`、`event.export` 与 `nwave.rc.generate` 只产生用户明确请求的最终输出；这些输出永不回灌为输入，永不参与后续分析，永不成为 backend、transport、fixture 或 fallback。
6. 如果 Wellen 不能从 FST 保真提供某项必要事实，任务是修复 FST 生成链或 Wellen/FST 访问能力并建立失败回归；不得以任何转换、替代格式或离线分析路径绕过。
7. 每个分析 action 必须保持原版 xdebug 的语义责任边界；FST 只替换波形输入适配层，不替换、简化或重定义分析算法。

本节是当前 active Goal 的不可撤销执行修正。Goal objective 首句明确引用本任务书，故本节与“零、不可漂移的 FST-only 输入边界”共同构成 Goal 的权威范围；旧 Goal 文本、旧交接材料或历史代码中的相反表述一律视为已废止，不得作为恢复旧方案的依据。

### Goal 持续执行门禁

`GOAL-FST-DIRECT-001` 不是仅在最终验收时检查的文档约定，而是 P0–P7 每个未完成批次的持续执行门禁：

1. 每个功能批次开始前，确认拟修改的数据流仍为“当前 session 原始 `.fst` → Wellen 按需访问 → action 查询/推理”；若不是，立即停止该实现方向。
2. 每个测试批次必须记录实际打开的波形路径；生产、单元、集成、差分和最终验收发现非 `.fst` 输入立即失败。
3. 每个提交审查必须检查是否新增 FST→VCD/JSON/私有索引/离线数据库/全量内存快照转换，是否重载 export 产物，是否新增 backend/fixture fallback；任一项存在即不得提交。
4. Wellen 能力不足只能形成待修复缺口或修改前失败证据，不得通过替代格式、预处理数据库或离线分析路径使测试变绿。
5. Verilator DesignDB 只能提供静态 HDL 事实；任何把它用于保存、重建或替代 FST 运行时值的实现均直接否决。
6. 即使 73 个 action、schema 和差分测试全部通过，只要违反本门禁，Goal 仍不得标记 `complete`。

当前 Goal 系统不能原地改写 active objective。该限制不改变约束效力：Goal objective 第一行将本任务书定义为完整计划来源，因此 `GOAL-FST-DIRECT-001` 是该 Goal 的组成部分；Goal 状态保持 active，直至本门禁与其余 P0–P7 条件同时满足。

2026-08-10 用户再次以“不得退化到用 FST 做分析，只需要并且也必须适配 FST 波形”确认本门禁。该表述已逐项固化在 Goal 权威附件的“用户再次确认的解释锁”中；后续所有计划、实现、测试、提交和交接必须同时证明“FST 是唯一且必须支持的波形输入”与“FST 不是分析引擎”，缺少任一项均视为 Goal 漂移。

上述两项证明是 `GOAL-FST-DIRECT-001` 不可拆分的双断言，不允许在上下文压缩、阶段切换或最终报告中只保留“FST-only”这一半。每个批次都必须回答两个独立问题：实际打开的唯一波形是否为原始 `.fst`；driver/load、active-driver、chain、X-origin、协议、表达式、窗口和统计等分析是否仍由 xdebug action 语义结合必要的 Verilator DesignDB 静态事实承担。任一答案为否，该批次立即判定漂移并不得提交。

## 一、强制执行顺序

进入实施阶段后，必须严格按照以下顺序开始，不得提前修改其他源码。

### 第一步：原样建立任务书

1. 创建目录：

   `${REPO_ROOT}/doc`

2. 创建任务书：

   `${REPO_ROOT}/doc/XDEBUG_FULL_PARITY_TASKBOOK.md`

3. 将本 `<proposed_plan>` 标签内部的全部 Markdown 正文一字不差地写入任务书：

   - 不删减
   - 不摘要
   - 不改写措辞
   - 不调整阶段顺序
   - 不把计划和进度混写
   - 不在任务书中追加临时调试记录

4. 另建进度文件：

   `${REPO_ROOT}/doc/XDEBUG_FULL_PARITY_PROGRESS.md`

   进度文件记录每阶段状态、commit SHA、测试命令、执行环境、通过数量、失败原因和剩余差异。

### 第二步：建立详细 Goal 并进入 Goal 模式

任务书落盘后，立即调用 `create_goal`，不设置 `token_budget`。Goal objective 必须完整使用以下内容：

```text
实现 ${REPO_ROOT}/doc/XDEBUG_FULL_PARITY_TASKBOOK.md 中定义的全部计划，使开源 xdebug-fst 在当前冻结的 xdebug v1 基线上与原版 xdebug 达到完整能力兼容。

执行范围包含 ${REPO_ROOT}、${WELLEN_HOME} 和 ${VERILATOR_HOME} 三个独立 Git 仓库。必须保护三个仓库现有用户修改，不得 reset、覆盖或丢弃未提交内容；不得创建顶层 monorepo，不使用 submodule，不创建 pull request，不推送远端。所有 Git commit 必须使用详细中文标题和中文正文，按可独立验证、可单独回滚的小批次提交。

兼容金标准为当前 ${XDEBUG_ORIGINAL_ROOT}/xdebug：版本 0.1.0、当前运行时 build id、当前 schema revision、严格 73 个公开 action。原版仓库只读，不得修改。请求与响应必须通过同一套 xdebug v1 JSON Schema；忽略 PID、耗时、build id、临时路径和后端数据库路径等明确登记的易变字段后，所有确定性 summary、data、error、findings、warnings、完整性字段和 suggested_next_actions 必须语义一致。不得保留额外公开 action，不得以仅 action 名存在、简化返回、stub schema、smoke test 或宽松字段比较冒充兼容。

必须先冻结原版 action catalog、全部 request/response schema、原版 build/schema revision 和归一化规则，再迁移 MIT 许可的公共协议核心，包括 action registry、严格 schema 校验、canonical response envelope、错误合同、XOUT、one-shot、stdio-loop、session registry、engine 生命周期以及 UDS transport。根据 2026-08-09 用户范围变更，TCP 与 file transport 不实现、不测试，也不作为完成门禁；schema 中保留原版 enum 仅为协议查询兼容，实际请求必须明确返回不支持且不得 fallback。不得复制、提交或分发任何 Synopsys proprietary header、library、FSDB、daidir、文档、生成物或链接 proprietary runtime 的二进制。

必须完整实现全部 73 个 action，不区分 stable 与 experimental。value.at 必须支持 signal/list/APB/stream/AXI selector、time/times、clock sampling、sample_point、value_format、render_time_unit 和严格时间单位。必须完整实现 scope、signal、list、event、expression、verify、window、counter、APB、AXI、stream、cursor、export、session、trace.active_driver、trace.active_driver_chain 和 trace.x_origin 的当前合同、错误语义、limits、truncation 和 completeness。

Wellen 侧必须补齐 signal ref 0、递归层级、alias、timescale、四态值、real/string/event、delta-cycle、before/after sampling、批量访问和真实非零测试数量。Verilator 仓库修改必须尽可能克制：优先在 xdebug-fst 和现有 XDD 数据上解决问题；只有先建立失败差分用例并证明现有 XDD ABI 无法提供必要事实时，才允许修改 Verilator。Verilator 修改必须是最小、附加、向后兼容和局部化的，不进行无关重构，不改变普通 Verilator 行为，不扩大到无关编译阶段。每项 Verilator 修改都必须有独立回归，simple/full/UART/metadata 测试必须真实执行并通过。

FST 是唯一波形输入格式，但不得把 FST 预处理或转换成离线分析数据库。生产 action 必须由 Wellen 在会话中直接打开原始 `.fst` 并按需读取波形事实，再与独立的 Verilator DesignDB 静态事实组合；不得把 FST 转成 VCD、JSON、私有索引或全量内存快照，不得在能力缺失时切换 backend 或 fixture。

本仓库及其 Wellen、Verilator、xdebug-fst 测试默认在沙箱内运行，因为开源实现本身不依赖 license。若命令出现异常，必须先保存原始命令、退出码、stdout/stderr 和环境摘要；确认异常可能来自沙箱的工具可见性、路径、网络、进程、IPC 或运行环境限制后，可以使用完全相同的命令和测试层级在沙箱外重试。沙箱外重试不得更换 EDA 工具、数据源、transport、backend、fixture 或测试目标，不得把切换环境冒充功能修复。若沙箱内外结果不同，必须记录差异和根因；只有可解释且可重复的结果才能作为验收证据。

Git 中只保存合法的源文件、请求、配置、去敏归一化 JSON golden 和可重复生成脚本，不保存 proprietary database。

完成条件是：严格 73 个 action；全部冻结 schema 可加载且成功/错误响应均通过；73 个 action 均有正例、非法请求、资源缺失、空结果、边界、limits/truncation/completeness 测试；原版与 xdebug-fst 的归一化差分全部通过；one-shot、XOUT、stdio-loop、UDS、MCP direct 和 fake-LSF 全部通过；TCP/file 请求稳定返回已登记的不支持错误且绝不 fallback，但不实现相应 server；active-driver 与 X-origin 在多 driver、控制分支、NBA、跨端口、alias 和 X 传播场景与原版一致；Wellen 和 Verilator 测试不是 0 tests；ASan/UBSan、并发 session、异常退出和资源泄漏门禁通过；三个仓库工作树干净；任务书、进度文件、依赖 SHA、测试证据和最终验收报告齐全。

只在上述全部条件真实满足、没有剩余必需工作时将 Goal 标记为 complete。不得因为阶段完成、已有测试通过、预算接近耗尽或存在困难而提前完成 Goal。
```

Goal 创建成功后进入持续 Goal 模式，直到全部验收完成。只有任务真正全部完成时才调用 `update_goal(status="complete")`。

### 第三步：建立分支并提交任务书

- `xdebug_fst`：从 `1009e5c` 创建 `feature/full-xdebug-parity`
- `wellen`：从当前 `main` 创建 `feature/xdebug-fst-capi`
- `verilator`：继续使用 `feature/design-db-for-xdebug`
- 不创建顶层 Git 仓库
- 不 push、不创建 PR

首个提交：

```text
文档：建立 xdebug 全能力兼容任务书

记录当前原版 xdebug 的兼容目标、分阶段实施顺序、跨仓库约束、
测试门禁、提交策略和最终完成条件。

任务书作为 Goal 执行期间的固定规范，后续进度和测试证据单独维护，
避免实施过程中静默缩减范围或改变兼容标准。
```

## 二、目标与验收基线

以当前原版 xdebug 为唯一金标准：

- 版本：`0.1.0`
- 当前运行时 build id：`8eecf71271cc-c4509904...`
- 当前 schema revision：`c45099040abf...`
- 公开 action：严格等于 73 个
- 73 个 action 全部实现，不区分 stable/experimental
- 请求和响应通过相同 schema
- 剔除登记在白名单中的易变字段后，确定性结果语义完全一致
- 不允许额外公开 action、schema stub、简化 envelope 或宽松兼容判断

冻结基线时必须保存完整 SHA，不使用省略形式；同时记录：

- 原版 Git HEAD
- 原版工作树状态
- 原版二进制 build id
- schema revision
- 73 action catalog hash
- 146 个 schema 文件的整体 hash
- Wellen 基础提交
- Verilator 基础提交
- xdebug-fst 基础提交

## 三、测试执行环境规则

### 默认执行方式

- xdebug-fst、Wellen、Wellen C API、wellenx、Verilator DesignDB、CMake、pytest、Cargo、ASan、UBSan、MCP 和 transport 测试默认在沙箱内运行。
- 不因为命令名称、Verilator 使用场景或历史 EDA 习惯而预先切换到沙箱外。
- 每条正式验收命令都记录：
  - 完整命令
  - 工作目录
  - 关键环境变量名称，不记录 secret 值
  - 执行环境为 sandbox 或 outside-sandbox
  - 退出码
  - 测试数量
  - 失败摘要
  - 对应 commit SHA

### 异常后的沙箱外重试

只有出现以下类型异常时，才允许尝试沙箱外运行：

- 工具在沙箱内不可见，但宿主环境已安装
- 动态库、PATH 或只读挂载导致启动异常
- UDS、TCP、共享目录或子进程受到沙箱限制
- 网络、token、IPC、hostname 或进程权限明显受限
- 同一命令在历史宿主环境可运行，但沙箱内出现环境层错误
- 错误发生在测试启动层，而不是功能断言层

沙箱外重试必须满足：

- 使用完全相同的源码 commit
- 使用完全相同的测试命令
- 使用完全相同的 fixture
- 使用完全相同的 backend、transport 和数据源
- 不修改测试期望
- 不降低测试层级
- 不跳过失败用例
- 在进度文件记录沙箱内失败和沙箱外结果
- 对两种环境结果不同的原因给出可验证解释

以下情况不得通过沙箱外重试掩盖：

- schema 不匹配
- action 返回错误
- 数据或语义差分
- 崩溃
- 内存错误
- timeout 源于死循环或性能退化
- fixture 设计错误
- 测试断言失败
- 未实现功能

### 验收证据优先级

1. 沙箱内稳定通过
2. 沙箱内环境异常、同命令沙箱外稳定通过且根因明确
3. 单次偶然通过不作为验收证据
4. 通过更换工具、后端或数据源得到的结果不作为同一测试的通过证据

## 四、Git 与提交规则

### 仓库边界

保留三个独立仓库：

- `xdebug_fst`
- `wellen`
- `verilator`

在 `xdebug_fst` 中增加依赖锁定文件，记录三个仓库 SHA 和原版基线。原版 `${XDEBUG_ORIGINAL_ROOT}/xdebug` 始终只读。

### 提交要求

- 每个 commit 只解决一个合同或能力族
- 每个 commit 提交前运行对应测试
- 标题和正文全部使用中文
- 正文必须写明背景、实现内容、兼容影响和测试结果
- 不使用笼统的 `fix`、`update`、`misc`
- 不把测试失败状态提交到阶段完成点
- 跨仓库修改按照 Wellen/Verilator 提供能力、xdebug-fst 消费能力的顺序提交
- 每阶段结束打本地 annotated tag：`parity-p0` 至 `parity-p7`

### 禁止提交内容

- `.so`
- `.o`
- `.a`
- 可执行文件
- `obj_dir`
- FSDB
- daidir
- EDA/NPI headers 和 libraries
- proprietary 文档或样例
- 本地日志、缓存、session 目录
- 本机绝对路径生成物

FST 仅允许提交小型、确定性、可由仓库内脚本重建并带 hash 的 fixture。

## 五、分阶段实施与提交批次

### P0：冻结基线并整理仓库

#### Wellen

1. 补充 `.gitignore`，排除 `test_capi`、target 和查询工具生成物。
2. 审查当前未提交文件，只提交源文件、Cargo 配置和测试源码。
3. 为 `wellen_capi` 和 `wellenx_capi` 增加实际运行的 Rust/C ABI 测试。
4. 测试必须断言 signal ref 0、时间表、层级、值读取和错误处理，禁止 `0 tests` 作为通过。
5. 默认在沙箱内运行 Cargo 和 C ABI 测试；只有环境层异常时才按统一规则在沙箱外重试。

提交：

- `构建：整理 Wellen C API 工作区并排除生成产物`
- `功能：建立可测试的 Wellen C 波形访问接口`
- `测试：增加 Wellen C ABI 端到端验证`

#### Verilator

1. 审查现有 `--design-db` 修改和未跟踪文件。
2. 删除或忽略 `sim_fst`、`simx.fst`、`.bin`、临时 C 程序和其他构建产物。
3. 将 CLI 选项、XDD API、emitter 和测试分开提交。
4. 修复 `t_xdd_trace_full.py` 共用 prefix/obj_dir 导致测试未真正执行的问题。
5. simple、full、UART、metadata 必须成为独立用例并分别报告结果。
6. Verilator 回归默认在沙箱内运行；若启动、动态库或进程环境异常，再使用同命令沙箱外重试。

提交：

- `功能：增加最小化 Verilator DesignDB 生成接口`
- `测试：修复 DesignDB 全量回归并覆盖四类设计`

#### xdebug-fst

1. 保存原版 catalog、schema、许可和 hash。
2. 建立依赖锁定文件。
3. 建立自动检查脚本，检测原版基线是否漂移。
4. 建立归一化字段白名单，默认只允许 PID、耗时、build id、时间戳和临时路径等易变字段。

提交：

- `测试：冻结原版 xdebug v1 兼容基线`
- `构建：锁定 Wellen 与 Verilator 兼容版本`
- `测试：增加兼容基线漂移检查`

P0 完成后更新进度文件并创建 `parity-p0` tag。

### P1：迁移原版公共协议核心

从 MIT 许可的原版迁移公共核心，不复制 proprietary 依赖。

1. 引入原版使用的 JSON Schema validator。
2. 所有请求必须在 handler 执行前校验。
3. 所有成功和错误响应必须在输出前校验。
4. 迁移 action registry、metadata、resource variant 和完整 catalog。
5. 公开 action 严格为 73 个；删除公开的 `clock_point_query`。
6. 迁移 canonical response envelope：
   - `api_version`
   - `ok`
   - `action`
   - `tool`
   - `session`
   - `summary`
   - `data`
   - `findings`
   - `warnings`
   - `suggested_next_actions`
   - `error`
7. 迁移原版错误码、错误层、recoverable、invalid_arg、expected、schema_path 和 correct_example。
8. 对齐 one-shot JSON、默认 XOUT、stdio-loop ready/envelope、解析错误和退出码。
9. schema action 返回完整 schema，不允许返回占位对象。
10. actions action 支持当前原版过滤、verbose、modes 和 metadata。

提交：

- `功能：引入 xdebug v1 请求响应合同校验`
- `功能：对齐原版七十三项 action 注册表`
- `功能：统一 xdebug 响应封装与错误合同`
- `功能：对齐 xdebug 命令行与 stdio-loop 协议`
- `测试：建立全量 schema 合同门禁`

P1 验收：

- 原版 `actions` schema 验证 0 错误
- xdebug-fst `actions` schema 验证 0 错误
- 73 action 完全相等
- 146 个 schema 全部可加载
- 未知字段、互斥字段、类型错误和缺失字段均在 handler 前拒绝
- `schema` 与原版归一化输出一致

完成后创建 `parity-p1` tag。

### P2：实现真实 Session 和 Transport

范围修订（2026-08-09，用户明确指示）：只实现 UDS 与现有 stdio 生命周期；TCP 和 file transport 不需要，不得继续开发。冻结 public schema 仍保留原版 `transport` enum，选择 `tcp` 或 `file` 时必须 fail closed 返回明确错误，不自动降级到 UDS。

1. 迁移 session registry 和 generation 管理。
2. 实现 opening、alive、closed、failed、cleanup_failed 等状态。
3. session 元数据使用锁和原子替换持久化。
4. 实现真实 engine 子进程，不再把 `--server` 当作 stdin one-shot。
5. 实现 UDS transport。
6. UDS transport 失败时直接返回对应错误，不自动 fallback。
7. 对齐：
   - `session.open`
   - `session.list`
   - `session.doctor`
   - `session.close`
   - `session.kill`
   - `session.gc`
8. 覆盖重复名称、并发打开、generation 冲突、启动超时、进程崩溃、残留记录和补偿清理。
9. public target 只使用原版字段：
   - `target.fsdb` 接受 FST
   - `target.daidir` 指向 Verilator DesignDB bundle
   - bundle manifest 唯一指定 `.so`
   - 删除 `target.design_db`
10. 对齐 MCP direct、fake-LSF 和 SDK-free stdio-loop 生命周期。
11. UDS 和子进程测试默认在沙箱内运行；若异常属于 IPC、进程或共享目录限制，保存证据后用同命令沙箱外重试。

提交：

- `功能：实现多会话注册表与生命周期状态机`
- `功能：实现 UDS 会话服务与资源释放`
- `功能：对齐会话诊断清理与失败补偿`
- `测试：覆盖全部会话传输与异常生命周期`

完成后创建 `parity-p2` tag。

### P3：补全 Wellen 波形语义

1. 修复 signal ref 0 被当成无效句柄的问题。
2. 统一无效句柄 sentinel。
3. 实现任意深度递归 hierarchy。
4. 正确处理同一 signal 的 alias 和多层引用。
5. 枚举 interface、array、struct 的最终 leaf。
6. 读取 FST timescale。
7. 严格解析 `ps/ns/us` 和裸数字。
8. `1ns` 与 `1us` 必须映射到不同物理时间。
9. 实现 `render_time_unit=auto/ps/ns/us`。
10. 补齐四态 LogicValue、真实宽度、X/Z、real、string 和 event。
11. 支持同一时间点多 element/delta-cycle。
12. 支持 raw、before、after 和 clock sampled 观察点。
13. 实现批量 signal load/unload、批量 value、变化游标和范围扫描。
14. 正确报告 scan/analysis complete、truncated 和 width diagnostics。
15. 为深层 hierarchy、signal 0、X/Z、宽总线、非整纳秒和 delta-cycle 建立回归。

提交：

- `修复：统一 Wellen 信号句柄与递归层级解析`
- `功能：实现与原版一致的时间解析和渲染`
- `功能：补齐波形值类型与采样语义`
- `功能：完善波形批量查询与扫描完整性`
- `测试：覆盖 FST 层级时间与四态边界`

完成后创建 `parity-p3` tag。

P6 第五十七批对上述 P3 能力增加公开 action 贯通证据，而不是重复底层单元测试。
`value.at` 直接打开三份 Wellen 原始 `.fst`：string 用例在 0ps 选择同时间两个 delta 中
settled 的最后值并保留 UTF-8 与定宽尾部空格；real 用例在 1ps 返回 typed 0.1 数值语义且
不伪造 bit width；event 用例返回独立 `event` 标记而不是 missing/X。三者都通过冻结
`value.at` response schema，路径仅由 `XDEBUG_WELLEN_REPO` 绝对环境变量定位。现有生产
action、Wellen、Verilator 和 ABI 无需修改。

第五十八批进一步修复公开 `signal.changes` 对同时间 delta 的丢失。修改前 action 遍历去重
后的 time index 并对每个时间只取 raw settled 值，导致 Wellen 已保真的 0ps 两个 string
delta 被压成一行。修复改为消费现有类型化 `scan_changes`：窗口起点有原始 change 时按 delta
顺序全部保留并以第一条作为 initial；起点无 change 时仍合成该时刻 initial；物理 begin/end
继续精确过滤，`line_limit` 只裁剪响应。backend 的 scan/analysis completeness 直接进入
summary，不再由 action 硬编码。最终 0ps/0ps/10ns/20ns 四行、三次 transition 全部保留。

### P4：最克制地扩展 Verilator DesignDB

#### 强制原则

- 优先在 xdebug-fst 解决
- 优先复用现有 XDD 输出
- 没有失败差分测试，不修改 Verilator
- 能通过 xdebug-fst 组合或求值获得的事实，不扩展 XDD ABI
- 每个 Verilator 改动必须证明现有 ABI 无法提供所需确定性证据
- 只做附加、向后兼容的 API 扩展
- 不改变普通 Verilator 编译、仿真和优化行为
- 不进行无关 AST、调度器、代码风格或公共 API 重构
- 修改范围尽量限制在：
  - `src/V3Options.*`
  - `src/Verilator.cpp`
  - `src/V3EmitDesignDb.*`
  - `include/xdd_api.h`
  - 对应 `test_regress/t/t_xdd_*`
- 若必须触及其他文件，先在进度文件记录原因、最小方案和不可替代证据

#### 允许的最小扩展顺序

1. 先修复现有方向和 port connection 输出，避免 xdebug-fst 全表启发式扫描。
2. 仅在 active-driver 差分失败证明必要时，附加导出：
   - assignment kind
   - RHS signal dependencies
   - control dependencies
   - source file/line
   - process order
   - sequential boundary
3. 仅在对应测试需要时增加 alias、interface/modport/ref 边。
4. 不预先设计大型通用 IR；只加入当前 73 action 合同实际需要的最小字段。
5. ABI 增加版本字段和 capability 查询，旧 bundle 失败时明确报版本不兼容，不自动降级。
6. 每项新增字段必须有：
   - 修改前失败用例
   - 修改后通过用例
   - 旧接口兼容测试
   - 普通 Verilator 非 DesignDB 回归

提交按实际必要性拆分：

- `修复：导出精确端口方向与跨层连接`
- `功能：补充活动驱动所需最小语义证据`
- `测试：覆盖复杂驱动与跨层语义`
- `重构：使用 DesignDB 原生连接证据`

如果现有 XDD 能满足某项能力，则跳过对应 Verilator 功能提交，并在进度文件记录“不需要修改”的证据。

完成后创建 `parity-p4` tag。

### P5：实现全部公共 Action

每个批次必须同时完成实现、schema、成功响应、错误响应、边界和原版差分。

#### 发现与设计能力

- `scope.roots`
- `scope.list`
- `signal.resolve`
- `signal.canonicalize`
- `trace.driver`
- `trace.load`
- `expr.normalize`

提交：

`功能：对齐发现与静态设计 action`

#### value.at

完整支持：

- signal
- list
- APB
- stream
- AXI
- `time`
- `times`
- clock sampling
- edge
- sample_point
- value_format
- render_time_unit
- slice_hint
- 宽度完整性诊断

提交：

`功能：完整实现 value.at 多源批量查询`

#### 集合、事件和验证

- list create/add/delete/load/show/validate/export/first_change
- waveform cursor set/get/list/delete/use
- event config list/load/find/export
- expr eval
- verify conditions
- window verify
- signal changes/statistics/stability/xz/anomaly/sampled pulse
- counter statistics
- protocol handshake inspect

提交：

`功能：对齐集合事件表达式与窗口分析`

#### APB

完整实现：

- config list/load
- query
- statistics
- transaction cursor
- transfer window
- direction
- exact/range/mask address
- protocol error
- limits 和完整性

提交：

`功能：完整实现 APB 事务能力`

#### AXI

完整实现：

- config list/load
- query
- analysis
- export
- statistics
- transaction cursor
- channel stall
- latency outlier
- outstanding timeline
- request/response pair
- 多 ID
- 多 beat
- 乱序
- backpressure
- burst 和 response error

提交：

`功能：完整实现 AXI 事务与性能分析`

#### Stream

完整实现：

- config list/get/load
- describe
- validate
- query
- export
- transfer
- stall
- packet
- 动态字段 exact/range/mask
- full/range base cache scope

提交：

`功能：完整实现通用流接口分析`

#### 导出能力

- list export
- event export
- AXI export
- stream export
- NWave RC
- 安全 artifact 路径
- 原子写入
- preview、format 和 overwrite 合同

提交：

`功能：对齐导出与波形视图能力`

每批迁移完成后立即删除相应旧简化 handler，禁止新旧实现长期并存。

完成后创建 `parity-p5` tag。

### P6：活动驱动与 X 根因能力

当前实施检查点（2026-08-10）：已先提交 counter `if/else` 修改前失败证据，并证明
原 XDD 的 source/role/file/line 无法表达 then/else 极性；Verilator 随后只附加 driver
activation predicate capability 和只读访问器，ABI 仍为 v2。xdebug-fst 在目标
`active_time` 通过 Wellen 直接按需读取当前原始 `.fst` 的 predicate 叶子并做四态求值，
不生成中间波形、离线索引或全量快照。缺 predicate、解析失败、信号缺失或非 wildcard
谓词无法归约为已知真假时必须 unresolved/fail closed，禁止选择静态首项；casez/casex
中的 X/Z 则按对应通配规则求值。随后又用真实 APB FST 验证嵌套
`presetn && psel && penable && !pwrite` 条件、NBA、数组 RHS 和端口归一化，唯一选中
`apb_top.sv:25`；用独立 case FST 验证普通 case item/default 分别唯一选中
`case_top.sv:14/:15`（固件扩展 wildcard 输出后对应行号为 `:16/:17`）。这些运行时值
全部由 Wellen 直接读取 FST，且两项验证都没有修改
Verilator。第三批在双重修改前失败证据后，以 XDD 内部 `==?z`/`==?x` 谓词保留
`casez/casex` 静态匹配种类，并由 xdebug 使用 Wellen 读取的真实四态 FST 值求值；ABI
仍为 v2，未修改普通 Verilator 行为。此检查点关闭已验证的 `if/else`、APB 嵌套条件、
普通 `case/default`、`casez/casex` 以及 V3Inst 折叠后的同目标嵌套条件基础语义；
第四批之后又以单行 NBA 三元赋值证明 `(file,line,kind)` 不足以区分 lowering 后的
信号/常量叶子，并仅在 xdebug-fst consumer 将 predicate 纳入语句身份；真实 FST 已分别
选中信号 RHS 和常量 RHS 分支。该用例关闭基本常量叶子与同源行分支身份，但
随后以既有 counter FST alias 和 DesignDB 对称端口边验证内部 input 只向父级上溯，并在
顶层 primary input 终止，关闭基本 input alias/跨端口反向折返；再按冻结原版排除目标自引用
RHS 的规则，将 `count <= count + 1` 的活动 NBA 正确终止为
`assignment/constant_or_no_rhs_signal`，关闭基本 NBA self-RHS 分类。第八批先以真实 FST
建立双连续赋值失败证据，再仅在 `--design-db` 下旁路保存被 V3Tristate 删除的同强度
非三态连续赋值静态描述，不保留 AST、不改变普通仿真或 XDD ABI；chain 已按原版合同报告
两条活动语句的 `ambiguous/multiple_active_candidates`。第九批又在独立失败证据后，以现有
predicate 字符串表达 case inside 的仅 item 侧通配和闭区间，并由 xdebug 使用 Wellen
直接读取的真实 FST 值判定；第十七批在普通仿真和 DesignDB 双重失败证据后，仅关闭
`case matches` 的精确表达式 item/default 子集：Verilator 发布 `===` 静态谓词，xdebug
使用 Wellen 从当前原始 FST 直接读取的 selector 在 active time 判定。第二十五至第三十一批
继续按修改前失败证据分片关闭无绑定 pattern：`case matches` 的直接顶层点星与 packed
assignment pattern，以及独立 `matches` 的精确标量、无绑定 packed assignment pattern 和
直接顶层点星均已通过普通仿真；独立精确真/假与顶层点星又以同步生成的 DesignDB 和原始
FST 完成 `trace.active_driver` 动态闭环。这里 FST/Wellen 仍只提供运行时 selector/value，
pattern 语法和静态谓词属于 Verilator，合同判定属于 xdebug action。第三十六批又在普通
仿真、DesignDB 和 xdebug 动态三层失败证据后关闭仅 `default` 的 `case matches`：Verilator
只移除“必须存在精确 item”的误门禁，DesignDB 为唯一 default 分支发布恒真谓词，action
结合 Wellen 按需读取的当前原始 FST 值完成 45ps/65ps 闭环。该批结束时 tagged union、tagged
expression/pattern、pattern variable/binding 与嵌套 wildcard 仍明确不支持；第五十九批随后
已用精确 value/mask 关闭无 binding packed 嵌套 wildcard，但 PatternVar/tagged 仍未完成，
不得冒充通用 matches 完成。第十八批又以等价 NBA 时序固件证明“最近赋值事件”不能退化为
FST 的“最近值变化”：同值 NBA 在 20ps/40ps/60ps 均执行，修改前 65ps 查询只得到 20ps。
Verilator `01f9f2a4b` 先以独立失败回归锁定简单 posedge 与异步 reset 的完整敏感事件需求，
`6239de45e` 再只通过既有 driver role 发布 direct `event_*` 静态事实，不改 ABI 布局、仿真
调度或 pass 顺序。xdebug 让 Wellen 按需读取当前原始 FST 的明确时钟/复位边沿，恢复 60ps
并传播过直接连续 alias；禁止按最终值、固定周期、任意时钟或离线事件索引猜测。
第十九批继续以两级连续 alias 建立 consumer 修改前证据：静态链和原始 FST 都已充分，但
当前只向前查看一层，前三跳仍为 20ps。修复只能沿 DesignDB 唯一 `cont_assign/rhs` 有界
递归到 NBA event，不得用 FST 值相等发现 alias，也不得为此扩展 Verilator 或 Wellen。
实现以请求 `max_nodes` 为静态前瞻预算并维护 signal visited set；只有谓词可解、唯一活动
statement、连续赋值、唯一 RHS 四项同时成立才继续，到达唯一 NBA `event_*` 后才传播时间。
两级 alias 已全部恢复 60ps；歧义、环、缺失或非连续边界一律停止而不 fallback。
第三十七批进一步区分“同值数据赋值”和“纯自保持赋值”：`q <= q` 在新 posedge 执行时
不能成为新的数据根因。只有 DesignDB load 精确证明同目标、同文件、同行 self RHS，且该行
只有一个静态 statement identity 时，action 才沿明确 `event_*` 对当前原始 FST 时钟边沿
有界回溯；同源行混合叶子、常量、缺失或不唯一证据均不猜测。`max_nodes` 同时约束回溯，
耗尽明确返回 limit。65ps 已跳过 60ps self-hold，恢复 40ps 数据赋值和 primary input 链；
这不是按目标值是否变化推断，也没有建立事件索引或修改 Verilator/Wellen。
第三十八批又用无 else 的门控 NBA 验证相邻边界：60ps predicate 为假时 statement 根本没有
执行，目标原始 FST 的最近实际变化观察点仍为 40ps，现有 action 直接在该点选择数据赋值，
不需要套用 self-hold 回溯。门控空事件和活动 self-assignment 必须保持两类，不能仅凭“值
未变化”合并；同步固件后 action、Verilator、Wellen 和 ABI 均无需修改。
第三十九批关闭同源行三元 self-hold：无 predicate 的 load 文件/行不足以区分 self 与 data
叶子，因此 Verilator 只对“叶子本身恰好等于目标”发布 predicate-local `self_rhs`，而
`q+1` 等表达式不标记。xdebug action 只用该角色证明纯保持，绝不把它当上游 RHS；45ps
已跳过 40ps self 叶子并回到 20ps data 叶子。该附加角色不改变 C ABI 函数签名、普通仿真
或 Wellen，运行时边沿仍由 Wellen 从当前原始 FST 按需读取。
第二十批进一步用不与 posedge 重合的 30ps `negedge async_reset_n` 写入同值，证明 consumer
会从 DesignDB 的多敏感项中选择真实最近事件，并在该时刻判定复位常量分支；现有实现
直接返回 30ps 并按常量 assignment 终止，因此不修改任何仓库算法或 ABI。
第二十一批审计 force：Verilator 先用独立失败回归证明 kind 丢失，再仅把
`AstAssignForce` 通过现有字符串标为 `force`。真实 FST 中 force 与底层 NBA 同时可见时，
当前 consumer 错误按普通双 driver 报 ambiguous；冻结原版要求活动 force 优先并就地终止。
修复只属于 action 分组优先级，FST 不负责识别 force，Wellen 与 ABI 不再修改。
实现先在已求值活动 group 中筛选 force：唯一 force 覆盖底层 assignment 并就地终止，其
RHS 只作证据；多个 force 仍报多候选。release 后 force predicate 失活，chain 恢复底层
NBA。所有状态判断来自 DesignDB 静态类型/谓词/事件与原始 FST 控制边沿，不从值猜 force。
第二十二批证明 `trace.x_origin` 仍有独立 force 缺口：合法原始 GCD FST 加静态 force 事实
后，当前返回 `candidate_x_source`，而冻结原版要求当前信号为 proven `force_x` origin。
修复必须在 X-origin 自身应用活动 force 优先级，不能假定 chain 修复会自动覆盖。
实现让活动 force 在普通 unresolved/依赖枚举之前终止 DFS，origin 和当前 hop 都引用 force
源码，RHS 不再展开；chain 也不再让无关普通 driver 的未决谓词覆盖已明确活动的 force。
第二十三批覆盖最后一个 force 消费入口 `trace.active_driver`：修改前它仍把 force 与底层
NBA 同列并报告 assignment/2 paths；冻结原版要求保留 termination=force 且只投影活动
force 路径。修复只复用既有静态 group/predicate，不增加任何后端事实。
实现存在活动 force 时只投影 force groups，summary 保留 `force/force`；多个 force 全部保留并
走既有结果上限裁剪。底层 assignment 与其未决谓词不再污染已解析 force 结果。
第二十七批进一步覆盖同一过程先 blocking 后 NBA 的调度边界：只有恰好一条活动 NBA 且
没有未决 NBA 时，NBA 才覆盖同目标非 NBA；多条或未决 NBA 必须继续歧义。第三十二批用
同一 posedge 过程连续两条 NBA 验证后一条规则，冻结原版在精确活动时刻发现两条
assignment-like handle 时同样返回 `multiple_active_candidates`。同步生成的原始 FST 与
DesignDB 已让 chain 保留第 88/89 行两条候选；禁止按源码最后一条、最终 FST 值或值相等
关系任选。两批均由 DesignDB 提供静态语句/事件，Wellen 只从当前原始 FST 按需提供边沿
和值，调度合同由 xdebug action 执行。
第十批进一步关闭基本 inout alias：DesignDB 只用替换型静态描述恢复被
tristate lowering 遮蔽的原始 RHS，xdebug 则沿真实 FST alias 从子端口追到父级 primary
input。第十一批在同一原始 FST 中增加父级 net、`inout_mid.bus`、中间 `leaf_bus` 和
`inout_leaf.bus` 组成的真实两级 inout 网络；当前锁定的 DesignDB 已完整发布各级静态
端口/赋值关系，既有 xdebug action 沿 Wellen 按需读取的六个 FST 层级名/alias 正确上溯到
顶层 primary input，因此无需修改 Verilator、Wellen 或 consumer。该证据关闭“基本多级
inout 跨端口链”，但不能外推为任意双向驱动。更多连续/过程/NBA 时序边界与常量组合、
复杂 output/inout alias，以及条件/过程/跨层多驱动差分仍是 P6 必做项。

第十二批已建立 output 边界修改前失败证据。冻结原版 P0-2 module-boundary 用例要求
`parent net → child output → child input → parent input`，不能把模块边界全部折叠成等值
alias。当前 DesignDB 对等价 `output_leaf` 的 lowering 后 RHS 直接指向顶层 `data`，使
xdebug 返回 `child_output_bus → top.data` 并跳过三个层级节点。FST 中的等值 alias 只
证明运行时值，不能用于反推 HDL 端口关系；修复必须来自既有 DesignDB 静态记录的严格
consumer 组合，或在证明确有静态事实缺口后对 Verilator 作最小、`--design-db` 专用的
附加保留。禁止为了补 hop 扫描 FST 寻找同值信号。

审计结果证明这一用例不需要修改 Verilator：XDD 已分别保留 output/input 端口的局部连接、
父 net 上带原始源行的扁平化 RHS，以及所有端口方向。xdebug consumer 仅在静态事实能
唯一组合时恢复边界：父 net 进入唯一更深 output port；output 的回边通过父 net 唯一活动
赋值映射到同一实例的唯一 input port；该 input 的扁平化源再经过最近祖先 input port。
最终链恢复为 `child_output_bus → u_output.data_o → u_output.data_i → case_top.data →
top.data`。若任一步不唯一，不得靠 FST 同值搜索猜测。该批关闭基本单输入/单输出模块
边界。第十六批又验证复杂 output 表达式必须先进入子 output，再以同实例多个 input port
报告一个 statement 的多 RHS；第三十三批验证同一实例两个独立 output port 共同驱动父 net
时必须保留两条活动 statement，不能按端口顺序、FST 值或可读性合并。至此单 output 多 RHS
和同实例多 output 两种基础组合已关闭。第三十四批进一步关闭带条件的单 output 基础边界：
Verilator lowering 谓词引用内部一位信号，而原始 FST 保存等价子模块 input port；consumer
只能依据 DesignDB 已有端口边，在位宽一致、波形可读且候选唯一时改写谓词引用，任何缺失
或歧义继续 `predicate_unresolved`。活动信号分支映射到同实例 input 后继续上溯；活动常量
分支必须在子 output 保留父 statement 的精确源码行并终止，不能沿父子 alias 折返。该修复
不构成 FST 分析：FST/Wellen 只提供被静态选定端口的运行时值，候选唯一性、谓词和链合同
由 DesignDB 与 action 负责。第三十五批再证明 output 边界优先只能覆盖父级恰好一个活动
statement 的扁平化视图；子 output 与父级赋值共同驱动内部 net 时，DesignDB 中两条活动
statement 必须在父 net 保持 `multiple_active_candidates`，不能因唯一 output 端口而清空，
更不能按两个 RHS 的相同 FST 值合并。跨层 output/inout 混合反馈和更复杂多驱动组合仍须
独立差分。

第十三批在同一真实 FST 固件中加入两个独立 `always @(posedge clk)` 对同一 reg 的 NBA：
第一条受 `!reset` 控制，第二条受 `sel[0]` 控制。25ps 只有第一条活动，必须唯一返回第
83 行；45ps 两个 predicate 同时为真，chain 必须返回
`ambiguous/multiple_active_candidates`，并保留第 83/87 行两条语句。当前锁定的
DesignDB 与 action 已直接通过，因此不修改 Verilator/Wellen/consumer。该证据关闭基本
“互斥与重叠条件的双过程 NBA driver”，但不关闭跨层、连续+过程混合、不同调度区或
时钟域竞争。

第十四批建立跨实例 output 多驱动失败证据：两个 `output_leaf` 实例的同源第 118 行分别
形成一条父 net driver，静态 RHS 与端口连接足以区分 `u_output_a/u_output_b`。当前仅按
`(file,line,kind,predicate)` 聚合会把它们误当成同一表达式的两个 RHS，返回
`multiple_rhs_sources` 而非两条活动候选。修复只能从既有 DesignDB output/input 边恢复
实例 identity 并纳入 statement key；FST 值、alias 可读性和内部临时信号都不得用于决定
静态语句数量。这个修复不需要也不允许扩大 XDD ABI。

实现只在 xdebug-fst 内部 `DriverRecord` 增加 consumer-only `statement_identity`。对同一父
net 的多个 output port，consumer 以每个 output 的实例作用域收集同实例 input port 连接；
只有同一基础 statement 的所有依赖都能唯一映射、且确有两个以上实例 identity 时，才把
该 identity 加入聚合键。这样两个实例成为两个活动候选，而单实例的多个 RHS 仍保持一个
statement。映射缺失或不唯一时不任选，不依据 FST 可读性裁剪静态候选。

第十五批的嵌套 NBA 常量回归证明，常量 assignment 不能因没有 RHS signal 而丢失源码
statement。DesignDB 已在 control 记录中保存第 103/107 行和精确 predicate；当前 chain
正确识别常量终止，却把 hop 行号退化为声明第 100 行。consumer 必须让所有活动 assignment
参与源码与歧义分组，但只有 `dependency_role=rhs` 可以继续上游；control 记录只能作为
常量分支的动态/源码证据。禁止伪造常量波形信号或沿 control 当数据源追踪。

最终 consumer 纳入三类活动 group：含 RHS、`kind=nba`、或至少有一个 control dependency。
这覆盖有 control 的常量叶子与无 control 的直接 NBA，同时排除 lowering 生成的裸
`statement`-only `proc_assign`，避免在 inout 链制造假 driver。`representative_driver`
只负责 hop 的 file/line evidence；其 role 不是 `rhs` 时，chain 必须就地按 assignment
终止，不能沿 control 或 statement source 继续。

第十六批建立复杂 output 表达式边界证据。父 net 上的 lowering driver 虽有两个 RHS，但
原版链必须先显示 `parent net → child output`，随后在子模块第 143 行以一个 statement 的
`multiple_rhs_sources` 报告 `data_i/sel_i`。现有 XDD 已分别保存 output→parent、两个
input→flattened source 和父 statement RHS，故无需扩大 ABI。consumer 必须让唯一 output
边界优先于父 net 的扁平化歧义，并在 output 实例内把每个 RHS 唯一映射回 input port；
映射不完整或不唯一时不得用 FST 同值/可读性猜测。

实现中仅当父级 predicate 无 unresolved 且唯一更深 output port 的 FST hop 可读取时，才让
边界优先于父 lowering driver；predicate 未决仍在父级失败关闭。进入 output 后要求父级
恰有一个活动 statement，且其中每个 RHS 都能在同一实例找到唯一 input port，才整体替换
signal index。任何一个 RHS 映射失败都不会部分改写。完成映射后复用既有
`multiple_rhs_sources` evidence，保证普通单实例表达式仍是一个 statement。

第五十二批关闭基础 interface/modport source/sink 成员边界。冻结原版 composite chain
要求路径显式跨过 sink/source 两侧 interface 成员，不能把共享 `bus.data` 当作全部层级。
修改前锁定 Verilator 只发布零宽 interface port 和共享成员，FST 虽保真包含
`u_sink.bus.data/u_source.bus.data` alias，也只能证明运行时值，不能承担静态成员分析。
因此先在 Verilator 仓库提交独立失败回归，再把唯一获准改动限制在 `--design-db`：于
`V3Scope` 后、`V3LinkDot` 前只读捕获随后会消失的 `AstAliasScope`，精确保存 modport
端口、实际 interface scope、成员与方向；发射阶段只向既有 XDD v2 表追加实例成员信号、
`interface_modport_member` 连接和 source 成员已有驱动，不修改 AST、普通仿真、FST 生成、
ABI 或 capability。xdebug action 只在唯一活动连续 statement、唯一最深 output 和唯一
同实例 input 映射均成立时跨边界；NBA/过程赋值、多个最深候选、不可读或映射不唯一均
保持原合同或失败关闭。

本批同时重申 `GOAL-FST-DIRECT-001` 的双断言：Wellen 的输入仍只能是当前 session 的原始
`.fst`，它按需读取六个已由 DesignDB/action 静态选定的 hop 值；interface/member 结构、
方向、driver 和边界唯一性全部来自 Verilator DesignDB，chain 顺序和终止来自冻结 xdebug
action 语义。禁止扫描 FST alias 或相同值反推 interface 结构，禁止转换、预扫、私有索引、
离线数据库、全量内存快照、TCP、fileport 或 backend fallback。基础六跳 source/sink 链已
关闭；第五十四批又关闭基础 modport X-origin/node-budget，但 ref、嵌套/数组 interface、
多 interface driver、端口反馈及 node/time/depth/loop 联合限制仍须单独差分，不得据此
宣称任意 interface 能力完成。

第五十三批关闭单实例、单 direction=3 `ref` 端口的基础连续赋值链。锁定 Verilator 已经通过
既有 XDD v2 发布 ref 方向、父子连接和 driver，因此不修改 Verilator。consumer 只把 ref/
inout 加入 output 同实例 RHS 的唯一端口映射，并在从双向端口返回父 alias 后禁止反射进入
刚离开的 child output；候选不唯一时仍不选择。真实五跳链全部由当前原始 `.fst` 经 Wellen
按需取值。多 ref driver、ref 与过程/NBA/force、ref 反馈及 X-origin 预算组合仍未关闭。

第五十四批关闭 `interface_modport_member` 与基础 X-origin node 预算的组合证据。测试继续
直接打开 Wellen 已有的 GCD 原始 `.fst`，只用独立测试 DesignDB 把同一组可采样 X 信号间
的静态端口边标记为 `interface_modport_member`。`trace.x_origin` 必须把这些物理 hop
作为可见但语义透明的 `port` alias，在 `max_nodes=6` 计数前合并汇聚探索态，最终保留
`io_a` 和 `y` 两条独立 RHS 来源链，且不得产生假 `max_nodes` limitation。现有 action
直接满足合同，因此本批只增加测试证据，不修改生产 consumer、Verilator、Wellen 或 XDD
ABI。该证据只关闭基础 modport alias/node-budget 交互；嵌套/数组 interface、ref
X-origin、端口反馈以及 node/time/depth/loop 联合限制仍须独立差分。

第五十五批关闭 direction=3 `ref` 纯端口反馈的基础 X-origin 环合同。修改前 action 在沿
port 边返回已访问 X 节点时直接过滤该候选，随后把环上节点误报为 `candidate_x_source`。
初始两节点失败模型又被第五十四批 modport 回归证明过宽：双向连接返回直接父节点只是普通
alias 的反向记录，不是反馈。最终测试因此收紧为 `T_14 → GEN_0 → GEN_1 → T_14` 三节点
direction=3 静态端口环；consumer 只把返回非直接父节点的已访问候选送入既有
`loop_sources`，直接父边仍忽略。结果保留三个物理 hop，current 回到 `T_14`，以
`loop_detected` 完整终止且没有 origin。测试继续使用 GCD 原始 `.fst` 按需确认三个节点为
X，不从值相等推断连接。带 driver/分支的端口反馈以及反馈与 node/time/depth/chain 限制的
联合交互仍未关闭。

第五十六批关闭基础 ref 纯端口反馈与 `max_nodes` 的组合边界。同一三节点静态环在
`max_nodes=2` 时只能展开 `T_14` 和 `GEN_0`；进入 `GEN_1` 前必须以 `limit/max_nodes`
停止，保留 `GEN_1` current/frontier、零 completed chain、零 origin 和唯一 limitation。
默认预算下仍按第五十五批返回完整 loop。现有 action 直接通过，本批只增加合同证据；
`max_time_steps`、`max_depth`、`max_chains` 与带 driver/分支反馈的联合交互仍未关闭。

#### trace.active_driver

1. 根据当前时间的控制条件判断有效分支。
2. 处理 if/case、连续赋值、过程赋值、NBA、常量和 alias。
3. 不能选择“第一个可读取 driver”。
4. 输出 resolved、control_only、unresolved 和动态证据。
5. 保留 source file/line 和有效条件。
6. 对歧义返回 ambiguity evidence，不随意选一个结果。

提交：

`功能：实现确定性的活动驱动判定`

#### trace.active_driver_chain

1. 支持 `limits.max_depth`。
2. 支持 loop、frontier 和 max-depth 终止。
3. 支持 ambiguity。
4. 支持跨 module port、interface/modport/ref 和 alias。
5. 每一跳保留当前值、控制证据和 source evidence。

提交：

`功能：实现完整活动驱动链追踪`

#### trace.x_origin

1. 实现多分支语义 DFS。
2. 分别追踪 RHS、control、port 和时间边界。
3. 区分：
   - query_time
   - x_onset_time
   - active_time
4. 每个分支独立计算 X onset。
5. 合并纯 alias 路径。
6. 支持 max_chains、frontier、partial 和 analysis completeness。
7. 不复用查询信号宽度渲染不同宽度的上游信号。

第 48 批进一步锁定多分支环合同：当同一活动 statement 同时包含一个回到当前路径的 X
依赖和一个尚未访问的 X 依赖时，action 必须分别形成完成的 `loop_detected` chain 与继续
追踪的正常 chain。若正常分支找到来源，summary 必须为 `origin_found`，同时保留环证据、
精确 chain/origin 计数和完整性；不得静默过滤环、把环误报为候选来源，或因一条环覆盖
正常来源。测试只允许由 DesignDB 发布静态依赖、由 action 判断 `(signal,onset)` 路径状态，
Wellen 仅从当前 session 原始 `.fst` 按需确认候选 X 值。不得扫描同值信号推断依赖或环，
不得生成波形转换、事件索引或离线分析数据库。当前基础同语句 loop+normal 已关闭；基础
modport alias 与 node 预算组合在第五十四批关闭，基础 ref 纯端口反馈在第五十五批关闭；
带 driver/分支的复杂端口反馈、嵌套/数组 interface 及 time/depth/chain/loop 联合限制仍须
独立差分；基础 ref 反馈的 node 限制已由第五十六批关闭。

第 49 批关闭基础 branch+depth 组合：同一请求同时限制 `max_chains` 和 `max_depth` 时，保留
语义链必须以 `max_depth` frontier 终止，被省略语义分支必须继续出现在该链的 pending 与
branch event 中；summary 计数、完整性、frontier 值/时间和续跑建议必须一致。此合同由
action 对 DesignDB 静态依赖执行预算，Wellen 只按需读取当前原始 FST 中已选定信号，禁止
把波形扫描结果用于选择分支或预建 frontier。alias/loop 与 node/time/depth 的复杂组合仍是
P6 必做项，不能据本批宣称全部限制交互已完成。

第 50 批关闭查询晚于 X 首发的基础时间合同：summary/query 必须保留用户 query time，根
hop 和各上游 hop/current 必须分别记录自身 X onset，不能跨信号复用。Wellen 只对 action
根据 DesignDB 静态依赖选出的信号执行当前请求内的按需向前查找；禁止预扫整份 FST 生成
onset 时间表、持久化事件索引或离线数据库。冻结 query schema 不增加 onset 字段。

第 51 批关闭基础 primitive output 合同：若 DesignDB 已发布真实 primitive RHS，chain 必须
先沿静态证据追到上游，不能把根节点伪报为一跳 primary input；primary input 只能在声明
方向和无父连接事实同时成立的实际来源处终止。公开 hop 使用冻结的 file/line、signal path
字段，不得为测试扩 schema。复杂 primitive、strength 与 tristate 组合继续保留差分任务。

提交：

`功能：实现多分支 X 来源追踪`

#### 对照测试

使用 active-semantics、active-zero、interface-port、x-prop、多 driver、控制分支和 NBA fixture 建立双后端对照。

对照测试默认在沙箱内运行。如果原版工具、IPC、动态库或宿主环境在沙箱内异常，必须先保存失败证据，再用相同命令沙箱外重试；不得改变 fixture 或比较规则。

提交：

`测试：建立活动驱动与 X 来源差分回归`

完成后创建 `parity-p6` tag。

### P7：全量差分、稳定性与交付

1. 同一 SystemVerilog fixture 生成：
   - 原版参考结果所需数据库
   - 开源 FST/DesignDB
2. 所有测试默认先在沙箱内运行。
3. 如果数据库生成、原版工具启动、IPC、动态库或子进程出现环境层异常，可按统一规则用同命令沙箱外重试。
4. 不保存 proprietary database，只保存：
   - RTL
   - 构建配置
   - 请求
   - 去敏归一化 JSON golden
   - hash
5. 73 个 action 每个至少覆盖：
   - 正常成功
   - schema 非法输入
   - 资源缺失
   - 空结果
   - 边界时间
   - 多结果
   - limits
   - truncation
   - completeness
   - X/Z
6. 归一化器只忽略固定白名单字段，新增忽略字段必须单独审查和提交。
7. 运行：
   - CMake clean build
   - pytest 全量
   - Wellen Rust tests
   - Wellen C ABI tests
   - wellenx tests
   - Verilator DesignDB tests
   - 原版 differential tests
   - MCP direct
   - fake-LSF
   - UDS
   - ASan
   - UBSan
   - 并发 session
   - engine crash
   - 重复 open/close
   - 文件描述符和内存泄漏
8. 对每个沙箱外重试，在进度报告中记录：
   - 沙箱内命令和错误
   - 沙箱外相同命令和结果
   - 判定为环境问题的依据
   - 是否影响功能结论
9. 删除：
   - 旧 `render_format`
   - 多余 `clock_point_query`
   - schema stub
   - 旧简化 handler
   - 已提交构建产物
   - 过时的“全部完成”文档声明
10. 更新任务进度和最终验收报告。

#### P7 第一批：独立 sanitizer 构建门禁

2026-08-10 已完成 P7 的 ASan/UBSan 基础门禁，但这不代表 P7 或 Goal 完成：

1. CMake 增加默认关闭且互斥的 `XDEBUG_ENABLE_ASAN`、`XDEBUG_ENABLE_UBSAN`。开关位于
   所有本仓库 C/C++ target 定义之前，因此覆盖 `xdebug-fst`、CTest 可执行文件和测试用
   DesignDB 共享库；不得只 sanitizer 主程序而遗漏测试装载代码。
2. ASan 与 UBSan 必须使用两个独立绝对构建目录，不允许双开或共用 cache。ASan 使用
   `-fsanitize=address -fno-omit-frame-pointer`，验收运行时设置
   `ASAN_OPTIONS=detect_leaks=1:abort_on_error=1:halt_on_error=1`；UBSan 使用
   `-fsanitize=undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer`，验收运行时
   设置 `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`。
3. 当前 GCC 8.5 初始链接失败，原始错误为缺少 `/usr/lib64/libasan.so.5.0.0`；进一步审计
   同时确认 `/usr/lib64/libubsan.so.1.0.0` 缺失。经用户明确授权，安装与 GCC 8 ABI 对应、
   GPG 签名校验通过的 `libasan-8.5.0-28.el8_10.alma.1.x86_64` 和
   `libubsan-8.5.0-28.el8_10.alma.1.x86_64`。这只修复宿主工具链运行库，不改变编译器、
   backend、fixture、transport 或测试目标。
4. `/tmp/xdebug-fst-build-asan` 在 leak 检测和遇错即停配置下通过 CTest 6/6 与完整 pytest
   266/266；`/tmp/xdebug-fst-build-ubsan` 在遇错即停配置下通过 CTest 6/6 与完整 pytest
   266/266，均无 sanitizer 诊断。普通构建继续通过 CTest 8/8、pytest 266/266 和冻结兼容
   基线，证明默认关闭时没有行为退化。
   正式复现命令如下；CMake 从 shell 环境读取 `.codex/config.toml` 配置的
   `XDEBUG_VERILATOR_REPO`、`XDEBUG_WELLEN_REPO` 绝对仓库路径：

   ```bash
   cmake -S ${REPO_ROOT} -B /tmp/xdebug-fst-build-asan -DXDEBUG_ENABLE_ASAN=ON -DXDEBUG_ENABLE_UBSAN=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo
   cmake --build /tmp/xdebug-fst-build-asan --parallel 8
   ASAN_OPTIONS=detect_leaks=1:abort_on_error=1:halt_on_error=1 ctest --test-dir /tmp/xdebug-fst-build-asan --output-on-failure
   ASAN_OPTIONS=detect_leaks=1:abort_on_error=1:halt_on_error=1 PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 ${XFST_CONDA_ENV} -m pytest -q --xfst-bin=/tmp/xdebug-fst-build-asan/xdebug-fst

   cmake -S ${REPO_ROOT} -B /tmp/xdebug-fst-build-ubsan -DXDEBUG_ENABLE_ASAN=OFF -DXDEBUG_ENABLE_UBSAN=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
   cmake --build /tmp/xdebug-fst-build-ubsan --parallel 8
   UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir /tmp/xdebug-fst-build-ubsan --output-on-failure
   UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 ${XFST_CONDA_ENV} -m pytest -q --xfst-bin=/tmp/xdebug-fst-build-ubsan/xdebug-fst
   ```
5. sanitizer 测试继续只打开既有原始 `.fst`，由 Wellen 按需提供波形事实；sanitizer 只是
   C/C++ 内存与未定义行为检测门禁，不增加波形预处理、转换、索引、离线数据库、全量快照
   或替代分析后端，持续满足 `GOAL-FST-DIRECT-001`。
6. 本批尚未替代 P7 的并发 session、engine crash、重复 open/close、FD/长期内存泄漏、
   73-action 原版全差分及最终清洁性门禁；这些任务仍必须分别建立证据。

#### P7 第二批：并发、崩溃、重复生命周期与资源稳定性

2026-08-10 在第一批 sanitizer 基础上增加独立 `session-stability` CTest，并与既有
`session-uds-lifecycle` 组合关闭当前 UDS session 稳定性门禁：

1. 使用 8 个并行 one-shot frontend 同时创建 8 个不同名称的 UDS session，再并行执行
   doctor 和 close。必须证明 PID、socket path、内部 generation 全部唯一，registry 同时完整
   保存 8 条 active 记录，最终 8 个 engine 进程和 socket 全部消失且 registry 为零 session。
2. 使用一个长驻 `--stdio-loop --json` frontend，先执行 3 轮预热，再执行 24 轮计量的同名
   `open → doctor → close`。每一轮都必须安全复用 session id、回收当轮 child PID/socket，
   最终 registry 精确等于 `{"sessions":[],"version":2}`。
3. `/proc/<frontend-pid>/fd` 在计量前后必须精确相等。普通与 UBSan 构建的 frontend RSS
   增长不得超过 4096 KiB；ASan 因 allocator quarantine 允许 65536 KiB 仪器化上限，但必须
   同时保持 `detect_leaks=1`，不能用扩大 RSS 上限替代 LeakSanitizer。
4. 测试自身保存所有已创建 PID；任意中间断言失败时，只对命令行仍为本轮 `xdebug-fst`
   的精确 PID 发送终止信号，避免失败测试遗留 engine 或误伤其他 session。
5. 既有 `session-uds-lifecycle` 已真实对一个 active engine 发送 `SIGKILL`，要求
   `session.doctor` 返回 `SESSION_UNHEALTHY`，随后 `session.gc` 删除 registry、generation
   artifact 和 socket；该用例继续纳入普通、ASan 和 UBSan 完整 CTest，不以新重复测试替代。
6. 验收结果：普通构建 CTest 9/9、pytest 266/266、冻结基线通过；ASan 在 leak/遇错即停
   下完整 CTest 7/7，UBSan 在遇错即停下完整 CTest 7/7；新 stability 又分别连续重复 3 次
   通过。一次 ASan 高并发 doctor 的 1 秒 ping 超时未在随后三轮重复中复现，因此没有修改
   生产 timeout，也没有通过重试逻辑、降低普通并发度或 fallback 掩盖测试。
7. 本批输入仍只有 `testdata/fixtures/waves.fst`，Wellen 直接按需读取原始 FST。session
   concurrency、process cleanup、FD/RSS 和 sanitizer 都属于运行稳定性门禁，不承担波形分析，
   不改变 `GOAL-FST-DIRECT-001`。

#### P7 第三批：73-action 十维覆盖审计基线

2026-08-10 建立机器可读的 action 交换 trace 与保守覆盖矩阵。该矩阵用于量化 TODO，不能
代替原版归一化差分或人工合同审查：

1. `XDEBUG_ACTION_COVERAGE_LOG` 只有显式设置时才启用，且必须为绝对路径。pytest runner
   逐次记录 test node、one-shot/stdio-loop transport、完整请求/响应、退出码和 timeout；默认
   关闭时不写文件、不改变请求、响应、backend 或测试顺序。pytest session 启动时拒绝相对
   路径、缺失父目录和已存在的目标文件，防止跨次运行追加旧事件伪造覆盖数量。
2. `tools/audit_action_coverage.py` 从冻结 catalog 读取严格 73 个 action，并按实际 trace 保守
   标记：正常成功、schema 非法输入、资源缺失、空结果、边界时间、多结果、limits、
   truncation、completeness、X/Z。每个标记保留 pytest node 证据；没有观察到就保持 missing，
   不从 handler 名、schema 字段存在或文档声明推断已覆盖。
3. 第一份基线由 342 项 pytest 中的 925 次 public action 交换生成：success 73/73、
   invalid_request 73/73、resource_missing 18/73、empty_result 9/73、boundary_time 22/73、
   multiple_results 35/73、limits 20/73、truncation 3/73、completeness 37/73、X/Z 7/73。
   73 个 action 均有至少一次交换和成功响应；负例中的 `clock_point_query`、`no.such.action`
   作为 unknown action 单独报告，不计入冻结 73 项。
4. `trace.x_origin` 在这份启发式矩阵中十列均有观察证据，只能说明现有 pytest 触达十类
   形状，不能声明该 action 已与原版全差分；其余 72 项更不能因某列出现勾选而跳过逐字段
   归一化比较。
5. 对 `actions`、`schema`、`session.*`、config/list 等资源无关或时间无关 action，某些维度
   是否“不适用”必须依据冻结 request/response schema 和原版真实请求结果逐项裁定并保存
   evidence；禁止审计工具自行标 N/A，也禁止为填表构造违反 schema 的无意义波形请求。
6. X/Z 分类只接受四态 literal/bit 值和明确 unknown kind；普通 `0xdead_beef` 或
   `32'hdead_beef` 不得因字符 `x` 被误报。分类器已有独立单测覆盖该边界。
7. 正式复现命令如下，trace/JSON/Markdown 均生成在 `/tmp`，不得作为仓库产物提交：

   ```bash
   cmake -E remove /tmp/xdebug-action-coverage.ndjson /tmp/xdebug-action-coverage.json /tmp/xdebug-action-coverage.md
   XDEBUG_ACTION_COVERAGE_LOG=/tmp/xdebug-action-coverage.ndjson PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 ${XFST_CONDA_ENV} -m pytest -q --xfst-bin=${REPO_ROOT}/build/xdebug-fst ${REPO_ROOT}/tests
   ${XFST_CONDA_ENV} ${REPO_ROOT}/tools/audit_action_coverage.py --repo-root ${REPO_ROOT} --trace /tmp/xdebug-action-coverage.ndjson --output-json /tmp/xdebug-action-coverage.json --output-markdown /tmp/xdebug-action-coverage.md
   ```
8. 下一步必须先按 schema/原版裁定 applicability，再从缺口最大的资源缺失、空结果和
   truncation 开始补真实请求/响应差分；只有全部适用维度都有可定位证据且归一化语义一致，
   才允许启用 `--require-complete` 作为硬门禁。

#### P7 第四批：resource_missing 全 action 适用性裁定

2026-08-10 完成 resource_missing 维度的 73 项裁定，同时修复审计器把路由失败请求中的
time/limits/XZ 误记为 action 执行覆盖的问题：

1. runtime verbose catalog 的冻结资源分组为：55 waveform、4 design、3 combined、3 session、
   2 any、6 none。对前四组共 65 项和 any 中的 `scope.roots`，逐项加载该 action 自己的
   第一份冻结合法 request example，只把 target 替换为不存在的 session；66/66 必须返回
   退出码 1、原 action 和精确 `SESSION_NOT_FOUND/session_manager`。
2. 使用 action 自身 example 保证必填 args、selector/config shape 先通过 schema，资源缺失
   不会被 `INVALID_REQUEST` 冒充。`session.open` 不经过 managed-session 路由，其既有不存在
   `.fst` 回归返回 `WAVEFORM_OPEN_FAILED`。因此 observed resource_missing 为 67/73。
3. 剩余 6 个 `requires=none` action 为 `actions`、`batch`、`expr.normalize`、`schema`、
   `session.gc`、`session.list`。冻结 request schema 禁止 `target.session_id`；只读原版与开源
   候选对附加缺失 session target 的完整响应逐项精确一致，均在资源查找前返回 schema
   `INVALID_REQUEST`。五项 `invalid_arg=target.session_id`；`expr.normalize` 的 `oneOf` 合同
   为 `invalid_arg=$`，同时在 `validation_issues` 明确指出 target/session_id。
4. `tests/coverage/action_applicability.json` 只把上述六个 action 的 resource_missing 标为
   N/A，并为每项保存原因和 `tools/check_resource_applicability.py` 证据。审计器对未知 action、
   未知维度、重复项、空原因/证据或多余字段 fail closed；observed 与 N/A 分列，不得相加
   伪造实际错误执行数。
5. 审计器现在只有在成功响应后才从请求登记 boundary_time/XZ，limits 也只在成功或真实
   truncation/limit 结果后登记。资源路由失败只记 resource_missing。对同一 992-event trace
   重算后 resource_missing 为 67 observed + 6 N/A，boundary_time 从旧误计 23 校正为 22，
   limits 保持 20。
6. 原版适用性差分与带 N/A 矩阵复现命令：

   ```bash
   ${XFST_CONDA_ENV} ${REPO_ROOT}/tools/check_resource_applicability.py --repo-root ${REPO_ROOT} --original-root ${XDEBUG_ORIGINAL_ROOT}
   ${XFST_CONDA_ENV} ${REPO_ROOT}/tools/audit_action_coverage.py --repo-root ${REPO_ROOT} --trace /tmp/xdebug-action-coverage.ndjson --applicability ${REPO_ROOT}/tests/coverage/action_applicability.json --output-json /tmp/xdebug-action-coverage.json --output-markdown /tmp/xdebug-action-coverage.md
   ```
7. 全量 pytest 345/345、CTest 9/9、冻结基线和原版差分 6/6 通过。所有 managed session
   测试仍只引用原始 `.fst`；本批未修改生产实现、Wellen、Verilator、ABI 或数据路径。
8. 下一优先级为 empty_result 9/73 与 truncation 3/73；仍须先裁定每个 action 的适用性，
   不得把 resource_missing 的 73 项完成外推到其他维度。

#### P7 第五批：empty_result 扩面与 truncation 分类校正

2026-08-10 完成两批真实空结果扩面，并先修正截断审计定义：

1. 第一批新增 `apb.config.list`、`event.config.list`、`list.show`、`list.validate`、
   `waveform.cursor.list` 和 `session.list` 六项成功空集合。session registry 使用隔离 HOME，
   避免共享状态或测试顺序制造零会话假阳性。
2. 第二批新增 `apb.query`、`apb.transfer_window`、`axi.query`、`event.find` 四项合法零匹配。
   地址过滤、有效时间窗和合法表达式都真正执行，返回 `ok=true`、零 cardinality 与空集合；
   不允许用资源错误、schema 错误或缺失数据字段代替。
3. `has_truncation` 现在识别冻结合同的 `response_truncated=true`、非空
   `truncation_scopes` 和显式 limit 终止，移除“任意非空 limitations 即截断”的宽松规则；
   `design_unavailable` limitation 是固定反例。
4. 353 项 pytest 的全新 trace 含 1019 次 public exchange，最终 observed 为：success 73、
   invalid_request 73、resource_missing 67、empty_result 19、boundary_time 22、
   multiple_results 35、limits 20、truncation 16、completeness 37、X/Z 7；resource_missing
   另有已证明的 6 N/A。
5. 普通 CTest 9/9、pytest 353/353、冻结基线和审计单测通过。trace 与矩阵只在 `/tmp`，
   没有回灌 action 或成为 FST 索引/离线数据库。
6. empty_result 19/73、truncation 16/73 都不是完成率；剩余项必须逐一给出真实运行证据，
   或由冻结 schema、语义不可达性和原版响应差分共同证明 N/A。仅看 response schema 中存在
   array、count、limit 或 truncation 字段不足以标 N/A/适用。
7. 本批未修改生产 action、Wellen、Verilator、ABI、backend 或 transport。运行时事实仍为
   `原始 .fst → Wellen 按需读取 → action`，DesignDB 静态事实边界不变。

#### P7 第六批：主结果空语义与八项协议扫描扩面

2026-08-10 继续校正 empty_result 定义并增加八项真实协议空结果：

1. `total_count=0` 与 `found/diff_found=false` 是冻结合同的明确空结果；
   `total_count>0,returned_count=0` 的 count-only 响应只证明存在多结果，不能算空。
2. 主结果集合扩展到 outliers、change_points、changed_signals、evidence、matches、payloads、
   rows、chains、hops、preview 等；空 issues、recommended_actions、constraints 属于辅助诊断、
   建议或 schema 元数据，明确不计为空查询结果。
3. `axi.transaction.cursor` 走到末尾返回 `found=false`；`axi.latency_outlier` 使用合法 1us
   阈值返回零 outlier；`axi.outstanding_timeline` 与 `axi.request_response_pair` 在首个事务前
   合法时间窗返回空主集合。
4. `stream.query/export` 在首个 transfer 前返回空 rows/preview；`signal.sampled_pulse.inspect`
   与 `protocol.handshake.inspect` 在首个采样边沿前返回零 sample/零 finding。sampled-pulse
   初始 payload 变化会产生真实 finding，因此空门禁使用其默认 unsampled-pulse 语义，未删除
   或忽略真实 finding。
5. 最新完整 trace 含 1046 次已识别 public exchange：success 73、invalid_request 73、
   resource_missing 67 observed+6 N/A、empty_result 33、boundary_time 24、multiple_results 42、
   limits 20、truncation 16、completeness 37、X/Z 7。普通 CTest 9/9 通过。
6. empty_result 剩余 40 项尚未裁定；必须继续区分真实适用、语义不可达和 N/A，不能用辅助
   数组、错误响应或字段存在性填满矩阵。
7. 本批未修改生产实现、Wellen、Verilator、ABI、backend 或 transport；测试事实路径仍为
   `原始 .fst → Wellen 按需读取 → action`。

#### P7 第七批：empty_result 七十三项全量裁定

2026-08-10 完成 empty_result 维度全部 73 项裁定：

1. 新增 catalog 合法空过滤、APB/AXI 零匹配 statistics、AXI 零行显式 export、event 零事件
   export、signal.changes 零 transition summary；observed 由 34 提高到 41。
2. 新增 waveform-only session 请求 design roots 的零 root 与不完整分析、primary input 无
   active/static driver、内部 output 无 static load，observed 最终为 45。
3. 第一类 18 N/A 必须同时满足：冻结文件哈希有效，全部冻结原版成功 schema 均没有主结果
   integer/array/found 表达。检查器要求集合精确相等，schema 演进时 fail closed。
4. 第二类 10 N/A 的成功 schema 虽含主结果字段，但公共请求要求非空输入或既有 target，且冻结
   原版成功 example 锁定对应的非空输出映射：batch、list.load、nwave.rc.generate、
   session.close/kill、signal.resolve、signal.xz_verify、trace.active_driver_chain、value.at、
   verify.conditions。
5. 最新完整 trace 含 1073 次已识别 public exchange：empty_result=45 observed+28 N/A，
   未裁定为 0；success/invalid 73/73、resource_missing=67 observed+6 N/A、boundary_time 24、
   multiple_results 44、limits 20、truncation 16、completeness 37、X/Z 7。
6. 全量 pytest、CTest 9/9、冻结基线、resource applicability 原版差分 6/6 与 empty applicability
   检查器通过。失败 trace 不复用，所有审计文件只在 `/tmp`。
7. AXI/event/list/stream 等显式 export 产物仅验证请求输出合同，不作为 action 输入、不回灌分析；
   本批未修改生产实现、Wellen、Verilator、ABI、backend 或 transport。
8. 下一步进入 truncation 维度；不得把 empty_result 的 N/A 集合直接复制到 truncation。

提交：

- `测试：建立七十三项 action 全量差分门禁`
- `测试：增加稳定性并发与资源泄漏门禁`
- `文档：更新完全兼容后的构建测试与使用说明`
- `发布：完成 xdebug v1 全能力兼容验收`

完成后创建 `parity-p7` tag。

#### P7 第八批：truncation 七十三项全量裁定

2026-08-10 完成 truncation 维度全部 73 项裁定，并以失败证据修复三处真实缺口：

1. 冻结成功 Schema 的穷举检查先得到 37 项可表达 canonical truncation、36 项不可表达；
   后续没有把“Schema 含通用完整性字段”直接当作运行适用性结论。
2. 35 项 action 已由真实运行观察到截断或非空 `truncation_scopes`。新增门禁覆盖
   `axi.query`、`axi.channel_stall`、`axi.analysis`、`axi.statistics`、
   `axi.transaction.cursor`、`axi.export` 和 `stream.validate`；测试输入均为 Wellen 仓库中的
   原始 `.fst`，由 Wellen 在当前 session 内按需读取。
3. `scope.list` 原先忽略 `limits.max_rows`，`trace.driver/trace.load` 原先忽略
   `limits.max_results`；均在 xdebug-fst 内执行完整扫描后只裁剪响应，保留 total/returned
   与对应 response scope，不修改 DesignDB 或 Verilator。
4. AXI 扫描器原先把采样点未知 reset/valid/ready 静默当作无握手并虚报完整。修改前真实
   原始 FST 回归失败；修复后未知控制边沿被跳过且 `analysis_transactions` 传播到所有依赖
   同一扫描结果的 AXI action。独立 channel-stall 扫描也记录未知 valid/ready。
5. `stream.validate` 冻结请求明确声明 `line_limit` 限制 evidence rows，因此不能标为 N/A。
   修复后 stream 动态扫描统计 control/data X/Z 与 ready/bp 冲突，转换为冻结 issue 结构，
   并用 `analysis_samples` 与 `response_issues` 区分分析不完整和响应截断。
6. `signal.resolve` 与 `signal.xz_verify` 是第二类 N/A：前者只接受一个 final leaf、禁止
   aggregate 自动展开且没有结果上限；后者只返回 initial value 和一个 nullable first
   mismatch，以 window end 或 first mismatch 终止，也没有 row/result limit。该结论由
   `tools/check_truncation_applicability.py` 对冻结请求和成功 Schema fail-closed 检查；合同变化
   会直接使门禁失败。
7. 最新 1113-event trace 为 truncation 35 observed + 38 N/A，未裁定为 0；其中 N/A 包含
   36 项 Schema 不可表达和 2 项标量生命周期。全量 pytest、CTest 9/9 与冻结适用性检查通过。
8. 本批没有修改 Wellen、Verilator、XDD ABI、backend 或 transport。没有 FST→VCD/JSON
   转换、预扫持久化、私有索引、离线数据库、全量内存快照、export 回灌、TCP/fileport 或
   fallback。FST 仍只是必须适配的波形输入，不是分析引擎。
9. truncation 维度完成不代表 Goal 完成；boundary_time、multiple_results、limits、
   completeness、X/Z 和最终原版归一化差分仍须继续逐项闭环。

#### P7 第九批：limits 七十三项全量裁定

2026-08-10 完成 limits 请求适用性 73 项裁定：

1. 穷举冻结请求 Schema，递归识别 args 内的 `line_limit`、`top_n` 等结果规模字段，并识别
   `limits.max_rows`、`max_results`、`max_nodes`、`max_depth`、`max_chains`、`max_events`、
   `max_time_steps` 和 `max_trace_signals`，得到 29 项适用、44 项 timeout-only 或无结果上限。
2. 修正覆盖审计器：`limits.timeout_ms` 只是 frontend watchdog，不再冒充 action 结果上限；
   新增成功反例锁定该边界。
3. 44 项逐项登记 N/A；29 项均有冻结合法的带结果上限成功请求。唯一缺口
   `expr.normalize` signal 分支增加 `line_limit=1` 请求门禁；可产生真实响应裁剪的 action
   仍由上一批 truncation 证据独立证明，不能把“请求接受 limit”等同于“发生截断”。
4. 最新 1113-event trace 为 limits 29 observed + 44 N/A，未裁定为 0；全量 pytest、
   CTest 9/9 与结果上限适用性检查通过。
5. 本批仅修改审计、测试和证据清单，没有修改 Wellen、Verilator、生产波形路径、ABI、
   backend 或 transport；没有转换、索引、离线数据库、TCP/fileport 或 fallback。

#### P7 第十批：boundary_time 七十三项全量裁定

2026-08-10 完成 boundary_time 维度 73 项裁定：

1. 冻结请求 Schema 穷举只把 `time`、`times` 和 `time_range` 视为显式时间选择，得到
   30 项适用、43 项非时间 action。
2. 审计器不再把任意 `begin/end=0` 当时间边界；地址、ID 和数值 range 的零起点是固定反例，
   只有 `time_range.begin/end` 或显式 `time/times` 才能计入。
3. 为 `axi.query`、`axi.channel_stall`、`axi.latency_outlier`、`signal.stability`、
   `verify.conditions`、`waveform.cursor.set` 补齐成功 `0ps`/零长度闭区间门禁；没有使用非法
   时间范围或错误响应冒充边界执行。
4. 最新 1116-event trace 为 boundary_time 30 observed + 43 N/A，未裁定为 0；全量 pytest、
   CTest 9/9 和冻结适用性检查通过。
5. 本批只修改测试、审计和证据清单，没有修改 Wellen、Verilator、ABI、backend、transport
   或生产 FST 路径；不存在转换、索引、离线数据库、TCP/fileport 或 fallback。

## 六、最终完成门禁

只有同时满足以下条件才允许完成 Goal：

- `GOAL-FST-DIRECT-001` 全程满足：唯一波形事实路径为“当前 session 原始 `.fst` → Wellen 按需访问 → action 查询/推理”
- 生产、单元、集成、差分和最终验收实际打开的波形输入全部为 `.fst`
- 不存在 FST→VCD/JSON/私有索引/离线数据库/全量内存快照分析路径，不存在 export 回灌或 backend/fixture fallback
- action catalog 严格为 73 个
- 不存在额外公开 action
- 全部冻结 schema 可加载
- 所有测试请求和响应通过 schema
- 原版和 xdebug-fst 的 73 action 归一化差分全部通过
- one-shot、JSON、XOUT、stdio-loop 全部通过
- UDS transport 全部通过；TCP/file 明确为用户裁剪项，不存在隐式 fallback
- MCP direct 和 fake-LSF 全部通过
- Wellen、Wellen C API 和 wellenx 有真实测试执行数量
- Verilator simple/full/UART/metadata 独立通过
- 普通 Verilator 行为未被 DesignDB 修改影响
- active-driver 和 X-origin 在复杂场景与原版一致
- ASan/UBSan 无错误
- 并发、崩溃、清理和资源泄漏测试通过
- 三个仓库工作树干净
- 所有阶段 commit 和 tag 齐全
- 依赖 SHA、测试证据、任务书、进度文件和最终报告齐全
- Git 中不存在 proprietary 内容或禁止提交的生成物
- 所有沙箱外测试均有对应的沙箱内异常证据和明确根因
- 不存在通过切换工具、backend、transport、fixture 或数据源获得的伪通过

## 七、默认约束

- 不创建顶层 Git 仓库
- 不使用 submodule
- 不创建 PR
- 不推送远端
- 不修改原版 xdebug
- 不 reset 或覆盖用户已有修改
- 测试默认在沙箱内运行
- 环境异常时允许用相同命令沙箱外重试
- 沙箱外重试不得改变测试层级、工具、后端、数据源或期望
- 不因测试失败自动 fallback 到其他实现路径
- 不把 smoke test、0 tests 或 schema stub 视为完成
- Verilator 修改必须有必要性证据并保持最小
- 原版基线变化必须显式更新 baseline 并单独提交
- Goal 不设置 token budget
- Goal 只在全部最终门禁满足后标记 complete

## 八、P7 第十一批 multiple_results 全量裁定任务记录

本批已完成 73 个公开 action 的多结果维度全量裁定。审计器只允许 `summary` 或 `data`
直接子字段中的冻结主结果计数/集合提供信用，禁止把 `data.validation.signals`、diagnostics、
recommendations 等嵌套辅助数组误当成 action 主结果。冻结成功响应 Schema 的逐项检查得到
54 项可表达大于一；其中 `signal.resolve` 的公开请求只接受一个最终叶节点精确路径，并明确
不展开 aggregate、array、struct 或 pattern，因此为语义 N/A。最终分区为 53 项运行时适用、
19 项 Schema 不可表达、1 项语义单值，共 73 项。

运行时补证据覆盖 APB/AXI/Stream 双配置列表、双信号 list.validate、同一首变时刻的两个
alias、wave/design 两个不同根、连续两个 X/Z 检查值，以及两个真实 UDS session 的 list、gc、
close-all、kill-all。`session_id="all"` 是冻结公开生命周期语义，故 session.close/kill 必须
适用，不能因请求字段是字符串而错误标 N/A。新增证据首次暴露 `stream.config.list` 返回
`packet="disabled"`，而冻结 Schema 只接受 `none|sop/eop`；生产映射已最小修复为 `none`。

最终全新 trace 记录 1137 次已知公开交换：multiple_results 为 53 observed + 20 N/A，
empty_result 同时保持无缺项；pytest 全量通过，生产修复后的 CTest 9/9 通过。trace 与报告只
位于 `/tmp`，不得提交、回灌或作为波形索引。该维度关闭不代表完全一致；completeness、X/Z、
P6 剩余复杂语义和最终 73-action 原版归一化差分仍必须继续完成。

唯一波形事实路径保持“当前 session 原始 `.fst` → Wellen 直接按需读取 → xdebug action
语义”；DesignDB 只提供静态事实。FST 不是分析引擎。本批未修改 Wellen、Verilator 或 XDD
ABI，不存在 VCD/JSON 转换、预扫持久化、私有索引、离线数据库、全量内存快照、export
回灌、TCP/fileport 或 fallback。

## 九、P7 第十二批 completeness 全量裁定任务记录

完整性审计现在只接受 `summary` 或 `data` 的直接布尔字段：`scan_complete`、
`analysis_complete`、`cleanup_complete`、`data_complete`、`complete` 和
`value_width_complete`。禁止将嵌套 validation/diagnostic 对象中的同名字段计为 action
完整性。冻结 73 项成功响应 Schema 穷举得到 41 项可表达、32 项不可表达，后者全部登记
严格 N/A。

旧审计遗漏 `value_width_complete`，且候选的 `expr.eval_at`、`list.first_change`、
`verify.conditions` 未执行原版统一 LogicValue 后处理的宽度摘要语义。三项均已在 xdebug-fst
响应层补 `value_width_complete=true` 与空 `width_diagnostics`：所有值来自 Wellen 已报告的
确定宽度，没有伪造未知宽度，也没有改变表达式、采样、首变或条件判定算法。

最终全新 1137-event trace 为 completeness 41 observed + 32 N/A，73 项无缺口；全量 pytest
和 CTest 9/9 通过。该维度只证明公开完整性字段覆盖，不能替代 X/Z 全量裁定、P6 剩余复杂
语义或最终原版归一化差分，Goal 必须保持 active。

唯一数据流继续是原始 `.fst` 由 Wellen 直接按需读取、xdebug action 执行语义、DesignDB
提供静态事实。FST 不是分析引擎。本批未修改 Wellen、Verilator、XDD ABI 或 transport，
不存在转换、预扫、索引、离线数据库、全量快照、export 回灌、TCP/fileport 或 fallback。

## 十、P7 第十三批 X/Z 全量裁定任务记录

本批完成 73 个公开 action 的 X/Z 维度全量裁定。适用性检查器穷举冻结成功响应 Schema，
只把可达 `LogicValue`、显式 X/Z 状态枚举、非零 `control_xz_count`、`data_xz_count`、
`unknown_count`、`unresolved_filter_count` 或 `unresolved_transaction_count` 视为公开四态
能力；`trace.active_driver_chain` 的递归 JSON hop/frontier/ambiguity 由冻结基本响应单独锁定。
普通 `unknown` 枚举、`analysis_complete=false`、AXI `phase_order=unknown` 和表达式置信度
`unknown` 均不得冒充 X/Z 证据。最终冻结 23 项运行时适用、50 项公开不可观察，共 73 项。

新增运行时证据全部直接打开 Wellen 仓库原始 `.fst`。APB 波形覆盖 batch 嵌套值、counter、
event、list、signal、sampled pulse 与 stream 的 X 字面量和四态计数；VCS processor 波形复现
stalled valid 从已知值变成 Z，验证 handshake 的 `observed_valid`；宽四态波形用真实完成的
AXI 事务和已知地址过滤器得到非零 `unresolved_transaction_count`。请求中的非法 X 地址
过滤字面量不会被采用，所有信用来自成功响应中的实际四态事实。

最终全新 trace 位于 `/tmp/xdebug-action-coverage-20260810-p7-xz-final.ndjson`，包含 1169 个
有效公开交换；审计结果为 X/Z 23 observed + 50 N/A、缺口 0，全部十个维度的 73 action
均无缺项。393 项 pytest、CTest 9/9、全部适用性检查以及 6 项原版资源差分通过。trace 和
JSON 报告只作为临时验收证据，不提交、不回灌、不参与 runtime。

本批未修改 Wellen、Verilator、XDD ABI、backend 或 transport。Wellen 对 X/Z 的要求仍是
保真、按需地从当前 session 原始 FST 返回四态位串、宽度、时间和采样点；它不执行协议、
表达式、driver 或事务分析。分析与公开合同由 xdebug action 负责，Verilator DesignDB 只
提供必要静态事实。禁止转换、预扫持久化、私有索引、离线数据库、全量快照、export 回灌、
TCP/fileport 和 fallback。该维度关闭不代表 Goal 完成；P6 剩余复杂语义和最终原版归一化
差分仍必须完成。

## 十一、P6 Phase 5 循环选择闭环后的强制任务锁（2026-08-11）

本节登记当前已由原版只读重放证明、并由红测到实现闭环的循环选择语义。它是后续 P6/P7
工作和 Goal 完成验收的强制回归项，不得在重构、依赖升级、上下文压缩或测试提速时删除、
放宽或改写为近似语义。

1. `dout` 物化 unpacked 元素必须复现原版展开后循环体的最终 selector 语义；冻结查询返回
   普通分支单 statement、六个精确 RHS 和 `multiple_rhs_sources`。不得擅自改成按请求元素
   下标绑定循环变量，除非先获得新的原版版本差分证据并显式更新冻结基线。
2. `flag[2]` packed 选择视图必须对完整向量 selector 域做存在性谓词求值，保留 special 和
   normal 两个活动候选；冻结结果为 `multiple_active_candidates`、两条 statement、九个 RHS。
3. 动态 RHS 必须保留 `mask_a[lane]`、`mask_b[lane]` 的结构证据名。它们在原始 FST 中不存在
   时必须报告 `signal_not_found` 与 `changed=null`，不得用某个具体 bit、base 向量值或静态
   猜测代替。
4. `multiple_active_candidates` 必须在当前 hop 入链前终止：根查询 `hops=[]`、两个 count
   均为 0，同时 evidence 保留根 signal 和 `hop_index=0`。`multiple_rhs_sources` 仍在当前
   statement node 后终止并保留 hop；两种歧义不得合并处理。
5. Verilator 允许保留的新增事实仅为既有 driver 通道中的 `target_loop_index`、
   `rhs_loop_selected` 和 `rhs_loop_index`。当前 revision 固定为
   `9c8ae78cba35ab152e13644a50d6fc0c882955d4`；XDD header/ABI 不变。任何进一步 Verilator
   修改仍须先有独立红测证明 xdebug-fst 与现有静态事实无法解决，并保持附加、局部和克制。
6. Wellen 继续只直接、按需读取当前 session 原始 `.fst` 的值、时间和采样事实；循环、driver、
   predicate、RHS 和歧义分析全部属于 xdebug action 与 DesignDB 组合。不得转换、预扫、建立
   私有索引/离线库/全量快照，不得回灌 export，不得增加 TCP/fileport 或 fallback。
7. 所有后续 C/C++ 构建和测试使用 `XDEBUG_GCC_TOOLCHAIN` 指向的
   `${REPO_ROOT}/../.toolchains/gcc-13`（GCC/G++ 13.3.1）；仓库路径只通过
   `XDEBUG_VERILATOR_REPO` 与 `XDEBUG_WELLEN_REPO` 索引。缺失依赖安装到对应仓库或
   `/workspace/work/xdebug_oc` 私有目录，不污染系统环境。
8. 当前闭环门禁为 Verilator XDD 13/13、xdebug combined 75/75、pytest 397/397、CTest 9/9
   和依赖基线检查。该批次完成不等于 P6 或 Goal 完成；tagged/binding matches、更多
   NBA/常量、复杂端口/接口/ref、多驱动调度边界与最终 73-action 原版归一化差分仍需继续。
9. GCC 13 sanitizer devel/runtime 必须保存在
   `${REPO_ROOT}/../.toolchains/gcc-13`。pytest、CTest、session engine 与其他
   子进程不得覆盖父进程私有 runtime 路径；只能在其前方追加 Wellen/wellenx 路径并去重。
   禁止因为动态库缺失切换回系统 GCC 8 或降低 sanitizer 测试层级。
10. GCC 13 ASan 使用 `detect_leaks=1:abort_on_error=1:halt_on_error=1`，UBSan 使用
    `halt_on_error=1:print_stacktrace=1`。当前两套独立 CTest 均为 9/9，普通/ASan/UBSan
    pytest 均为 396/396；以后每个改变 C/C++ 运行时语义的高风险批次必须保持该门禁。sanitizer
    只检查实现，不得成为新的波形访问、转换、索引或分析层。

## 十二、P6 第五十九批 matches 嵌套通配闭环后的强制任务锁（2026-08-11）

本节把 packed assignment pattern 内部 `.*` 的三层闭环登记为后续 P6/P7 与最终验收的强制
回归项。它只关闭无 binding 的 packed struct 成员通配，不得扩大解释为 tagged union、tagged
expression/pattern 或 PatternVar binding 已完成。

1. Verilator 前端必须把普通 pattern 成员构造成全一 mask，把 `.*` 对应成员构造成全零 mask，
   并以 `(selector & mask) === (value & mask)` 执行四态精确匹配。禁止把整个 pattern 降级为
   `casez/casex`，因为那会错误地把普通成员中的 X/Z 也当成通配位。
2. `case matches` 与独立 `matches` 必须共享同一 value/mask 语义；item/default 必须互补，
   source item 顺序保持不变。直接顶层 `.*` 的既有恒真语义继续保留。
3. DesignDB 必须发布可由冻结 xdebug expression evaluator 执行的同一掩码 predicate，不能输出
   `???? // PATTERNTEST`、空 predicate 或近似运算。XDD header、ABI 与 capability 保持不变。
4. tagged union、TaggedExpr、TaggedPattern 和 PatternVar binding 仍必须在前端或静态事实边界
   失败关闭；不得因为无绑定嵌套 wildcard 已通过而抑制这些诊断。
5. xdebug-fst 动态门禁使用 `matches_top.sv` 同一源码同步生成的 raw `waves.fst` 和 DesignDB。
   45ps 的 `sel=1` 必须唯一选择 data 分支；65ps 的 `sel=2` 必须唯一选择 case default 与
   standalone else 常量分支。源路径和行号必须来自 DesignDB，不得由 FST 猜测。
6. Wellen 只直接、按需读取当前 session 原始 `.fst` 中的 `nested_match_packet` 和数据值；pattern
   展开、mask、predicate、driver 选择与合同投影分别属于 Verilator 静态事实和 xdebug action。
   禁止转换、预扫、私有索引、离线库、全量快照、export 回灌、TCP/fileport 或 fallback。
7. 修改链固定为 Verilator 红测试 `b5d526c72`、精确前端实现 `f59f6e4c8`、XDD 红测试
   `3212f4580/2eb7713b6` 与最小 emitter 实现 `9c8ae78cb`；xdebug 动态红证据为 `c4cc865`。
8. 当前回归为 matches/tagged 聚焦 10/10、Verilator XDD 13/13、distribution 与 Python lint
   全绿、xdebug combined 75/75、pytest 397/397、CTest 9/9 和冻结依赖基线通过。该批闭环
   不等于 P6 或 Goal 完成，PatternVar/tagged、复杂端口与联合限制、最终 73-action 归一化差分
   仍必须继续。

## 十三、P6 能力与信息语义适用性收口（2026-08-13）

用户最新要求是不追求实现、JSON、措辞和排列“完全一致”，只要求能力一致、关键信息语义
一致。本节是对本文历史 P6 TODO 的权威范围修正；与较早的“tagged 等全部语法形态必须完成”
冲突时，以本节为准。

1. 最终门禁仍严格覆盖冻结 73 个公开 action，不删 action，不以 stub、近似返回或测试缺席
   冒充完成。目标、时间、值、driver/source、关系、计数、范围、完整性、截断、未知状态和
   错误原因必须保持等价语义。
2. P6 分为七个用户可观察分析语义族：活动 predicate/pattern，时序与 driver 优先级，模块/
   interface/ref/alias，多 driver 歧义，X-origin 分支/环/来源，时间及各类预算，typed/delta
   波形事实。每个能力族必须有至少两项真实仓库回归，证据由
   `tests/coverage/p6_capability_applicability.json` 和对应 pytest 自动校验。
3. PatternVar 已完成动态闭环，不再列为剩余项。tagged union/expression/pattern 的额外前端
   语法、nested/arrayed interface 的更多语法形状、primitive strength/tristate 的语法组合，
   若只产生已有 DesignDB/FST 事实，属于 Verilator producer 覆盖，不是新的 xdebug action
   能力；不得为追求语法穷举扩大 Verilator 修改。
4. max_time_steps/max_nodes/max_depth/max_chains 的单项行为和代表性联合限制必须正确；已有
   能力的全部参数笛卡尔积不是独立能力。若以后红测证明计数、frontier、pending、续跑建议或
   最终结论出现新的用户可观察错误，该场景立即重新成为必修缺口。
5. 上述裁定不声称 Verilator 支持所有列出的 SystemVerilog 语法，也不得用于跳过新的调度
   优先级、关系类型、未知状态、完整性或 action 能力。详细理由见
   `doc/XDEBUG_P6_CAPABILITY_APPLICABILITY.md`。
6. FST-only 架构不变：原始 `.fst` 是唯一波形输入，Wellen 直接按需提供值、时间、类型和
   delta 事实，xdebug action 结合 DesignDB 执行分析。禁止转换、预扫、私有索引、离线库、
   全量快照、export 回灌、TCP/fileport 和 fallback。

## 十四、P7 最终验收关闭记录（2026-08-13）

最终验收报告写入 `doc/XDEBUG_FINAL_ACCEPTANCE_REPORT.md`。当前 405 项 pytest、普通 CTest
9/9、ASan CTest 9/9 + pytest 405/405、UBSan CTest 9/9 + pytest 405/405 均通过。新鲜
1212-event trace 覆盖 73/73 action，十个维度全部为真实观察或冻结适用性裁定，无 missing，
且 observed 与 N/A 零重叠。

Wellen workspace 207 项运行通过、8 ignored、0 失败；C ABI 端到端与 wellenx 2/2 通过。
Verilator 当前 14 个 `t_xdd_*`、16 个普通行为代表回归、GCC 13 源码构建及两个 distribution
检查通过。三个仓库 revision、工具链、哈希、命令和架构边界均在最终报告登记。

本节按用户最新“能力一致、信息语义一致”口径关闭 P7，不主张实现或响应文本完全一致。FST
仍是唯一波形输入，Wellen 直接按需读取，分析仍由 xdebug action 与 DesignDB 完成；TCP/
fileport 保持裁剪，不存在转换、离线分析或 fallback。
