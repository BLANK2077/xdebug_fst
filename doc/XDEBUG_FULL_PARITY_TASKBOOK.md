# xdebug_oc 全能力兼容修复、Goal 执行与分批提交计划

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
使用 Wellen 从当前原始 FST 直接读取的 selector 在 active time 判定。tagged union、tagged
pattern、pattern variable/star、default-only 与独立 `matches` 运算符仍明确不支持，不得
冒充通用 matches 完成。第十八批又以等价 NBA 时序固件证明“最近赋值事件”不能退化为
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
边界，复杂 output 表达式、多输入 RHS、多个 output port 与多驱动组合仍须独立差分。

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

提交：

- `测试：建立七十三项 action 全量差分门禁`
- `测试：增加稳定性并发与资源泄漏门禁`
- `文档：更新完全兼容后的构建测试与使用说明`
- `发布：完成 xdebug v1 全能力兼容验收`

完成后创建 `parity-p7` tag。

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
