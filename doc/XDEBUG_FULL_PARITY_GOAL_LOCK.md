# xdebug-fst 完整兼容 Goal 不可漂移约束

## 一、与当前 active Goal 的关系

本文是当前 active Goal `019fe602-0198-7f23-a9a1-bb3c6a539dec` 的权威执行附件，约束编号为 **`GOAL-FST-DIRECT-001`**。Goal objective 的第一句要求实现 [`XDEBUG_FULL_PARITY_TASKBOOK.md`](XDEBUG_FULL_PARITY_TASKBOOK.md) 中定义的全部计划；任务书又明确引用本文，因此本文中的要求属于当前 Goal 的组成部分，而不是 Goal 之外的建议。

Goal 系统不允许在 active 状态原地改写 objective。旧 objective 中仍可见的 TCP/file transport 表述已被用户后续指令废止，不得据此恢复 TCP/file；不得为了改写文字而把尚未完成的 Goal 错误标记为 `complete` 或 `blocked`，也不得重建 Goal 以丢失既有执行状态。

### 2026-08-10 用户再次确认的解释锁

用户再次明确：**本项目只需要并且也必须适配 FST 波形，不得退化为“用 FST 做分析”。** 这句话在当前 Goal 中按以下唯一含义执行：

1. “适配 FST”是波形输入适配要求，FST 只提供运行时波形事实；
2. “分析”仍是冻结的 xdebug action 合同、算法和错误语义的职责，必要时结合 Verilator DesignDB 静态事实；
3. Wellen 只直接、按需、保真读取当前 session 的原始 `.fst`，不实现或替代 xdebug 调试语义；
4. 不得把 xdebug-fst 描述、设计或验收成“FST 分析引擎”，不得从“FST-only”推导出简化 action、只看信号和值变化、预扫全文件或建立中间分析库；
5. 后续交接摘要、上下文压缩、阶段切换、测试补洞和 Verilator 修改都必须显式继承 `GOAL-FST-DIRECT-001`，不得因旧 Goal objective、旧术语或历史实现重新解释本边界。

若任何实现方向同时满足不了“FST 是唯一且必须支持的波形输入”和“FST 不是分析引擎”这两项要求，该方向即违反当前 Goal，必须停止，不能以测试通过为由接受。

为防止后续上下文压缩只保留其中半句，`GOAL-FST-DIRECT-001` 必须机械地按一组不可拆分的双断言检查：

- **输入断言**：唯一且必须完整适配的运行时波形输入是当前 session 的原始 `.fst`；
- **职责断言**：FST 及 Wellen 只提供运行时波形事实，调试分析只能由冻结的 xdebug action 语义结合必要的 Verilator DesignDB 静态事实完成。

任何计划、代码、测试、提交说明、交接或验收只满足输入断言而没有满足职责断言，或只满足职责断言却引入非 FST 波形输入，均视为同等严重的 Goal 漂移。不得用“FST-only”“FST backend”“FST 分析”等缩写替代这组双断言。

## 二、唯一允许的波形数据流

当前 Goal 只需要并且也必须完整适配 **FST 波形**。唯一允许的数据流是：

```text
当前 session 的原始 .fst
          │
          ▼
Wellen 直接、按需读取层级/时间/delta/类型/值变化
          │
          ▼
xdebug-fst action 在请求期间查询、采样和推理
          │
          ├── 可选结合 Verilator DesignDB 的静态 HDL 事实
          └── 可选写出用户显式请求的最终 export 产物
```

这里的“适配 FST”不是另建一套离线“FST 分析系统”，也不是把 FST 转换后分析。以下路径全部禁止：

- FST → VCD → action；
- FST → JSON 波形或全量内存波形快照 → action；
- FST → 私有索引或离线数据库 → action；
- FST → export 文件 → 重新加载 → action；
- Wellen/FST 失败 → 其他波形格式、backend、fixture 或 transport fallback。

本 Goal 对职责边界作如下不可漂移定义：**FST 是唯一波形输入容器，不是分析引擎；Wellen 是 FST 的保真按需访问层，不是 xdebug 语义的替代实现；xdebug-fst action 与必要的 Verilator DesignDB 静态事实组合才承担调试分析。** 因此适配工作只把原版 action 所需的运行时波形事实接到 FST，不得把 driver/load、active-driver、chain、X-origin、协议、表达式、窗口、统计、完整性或错误语义降级成仅凭 FST 信号和值变化的简化分析。文档或代码中出现“FST 分析”时，只能指 action 使用原始 FST 波形事实，绝不表示由 FST 格式本身承担分析。

VCD 只允许作为可读的 fixture 源描述来生成 FST；测试、差分和最终验收实际打开的波形必须是 `.fst`。如果 FST 缺失必要事实，必须修复 FST 生成链或 Wellen 的 FST 读取能力，并先保存失败证据，不得绕过。

Verilator DesignDB 只提供 FST 不包含的源位置、driver/load、端口连接、控制依赖等静态 HDL 事实。它不得读取、保存、重建或替代运行时波形。对 Verilator 的任何修改仍须先有失败差分和现有 XDD ABI 不足的证据，并保持最小、附加、局部、向后兼容。

active-driver 的 activation predicate 也属于上述静态 HDL 事实，而不是波形分析结果。运行时分支是否成立必须由 xdebug-fst 在目标 active time 通过 Wellen 直接按需读取当前原始 `.fst` 的控制叶子并做四态求值；predicate 缺失、不可解析、引用缺失或结果为 X/Z 时必须 unresolved/fail closed。禁止把 predicate 预先扫成离线索引，禁止缓存整份 FST，禁止读取导出文件，也禁止退回“第一条静态 driver”。

同值 NBA 的赋值事件时间同样受 `GOAL-FST-DIRECT-001` 约束：FST 只记录值变化，不能也不得被伪装成赋值事件数据库。Verilator DesignDB 只发布赋值语句所在敏感列表的直接信号与边沿这一静态事实；xdebug action 再要求 Wellen 对当前原始 `.fst` 中这些已确定的时钟/复位信号按需查找真实边沿。不得假定固定周期、任选全局时钟、扫描等值信号、让测试数据每拍变化，或生成事件索引来补洞。这个组合仍是“DesignDB 静态语义 + 原始 FST 运行时事实 → xdebug action 分析”，不是“用 FST 做分析”。

## 三、范围锁定

- 波形输入：只支持 FST，并且必须把冻结的 xdebug action 能力完整适配到 FST。
- transport：保留 UDS、stdio、MCP direct 与 fake-LSF 计划；TCP/file 不实现、不验收，显式选择时 fail closed，绝不 fallback。
- export：只允许用户显式请求的最终产物；不得作为 transport、中间数据库或后续分析输入。
- Wellen：直接打开当前 session 的原始 `.fst`，按请求所需信号和时间范围访问；不得建立可跨请求重载的波形副本。
- Verilator：只补充静态设计事实，修改尽可能克制；没有修改前失败证据就不得扩展。

## 四、每批次强制门禁

每个功能批次开始、提交和验收时必须逐项确认：

1. 实际数据流仍是“原始 `.fst` → Wellen 按需读取 → action”。
2. 测试日志列出的所有实际波形输入均以 `.fst` 结尾。
3. 没有新增 FST 转换、离线数据库、私有索引或全量波形快照。
4. 没有重新加载显式 export 产物。
5. 没有 backend、fixture、格式或 transport fallback。
6. Verilator 修改若存在，具备修改前失败证据、必要性证明和独立回归。
7. TCP/file 仍为明确拒绝的裁剪项。
8. FST 仍只承担输入格式职责；没有 action 将原版调试语义降级或偷换为“FST 自身分析”。
9. 本批次的计划、提交说明和交接材料均继续引用 `GOAL-FST-DIRECT-001`，没有把“FST-only”缩写成会引起“FST 自身分析”误解的架构描述。

任一项不满足时，该批次不得提交，最终 Goal 也不得标记为 `complete`；即使 73 个 action 或现有测试已经通过也不例外。

## 五、Goal 完成判定

`GOAL-FST-DIRECT-001` 与任务书 P0–P7 的其余条件必须同时满足。只有全部冻结 action、schema、错误合同、差分、FST-only 门禁、Wellen/Verilator 回归和仓库清洁性均有真实证据，且不存在上述禁止路径时，才允许完成当前 Goal。

## 六、P6 第三十六批防漂移审计记录

仅 `default` 的 `case matches` 闭环再次验证本 Goal 的不可拆分双断言：Verilator DesignDB 提供唯一分支的恒真静态谓词，冻结的 xdebug action 执行 active-driver 合同，Wellen 只在 45ps/65ps 对当前 session 的原始 `.fst` 按需读取运行时事实。该批没有把 `matches` 语法、分支识别或 driver 判定下沉到 FST/Wellen，也没有引入 VCD/JSON 转换、私有索引、离线库、全量快照、TCP/fileport 或 fallback。

对应失败证据和实现为 Verilator `1ae90d55d`、`487482500`、`da63eb075` 及 xdebug-fst `813ee36`、`f8d0aee`。XDD header/ABI 未改变，Wellen 未修改；tagged、binding 与嵌套 wildcard 在该批结束时继续失败关闭。因此该批只能声明“仅 `default` 子集已闭环”，不能声明通用 matches 或 Goal 已完成；后续第五十九批对无 binding packed 嵌套 wildcard 的闭环以本文件后文的新锁为准。

P6 第三十七批继续按同一门禁处理 NBA 纯自保持：DesignDB 的精确 self load 与静态 `event_*` 负责证明 `q <= q` 及其敏感源，Wellen 只对当前原始 `.fst` 的已确定时钟按需读取边沿，xdebug action 执行有界历史回溯与冻结 active-driver 合同。实现 `e700d34` 没有从目标 FST 值是否变化推断 self-hold，没有建立事件索引或离线库，也没有修改 Verilator/Wellen/ABI；因此仍是“静态事实 + 原始 FST 运行时事实 → action 分析”，不是“用 FST 做分析”。

P6 第三十八批进一步锁定：门控 predicate 为假、statement 未赋值，与活动 `q <= q` self-hold 是两种不同静态语义，禁止仅凭 FST “值未变化”合并判断。前者由 DesignDB predicate 证明 statement 不活动，action 保留目标原始 FST 的此前观察点；后者才由精确 self load 触发有界回溯。该批 `d2f3f74` 未修改三方实现或 ABI，继续满足 `GOAL-FST-DIRECT-001`。

P6 第三十九批把 self-hold 证明进一步收紧为 DesignDB predicate-local `self_rhs`：只有 RHS 叶子本身恰好等于目标才发布，xdebug action 不把该角色作为上游数据依赖。FST/Wellen 不负责识别三元语法、自引用或 assignment 身份，只按 DesignDB 已指定的时钟读取当前原始 `.fst` 边沿；因此 `22e810d` 仍严格满足“静态事实 + 原始 FST 运行时事实 → action 分析”，没有退化为波形值启发式。

## 七、P6 第五十二批 interface/modport 防漂移审计记录

interface/modport 六跳闭环严格执行本 Goal 的双断言。Verilator `d5f5b21fd` 只在
`--design-db` 下发布 modport 实例成员、方向、连接和 source driver 静态事实；xdebug-fst
`43f8a0b` 只按冻结 action 合同和唯一性门禁组合这些事实；Wellen 只对当前 session 的原始
`.fst` 按需 resolve/sample 已被静态选定的六个 hop。FST 中存在同值 alias 不能证明
interface 结构，禁止据此枚举或推断成员、方向、driver 或边界。

本批实际输入只有 `testdata/fixtures/interface_modport/waves.fst`，没有 VCD/JSON 转换、预扫、
私有索引、离线数据库、全量内存快照、TCP、fileport 或 fallback。基础 source/sink modport
成员链已闭环；第五十四批又关闭基础 modport X-origin/node-budget，但 ref、嵌套/数组
interface、多 interface driver、端口反馈和联合限制仍未完成。当前 Goal 继续保持 active，
不能以这些基础批次通过宣称完全一致。

P6 第五十三批的 `ref` 边界继续使用同一职责划分：DesignDB 提供 direction=3 和静态端口/
driver 事实，action 执行唯一映射与防反射合同，Wellen 只按需读取原始 `.fst` 的五个 hop。
没有从双向 alias 的 FST 值反推 ref 结构，也没有修改 Verilator/Wellen、转换波形或增加
fallback。该批只关闭单实例连续赋值基础链，多 ref driver、时序/force 和 X-origin 组合仍
属于 active Goal 的未完成项。

P6 第五十四批只增加 `interface_modport_member` 的 X-origin/node-budget 合同证据。实际
波形输入仍是 Wellen 已有 GCD 原始 `.fst`；独立测试 DesignDB 只声明静态 modport 端口边，
action 负责透明 alias 合并、RHS/control 身份保持和 `max_nodes=6` 预算。两条来源链完整
返回且没有假 limitation，现有实现无需修改。禁止把这项结果解释为 FST 自身发现 modport
或执行分析：没有 alias 扫描、值相等推断、波形转换、预扫、私有索引、离线数据库、全量
快照、TCP、fileport 或 fallback。Verilator/Wellen/生产 consumer 均未修改；复杂
interface/ref/反馈和联合限制仍未完成，Goal 保持 active。

P6 第五十五批继续遵守同一门禁：DesignDB 只发布 direction=3 三节点静态 port 环，Wellen
只从现有 GCD 原始 `.fst` 按需确认三个节点的 X 值，xdebug action 用请求内路径状态区分
直接父 alias 反向边与返回更早节点的真实反馈。最终 `loop_detected` 由 action 语义生成，
不是扫描 FST 相同值或 alias 得出。Verilator、Wellen 和 XDD ABI 未修改，没有转换、预扫、
私有索引、离线库、全量快照、TCP、fileport 或 fallback。本批只关闭基础 ref 纯端口环；
复杂 driver/分支/预算反馈、嵌套 interface 和其余 P6 差分仍未完成，Goal 继续 active。

P6 第五十六批在同一原始 FST、同一静态三节点 ref 环上只增加 `max_nodes=2` 证据。预算由
xdebug action 在请求内执行，Wellen 不预扫或索引波形；结果在闭环前以明确 frontier/limit
停止且不伪造 origin。现有实现直接通过，三方实现和 ABI 均未修改。该批只关闭基础
ref-feedback/node 组合，其他预算和复杂反馈仍属于 active Goal。

P6 第五十七批用三份现有原始 `.fst` 证明 `value.at` 公开路径保留 UTF-8 string/delta、real
和 event 类型。Wellen 仍是唯一按需波形访问层，action 只执行冻结 schema 投影；没有 VCD、
FSDB、转换、预扫、索引、离线库、TCP/fileport 或 fallback。该批没有修改生产代码和三方
ABI，只补强 Wellen 完成条件的端到端证据，不影响 P6 其余未完成项，Goal 保持 active。

P6 第五十八批修复 `signal.changes` action 层丢失同时间 FST delta：Wellen 仍直接从原始
`.fst` 发布有序类型化 change，action 使用请求内 `scan_changes` 做窗口与投影，没有预扫后
落盘、私有索引或第二套分析后端。Wellen/Verilator/ABI 未修改，TCP/fileport 与 fallback
仍禁止。该批只关闭 typed change timeline 的 delta 贯通，Goal 保持 active。

## 八、P7 第一批 sanitizer 防漂移审计记录

ASan/UBSan 只作为本仓库 C/C++ 构建和运行时门禁，绝不成为新的波形访问层或分析实现。
两套独立构建继续使用同一原始 `.fst` fixture、Wellen 按需读取、冻结 action 语义和 Verilator
DesignDB 静态事实；没有转换、预扫、私有索引、离线数据库、全量内存快照、export 回灌、
TCP/fileport 或 fallback。宿主运行库缺失经用户明确授权后安装 ABI 匹配且签名通过的系统包，
保持 GCC 8.5 和原测试层级，不把环境修复冒充功能修复。

ASan 的 leak/遇错即停门禁与 UBSan 的遇错即停门禁分别通过 CTest 6/6、pytest 266/266，
普通构建 CTest 8/8、pytest 266/266 与冻结基线继续通过。该结果只证明当前测试覆盖下的
sanitizer 基础门禁；并发 session、engine crash、重复 open/close、FD/长期内存泄漏、
73-action 全差分和 P6 复杂语义缺口仍未完成，Goal 必须保持 active。

## 九、P7 第二批 session 稳定性防漂移审计记录

并发、crash、重复生命周期和 FD/RSS 测试只验证 frontend、registry、UDS 与 engine 资源管理。
8 路并发 session 和长驻 stdio 的 27 轮生命周期都继续打开唯一原始 `waves.fst`；Wellen 只在
engine 内按需提供波形事实，registry/PID/socket/FD/RSS 不参与 driver、X-origin、协议或表达式
分析。没有转换、预扫、私有索引、离线数据库、全量快照、TCP/fileport 或 fallback。

普通 CTest 9/9、ASan 7/7、UBSan 7/7 证明当前 session 稳定性门禁通过；ASan 的 64 MiB
RSS 上限只覆盖 quarantine，真实泄漏仍由 `detect_leaks=1` 否决，FD 仍精确零增长。既有
`SIGKILL → doctor unhealthy → gc` 负责 crash 证据，新测试负责并发和重复资源证据，二者
不能互相替代。P6 复杂语义、73-action 全量差分和最终交付仍未完成，Goal 保持 active。

## 十、P7 第三批 action 覆盖审计防漂移记录

可选 action trace 只记录 pytest 已发生的 xdebug.v1 请求/响应和 test node，不读取 FST、
Wellen 或 DesignDB，也不参与生产 action。审计生成的 NDJSON/JSON/Markdown 只位于 `/tmp`，
禁止回灌请求、作为 export 输入、构建波形索引或离线分析数据库，因此不改变
`GOAL-FST-DIRECT-001`。

首份矩阵的 success/invalid_request 为 73/73，但 resource_missing、empty_result、
boundary_time、multiple_results、limits、truncation、completeness、X/Z 仍分别只有
18/9/22/35/20/3/37/7 项观察证据。这些数字是 TODO 索引，不是完整兼容率；单个 action
十列有勾选也必须继续原版归一化差分。任何 N/A 必须由冻结 schema 与原版真实行为证明，
不得由审计器或实现便利擅自缩小目标。P6/P7 仍有大量缺口，Goal 保持 active。

## 十一、P7 第四批 resource applicability 防漂移记录

resource_missing 由 67 项真实运行错误和 6 项原版/schema 证明的 N/A 完整裁定。66 个 managed
action 使用自身冻结合法 example 到达缺失 session 路由；`session.open` 使用缺失 `.fst`。
六个 `requires=none` action 只在冻结 schema 已禁止 session target、且只读原版与候选完整
响应逐项一致后标 N/A。禁止根据实现方便、category 名称或缺少测试擅自增加 N/A。

审计器已禁止资源路由失败请求虚增 time/limits/XZ 覆盖，旧 boundary_time 23 校正为 22。
applicability 文件只是合同证据元数据，不含波形、不参与 action，也不得回灌分析。生产仍由
Wellen 直接按需读取当前 session 原始 `.fst`，本批没有修改 Wellen、Verilator、ABI、backend
或 transport。empty/truncation 等其余维度仍未裁定，Goal 保持 active。

## 十二、P7 第五批 empty/truncation 防漂移记录

empty_result 已由 9 项提高到 19 项 observed：新增六项 registry/list/cursor 空集合，以及 APB、
AXI、event 的四项合法零匹配查询。所有证据必须是 `ok=true` 且包含冻结的零 cardinality/空
结果集合；资源失败、schema 失败和缺字段响应一律不能冒充空结果。

truncation 审计定义已修正：只接受 `response_truncated=true`、非空 `truncation_scopes` 或显式
limit 终止；普通 `limitations` 不等于截断。1019-event trace 重算为 16 项 observed。两维均
尚未完成全 action applicability 与原版归一化差分，不允许把 19/73 或 16/73 宣称为兼容率。

本批没有修改生产 action、Wellen、Verilator、ABI、backend 或 transport。测试仍只让 Wellen
直接按需读取原始 `.fst`；trace 只用于测试覆盖审计，不得参与分析。Goal 保持 active。

## 十三、P7 第六批主结果空语义防漂移记录

空结果分类增加 `total_count=0`、`found/diff_found=false` 和明确的主结果集合；禁止将空
diagnostics、suggestions、constraints、任意空 `data` 或 `returned_count=0,total_count>0`
计为空结果。该边界使旧 trace 的 empty_result 从 19 校正为 25、multiple_results 从 35
校正为 42。

八项新增真实协议扫描把 empty_result 提高到 33：AXI cursor/latency/timeline/pair、stream
query/export、sampled-pulse 和 handshake。最新 1046-event trace 同时得到 boundary_time 24，
truncation 保持 16。剩余 action 仍须实测或原版/schema 证明 N/A，Goal 保持 active。

所有请求继续直接使用原始 `.fst`；没有 FST 转换、持久索引、离线分析库、TCP/fileport 或
fallback，也没有修改生产 action、Wellen、Verilator 或 ABI。

## 十四、P7 第七批 empty_result 全量裁定记录

empty_result 已达到 45 observed + 28 N/A，73 项全部裁定。18 项 N/A 由冻结原版成功 schema
无法表达主结果基数证明；10 项 N/A 由公共非空输入/target 约束和冻结原版非空成功映射证明。
不得把两类证明混写，也不得根据 action 名称或实现方便增加 N/A。

最新 1073-event trace 中 empty_result 未裁定为 0；resource_missing 仍为 67 observed + 6 N/A。
原版资源适用性差分 6/6、empty applicability 检查器、pytest 全量和 CTest 9/9 通过。

本批没有修改生产 action、Wellen、Verilator、ABI、backend 或 transport。显式 export 不参与
后续分析，唯一波形事实路径仍是当前 session 原始 `.fst` 由 Wellen 按需读取。Goal 保持 active，
下一维度为 truncation。

## 十五、P7 第八批 truncation 全量裁定防漂移记录

truncation 已达到 35 项真实运行 observed + 38 项 N/A，严格覆盖 73 action。38 项 N/A 中，
36 项由全部冻结成功 Schema 无法表达 canonical truncation 证明；`signal.resolve` 与
`signal.xz_verify` 另由冻结单一 final-leaf/标量 first-mismatch 生命周期和无结果 limit 证明。
任何未来 Schema 增加 selector、row limit、可截断集合或改变终止语义，检查器必须失败，禁止
继续沿用旧 N/A。

`stream.validate` 因冻结请求明确含 `line_limit`，不得降级成 N/A。真实原始 FST 已同时触发
control X/Z 与 data X/Z issue，并证明 line_limit 截断。AXI 未知 reset/valid/ready 也必须报告
`analysis_transactions`，不得静默当低电平后虚报完整。上述修复全部位于 xdebug-fst；没有修改
Wellen、Verilator 或 XDD ABI。

唯一允许的数据流仍是“当前 session 原始 `.fst` → Wellen 按需读取 → 冻结 action 语义”，
DesignDB 只提供静态设计事实。禁止把 FST 变成分析引擎，禁止 VCD/JSON 转换、预扫持久化、
私有索引、离线数据库、全量快照、export 回灌、TCP/fileport 和 fallback。该维度关闭不代表
Goal 完成，其余覆盖维度、复杂 P6 差分和最终 73-action 原版归一化差分仍保持 active。

## 十六、P7 第九批 limits 全量裁定防漂移记录

limits 已达到 29 项冻结合法结果上限请求 observed + 44 项 N/A，严格覆盖 73 action。N/A 只能
由冻结请求 Schema 不存在结果规模字段证明；存在的 `timeout_ms` 是 frontend watchdog，不能
算作 action 返回集合上限。审计器和独立适用性检查都锁定这一边界。

29 项的请求证据与 truncation 行为证据保持分离：接受 `line_limit/max_*` 证明 limits 维度，
实际 `response_truncated` 或非空 scope 由上一批 truncation 门禁证明。不得用 watchdog、非法
请求、资源路由失败或字段名猜测填充 limits。

本批不改变唯一原始 FST→Wellen 按需读取→action 的事实流，不修改 Wellen、Verilator、XDD
ABI、backend 或 transport，也不增加转换、离线分析、TCP/fileport 或 fallback。Goal 继续 active。

## 十七、P7 第十批 boundary_time 全量裁定防漂移记录

boundary_time 已达到 30 项成功零时间/闭区间 observed + 43 项 N/A，严格覆盖 73 action。
适用性只能由冻结请求中的 `time`、`times` 或 `time_range` 证明；普通地址/ID range 的
`begin/end` 即使为零也不是时间边界。非法时间请求、资源错误和 schema 拒绝不能获得信用。

新增六项门禁均在真实 session 中成功执行，波形 action 继续由 Wellen 直接按需读取原始
`.fst`。本批没有修改生产 action、Wellen、Verilator、XDD ABI 或 transport，没有转换、
离线分析、TCP/fileport 或 fallback。其余覆盖维度和最终原版归一化差分仍未完成，Goal active。

## 十八、P7 第十一批 multiple_results 全量裁定防漂移记录

multiple_results 已达到 53 项真实运行 observed + 20 项 N/A，严格覆盖 73 action。主结果
只能是 `summary/data` 的直接计数或集合；嵌套 validation、diagnostic、suggestion 数组不得
获得信用。19 项 N/A 由冻结成功响应 Schema 不可表达大于一证明；`signal.resolve` 由单个
最终叶节点精确解析合同证明。`session.close/session.kill` 的 `session_id=all` 能批量清理，
必须保留为适用项，禁止以后退化成 N/A。

运行时门禁使用直接原始 FST 和两个真实 UDS session。双 Stream 配置还暴露并修复了公开
枚举 `disabled` 与冻结 `none` 的合同差异。最终新 trace 为 53 observed + 20 N/A，且没有以
多结果测试替换 AXI/Stream 空结果证据。pytest 全量和生产修复后的 CTest 9/9 通过。

本批没有修改 Wellen、Verilator 或 XDD ABI。唯一事实流仍为原始 `.fst` 由 Wellen 直接
按需读取，xdebug action 执行语义，DesignDB 提供静态事实；FST 不是分析引擎。禁止转换、
离线索引/数据库、全量快照、export 回灌、TCP/fileport 和 fallback。Goal 保持 active，
completeness、X/Z、P6 剩余复杂差分和最终原版归一化差分尚未完成。

## 十九、P7 第十二批 completeness 全量裁定防漂移记录

completeness 已达到 41 项真实运行 observed + 32 项 N/A，严格覆盖 73 action。只允许
`summary/data` 直接布尔完整性字段提供信用，必须包含冻结的 `value_width_complete`；嵌套
validation/diagnostic 同名字段无效。32 项 N/A 只能由全部冻结成功响应 Schema 不可表达直接
完整性字段证明。

expr.eval_at、list.first_change、verify.conditions 现在与原版 LogicValue 后处理一致，在
Wellen 已提供确定 width 时报告 `value_width_complete=true` 和空诊断。不得以固定 true 掩盖
未来真实宽度缺失；如果 Wellen/backend 诊断变化，action 必须传播 false 与原因，适用性检查
不得被改成 N/A 逃避。

新 trace 为 41 observed + 32 N/A，pytest 全量和 CTest 9/9 通过。本批没有修改 Wellen、
Verilator 或 XDD ABI。原始 FST 仍由 Wellen 直接按需读取，FST 不是分析引擎；禁止转换、
离线索引/数据库、全量快照、export 回灌、TCP/fileport 和 fallback。Goal 保持 active，X/Z、
P6 剩余复杂差分和最终原版归一化差分尚未完成。

## 二十、P7 第十三批 X/Z 全量裁定防漂移记录

X/Z 已达到 23 项真实运行 observed + 50 项 N/A，严格覆盖 73 action。只有冻结成功响应中
可达的 `LogicValue`、显式四态状态或非零 X/Z/unresolved 计数能够获得信用；单纯分析不完整、
普通 `unknown` 枚举和请求中出现 X/Z 文本均不能替代成功响应证据。50 项 N/A 必须由
`tools/check_xz_applicability.py` 对全部冻结 Schema 的穷举结果证明，禁止为减少测试量扩大
N/A 范围。

23 项适用动作均已有真实原始 FST 证据。特别锁定 handshake 的 Z valid finding、AXI 已完成
事务的 unresolved address 过滤、batch 嵌套 LogicValue，以及 counter/event/list/signal/
stream 的 X 值或非零四态计数。Wellen 只保真返回当前 session FST 的四态事实，xdebug action
负责分析和合同投影，Verilator DesignDB 只提供静态事实；不得漂移为“由 FST 做分析”。

全新 1169-event trace 的十个维度均无缺项，393 项 pytest、CTest 9/9、适用性检查和原版
资源差分通过。本批未修改 Wellen、Verilator 或 XDD ABI，不存在转换、离线索引/数据库、
全量快照、export 回灌、TCP/fileport 或 fallback。Goal 继续 active；P6 剩余复杂差分和最终
73-action 原版归一化差分仍未完成。

## Phase 5 循环选择语义锁（2026-08-11）

Goal 后续执行必须永久保留本次原版只读重放已经证明的四项行为：物化 unpacked `dout`
使用展开后最终 selector 并返回单 statement/六 RHS 的 `multiple_rhs_sources`；packed
`flag[2]` 对完整 selector 域做存在性求值并返回双 statement/九 RHS 的
`multiple_active_candidates`；动态 RHS 证据保持 `mask_a[lane]`、`mask_b[lane]` 且因原始
FST 不存在该动态路径而报告 `signal_not_found`；多活动候选在当前 node 创建前终止，根查询
必须是空 hops、count 0、evidence root/hop_index 0。

Verilator 只允许消费已证明必要且已回归的三个附加 DesignDB role：`target_loop_index`、
`rhs_loop_selected`、`rhs_loop_index`，当前精确 SHA 为
`9c8ae78cba35ab152e13644a50d6fc0c882955d4`，XDD header/ABI 保持不变。Wellen 不承担上述
分析，只直接按需读取当前 session 的原始 `.fst` 值与时间；xdebug action 负责组合静态事实
并执行冻结语义。禁止转换、预扫、索引、离线库、全量快照、export 回灌、TCP/fileport 和
fallback。

所有后续 C/C++ 工作固定使用 `XDEBUG_GCC_TOOLCHAIN` 指向的 GCC/G++ 13.3.1，仓库通过
`XDEBUG_VERILATOR_REPO`、`XDEBUG_WELLEN_REPO` 定位；缺失依赖安装到对应仓库或
`/workspace/work/xdebug_oc` 私有目录。本批的 13/13 Verilator XDD、75/75 combined、
397/397 pytest 和 9/9 CTest 是持续回归门禁，不是 Goal 完成声明。P6 剩余复杂语义与最终
73-action 原版归一化差分未全部关闭前，Goal 必须保持 active。

## GCC 13 私有 sanitizer 运行时锁（2026-08-11）

ASan/LSan/UBSan devel 与 runtime 已按签名验证后安装到
`${REPO_ROOT}/../.toolchains/gcc-13`，不得因 sanitizer 动态库缺失切换回系统
GCC 8、关闭 instrumentation、缩小测试集合或更换 backend/fixture。Codex 环境必须携带私有
`lib64`；pytest 子进程只能追加 Wellen/wellenx 路径，不能覆盖父进程 `LD_LIBRARY_PATH`。

当前 GCC 13 ASan 和 UBSan 独立 CTest 均为 9/9，普通/ASan/UBSan 全量 pytest 均为
396/396，无 sanitizer 诊断。sanitizer 永远只是 C/C++ 实现质量门禁：其测试仍直接打开同一
原始 `.fst`，Wellen 按需提供波形事实，DesignDB 提供静态事实，xdebug action 执行分析；
不得引入转换、索引、离线库、全量快照、TCP/fileport 或 fallback。该门禁通过不表示 P6、
最终原版差分或 Goal 已完成。

## matches 嵌套通配语义锁（2026-08-11）

无 binding packed assignment pattern 内的 `.*` 已按逐位 value/mask 闭环：普通成员 mask
必须为一，只有通配成员 mask 为零，并以四态 `===` 比较掩码后的 selector/value。该规则同时
适用于 `case matches` 和独立 `matches`；禁止降级成会吞掉普通 X/Z 的 casez/casex 近似。

Verilator DesignDB 只发布上述静态掩码 predicate，当前锁定 SHA 为
`9c8ae78cba35ab152e13644a50d6fc0c882955d4`，XDD header/ABI/capability 未变。xdebug action
在 active time 执行 predicate 并选择 driver；Wellen 只从当前原始 `.fst` 按需返回控制值。
不得把 pattern 语义下沉到 Wellen/FST，也不得转换、预扫、建立索引/离线库/全量快照、回灌
export、增加 TCP/fileport 或 fallback。

`c4cc865` 的旧固件 `SIGNAL_NOT_FOUND` 是动态修改前证据；最终门禁为 45ps data item、65ps
case default/standalone else 四项唯一选择，combined 75/75、pytest 397/397、CTest 9/9、
Verilator XDD 13/13。PatternVar binding 与全部 tagged union/expression/pattern 继续失败关闭，
本批不得被描述为“通用 matches 完成”，Goal 继续 active。

# Goal 最新兼容验收修正（2026-08-13 用户确认）

用户已将验收目标明确修正为“能力一致、信息语义一致”，不要求与原版输出完全一致。当前
Goal 系统不能原地改写 active objective，因此本节作为 Goal 的最新权威修正；与旧 objective
或本文历史文字冲突时，以本节为准。

- 必须覆盖原版 73 个公开 action 所代表的能力，但不要求响应逐字段、逐字或逐顺序相等。
- 必须保证影响用户判断的目标、时间、值、关系、来源、计数、范围、完整性、截断和错误原因
  在语义上等价；未知和不完整状态必须如实表达。
- summary/warning/suggestion 的措辞、无序结果排列、等价渲染、易变元数据和不影响结论的
  冗余诊断字段不再纳入强制相等门禁。
- “原版归一化差分”自此指能力与关键信息语义差分，不再指整份归一化 JSON 相等。
- 本修正不改变 `GOAL-FST-DIRECT-001`、FST 唯一输入、Wellen 直接按需读取、禁止转换/离线
  分析/fallback、TCP/file 裁剪和 Verilator 克制修改等约束。

## PatternVar 不可观测绑定锁（2026-08-13）

PatternVar binding 的局部 lowering 变量未写入 FST 时，关系只能来自 Verilator DesignDB，运行
时活动谓词和值只能来自 Wellen 对当前原始 FST 的按需读取。xdebug-fst 仅可透明展开 DesignDB
证明为纯组合、有真实 RHS、无 self/event/NBA/force 的不可观测中间量；时序状态、force、无
静态证据或不完整关系必须失败关闭，不得按值相等推断，也不得要求 Wellen/FST 承担 PatternVar
分析。本锁已由真实 45ps/65ps active-driver/chain 与独立四态 X-origin 回归固化。

## Ref 分支终端语义锁（2026-08-13）

X-origin 的 ref/modport hop 只对物理 alias 路径透明，不能把共享非 port 前缀但终端不同的
反馈环与真实来源合并。语义 identity 必须保留最终 status、current signal 和 onset；限制发生
时必须保留省略来源与 frontier。`36f2b6c` 已用当前原始 GCD FST 锁定 driver→ref→loop 与
driver→ref→origin 同时存在，以及 depth+chain 联合限制。不得用 FST 值相等选择或删除分支。
`3080a4c` 将同一要求扩展到 node+chain 联合限制；node budget 耗尽不是丢弃其他来源的理由。
