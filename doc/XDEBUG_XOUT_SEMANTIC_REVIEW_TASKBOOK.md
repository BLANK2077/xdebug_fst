# xdebug 与 xdebug_fst 全 Action XOUT 语义评审及修复计划

## 一、目标与执行约束

对冻结的 73 个公开 action 逐项获取原版 `xdebug` 和 `xdebug_fst` 的真实 JSON/XOUT 返回，独立评审：

- XOUT 是否包含完成调试判断所需的关键信息。
- 是否遗漏嵌套值、时间、对象身份、结论、完整性、截断状态或错误原因。
- 是否重复展示同一事实、暴露无价值内部字段或生成难以阅读的 JSON dump。
- 原版 XOUT 自身是否存在遗漏、冗余或布局不合理。
- 不要求两边逐字一致；只要求能力一致、信息语义一致，并允许 `xdebug_fst` 在可读性上合理优于原版。

范围锁定：

- 修复范围仅为 `${REPO_ROOT}`。
- `${XDEBUG_ORIGINAL_ROOT}` 全程只读；原版问题只记录，不修改。
- 原版直接复用 `xverif` 的 Native XOUT 73-action 矩阵和现有 fixture，不创建、转换或重新设计 fixture。
- 原版真实 FSDB/NPI/VCS 测试按仓库规则在宿主环境运行；若 fixture 缓存缺失则停止并报告，不私自生成或 fallback。
- `xdebug_fst` 继续只读取原始 FST；不修改 Wellen、Verilator，不引入格式转换、离线波形数据库或其他 backend。
- 不改变 73 个 action 的 JSON schema、请求参数和 JSON 响应语义；本轮公开变化仅限 XOUT 文本投影。
- 不创建 PR、不推送远端；Git 提交仅发生在 `xdebug_fst`，使用详细中文标题和正文。

## 二、阶段实施与分批提交

### 阶段 0：任务书、进度记录和 Goal

1. 首先创建 `doc/XDEBUG_XOUT_SEMANTIC_REVIEW_TASKBOOK.md`，完整写入本计划，不能摘要或弱化。
2. 在同一任务书中建立独立的进度区，持续记录：

   - 73-action 覆盖状态。
   - 原版与 FST 每次捕获的版本、命令、fixture、返回码和哈希。
   - 每项发现的严重度、结论、修复 commit 和验证结果。
   - 尚未完成的 action、变体和阻塞原因。

3. 任务书落盘后创建新的 Goal。Goal 必须明确：

   - 完成全部 73 个 action 的原版/FST JSON-XOUT 实际调用和评审。
   - 主成功场景全部覆盖，并覆盖适用的错误、空结果、多结果、截断、限制、X/Z 和进制变体。
   - 修复 `xdebug_fst` 的所有关键语义遗漏和未解释冗余。
   - 原版仓库及 fixture 只读，原版不合理输出进入最终问题清单。
   - FST 是候选实现唯一波形输入，且不是分析引擎。
   - 只有矩阵、修复、全量测试和最终报告全部完成后才可将 Goal 标为 complete。

4. 首次提交：

   `文档：建立全 Action XOUT 语义评审任务书`

### 阶段 1：冻结基线并建立双端捕获矩阵

- 记录原版可执行文件 SHA、git revision、build id、schema revision和 73-action catalog。
- 在宿主环境运行 `xdebug.native_xout_all` 正式 suite，直接消费 `xverif_fixture` 已有缓存，捕获原版：

  - 73 个 primary 成功调用。
  - 既有错误族。
  - 支持 `value_format` 的 bin/dec 变体。
  - 已有 protection 场景。
  - 每次调用对应的 JSON 与原始 XOUT。

- 原版原始捕获写入临时证据目录，不向 `xdebug_fst` 提交 FSDB、daidir、绝对缓存路径或其他 proprietary 资源；任务书只记录去敏摘要和 SHA。
- 在 `xdebug_fst` 建立对应的 73-action XOUT runner，使用现有 counter、APB、AXI、stream、xprop、DesignDB 等 FST fixture，不新增波形。
- 每次候选调用同时获取 JSON 与 XOUT；同一请求在两种格式下必须返回相同业务结论。
- 建立 action 评审清单，每项明确：

  - 必须展示的关键事实。
  - 可以省略的机器诊断字段。
  - 禁止重复或泄漏的字段。
  - 适用的关键变体。
  - 原版与 FST 使用的 fixture 角色。

- 跨后端不比较不同 fixture 的具体数值；完整性通过“各自 XOUT 对各自 JSON 的关键事实投影”验证，跨后端比较信息角色、结论类别和可读布局。

提交：

`测试：建立七十三项 XOUT 双端语义捕获矩阵`

### 阶段 2：修复 XOUT 调度和通用渲染基础设施

当前 `xdebug_fst` 虽有 `EngineActionHandler::render_xout`，但 server 未调用它，导致所有 action 基本退化到通用 renderer。修复为：

- 内部 dispatch 结果同时携带 canonical JSON response 和可选 handler XOUT，不把 XOUT 字段注入公开 JSON。
- handler 在响应完成 canonical 化并通过 schema 校验后渲染，保证读取的是最终公开语义。
- managed-session UDS 使用私有 sidecar/envelope 传递 handler XOUT；前端只向用户暴露原有 JSON 或 XOUT，不改变公开协议。
- one-shot、stdio-loop、managed UDS 和 MCP direct 对相同请求生成一致 XOUT。
- 默认 handler 不再返回 `response.dump(2)`；没有专用 renderer 时统一走通用 XOUT renderer。
- 去掉通用 renderer 当前隐含的 20 行静默裁剪。结果规模由 action 的公开 `line_limit` 和 truncation 合同控制；若渲染层仍需保护上限，必须显式展示省略数量，不能静默丢失。
- 增强通用表格对 LogicValue、字段映射和有限嵌套对象的支持；复杂二维/链式结构交给专用 renderer，避免通用递归产生不可读 section 名。

提交：

`修复：恢复 Action 专用 XOUT 调度与无损通用渲染`

### 阶段 3：修复基础、会话、波形和列表类 Action

逐项审查并修复：

- `actions`、`schema`、`batch`。
- session open/close/list/doctor/gc/kill。
- scope、cursor、list、event。
- `value.at`、signal、expression、verify、window、counter、handshake、nwave。

重点处理：

- `value.at` 必须把多信号 × 多时间渲染为真正的值矩阵；不能只显示时间。
- signal/list/event 的时间、值、状态、数量、空结果和截断信息必须可见。
- X/Z 值保留必要 bit 证据；已知值不重复输出 `known=true`、完整 bits 和 width。
- summary 与 data 中相同事实只显示一次。
- 成功配置类 action 至少明确对象名和操作结果；不输出无意义内部存储细节。
- 错误 XOUT 保留 action、错误码、错误层、关键参数、候选项和恢复建议，删除重复 failure summary。

提交：

`修复：补全基础波形与列表类 Action 的 XOUT 语义`

### 阶段 4：修复 APB、AXI 和 Stream XOUT

- 为 APB/AXI query、export、statistics、cursor、window、analysis、stall、timeline、pair、outlier 等恢复或实现领域表格。
- 为 stream config、describe、query、validate、export 恢复紧凑的 transfer/packet/field 布局。
- 地址、数据、响应、方向、时间、延迟、事务身份、错误、完整性和截断状态按 action 语义展示。
- `value_format` 只影响逻辑值表达，不影响事务语义。
- export XOUT 展示输出路径、格式、实际导出数量和 preview/truncation 状态，不把完整导出内容重复打印到终端。
- 不机械复制原版布局；原版存在重复列或低价值字段时，在评审报告中登记，并采用更简洁但语义完整的 FST 布局。

提交：

`修复：完善 APB AXI 与 Stream 的领域化 XOUT`

### 阶段 5：修复 Design 与 Combined Trace XOUT

覆盖：

- `signal.canonicalize`、`signal.resolve`。
- `trace.driver`、`trace.load`。
- `trace.active_driver`、`trace.active_driver_chain`、`trace.x_origin`。
- `scope.roots` 的 design/waveform 对齐信息。

XOUT 必须保留：

- 查询信号、请求/有效时间和值。
- driver/load 的类型、关系和源码位置。
- active 控制信号、选择原因和不确定性。
- chain id、hop、relation、signal、time、terminal status。
- X onset、来源、环、歧义、未解析 frontier、limits 和 completeness。
- 源码窗口只显示对结论必要的行，不重复 dump 同一 source context。

提交：

`修复：补全设计关系与动态追踪 XOUT 证据`

### 阶段 6：原版独立评审和问题分类

不能沿用原版现有“73 项全部 PASS”的历史结论，按本轮统一口径重新评审。每个 action 都给出：

- `完整且精炼`
- `语义完整但有冗余`
- `语义完整但布局不佳`
- `存在非关键遗漏`
- `存在关键语义遗漏`
- `场景不适用`

原版问题按以下严重度登记：

- P0：输出错误事实或会误导结论。
- P1：关键事实缺失，用户必须转查 JSON。
- P2：明显冗余、重复或布局影响使用。
- P3：风格和可读性建议。

原版只形成证据、建议和代表性摘录，不修改 `${XDEBUG_ORIGINAL_ROOT}`。

提交：

`文档：完成原版 xdebug 全 Action XOUT 独立评审`

### 阶段 7：最终门禁和验收报告

生成最终报告 `doc/XDEBUG_XOUT_SEMANTIC_REVIEW.md`，至少包含：

- 73 个 action 的逐项对照表。
- 每项原版结论、FST 修复前问题、修复后结论和证据哈希。
- 原版不合理 XOUT 的独立问题清单。
- `xdebug_fst` 剩余已接受差异及理由。
- fixture、版本、实际命令、执行环境和测试统计。
- 不含 proprietary 数据、缓存路径和未去敏输出。

最后提交：

`报告：完成七十三项 XOUT 语义对照与最终验收`

## 三、XOUT 统一评审合同

每个 action 的 XOUT 至少接受以下检查：

- Header 与 action 正确，输出恰好以一个换行结束。
- 关键目标、对象身份、请求时间/范围、有效采样时间和值可见。
- verdict、状态、计数、错误、finding 和 artifact 语义完整。
- truncated、limited、incomplete、unknown、ambiguous 不得被省略或伪装成完整确定结果。
- 空结果必须明确，不能只显示空标题。
- 错误原因与 JSON 属于同一错误类别，并保留可操作诊断。
- 已知 LogicValue 不重复展示 `known=true`、width 和等价 bits。
- 不输出原始 JSON dump、内部 pointer、临时路径、build telemetry 或无意义空 section。
- 不在 summary/data 中重复同一事实；重复仅在确有上下文价值时允许，并在清单中登记理由。
- XOUT 自身不得静默截断 JSON 已返回的数据。
- 原版不合理输出不自动成为 FST 的兼容要求。

## 四、测试与验收标准

必须通过：

- 原版 73/73 primary action 实际运行并捕获 XOUT；原版仓库保持干净。
- `xdebug_fst` 73/73 primary action 同时获得成功 JSON 和 XOUT。
- 所有适用 action 完成错误、空结果、多结果、截断/限制、X/Z、bin/dec 变体。
- 每个 action 的关键语义投影均能从 XOUT 找到；不存在未登记遗漏。
- `value.at` 至少覆盖单信号单时间、单信号多时间、多信号多时间、X/Z 和缺失值。
- one-shot、stdio-loop、managed UDS、MCP direct 的 XOUT 内容一致。
- 现有完整 pytest、CTest、GCC 13、ASan 和 UBSan 回归通过。
- FST 输入门禁通过；无 VCD/FSDB fallback，无 Wellen/Verilator 改动。
- `git diff --check` 通过，任务书进度完整，工作树只包含本轮预期提交。
- Goal 仅在 73 项全部有结论、所有 P0/P1 FST 问题关闭、P2 问题已修复或明确接受、最终报告提交后标为 complete。

## 五、默认决策

- 原版 XOUT 是重要参考，不是不可质疑的 golden text。
- FST 修复优先采用专用 handler renderer；通用 renderer 只负责结构简单的 action。
- 原版与 FST fixture 不同，因此不比较具体波形值；两端分别用自己的 JSON 证明 XOUT 语义完整。
- 原版现有 fixture 只消费缓存，不重新仿真；缓存缺失时不自行 fallback。
- 本轮不修改 public JSON schema，不修改 Wellen、Verilator、xverif，也不新增 fixture。

## 六、执行进度

### 当前状态

- 阶段 0：已完成，提交 `91738f1`。
- 阶段 1：已完成；原版与候选实际捕获均已完成，冻结候选 catalog 的 73 项均有同响应 JSON/XOUT 证据。
- 阶段 2：已完成并提交；XOUT sidecar、专用 renderer 调度和无损嵌套渲染通过 73/73 审计。
- 阶段 3–5：已完成并分批提交；value、session、export、scope、stream 和 trace 关键领域输出已修复。
- 阶段 6：已完成；原版 73 项已重新独立评审，未沿用历史全 PASS 结论。
- 阶段 7：已完成；最终报告、CTest、ASan、UBSan、依赖仓库和工作树门禁全部通过。
- 当前阻塞：无。

### 提交与验证记录

| 阶段 | 状态 | Commit | 验证 | 备注 |
| --- | --- | --- | --- | --- |
| 0 | 已完成 | `91738f1` | `git diff --check` | 新 Goal 已建立并处于 active 状态 |
| 1 | 已完成 | `257039d` | 原版 nightly 1/1；候选 pytest 全量通过；73/73 捕获 | 当前原版 HEAD 的 catalog 漂移作为评审发现记录，不改变冻结协议 |
| 2 | 已完成 | `72bbd77` | GCC 13 构建、XOUT 定向测试、73/73 同响应语义审计通过 | 无公开 JSON/schema 变化 |
| 3 | 已完成 | `4ce6941` | session、artifact、scope roots 定向回归通过 | session 最小身份完整，不泄漏运行时遥测 |
| 4 | 已完成 | `6bb4574` | Stream 全量与 packet XOUT 回归通过 | 单 beat 去重，多 beat 保留 preview 证据 |
| 5 | 已完成 | `2bfdd58` | Design/Combined 全量回归通过 | source path、chain、hop、origin 与 ambiguity 领域表 |
| 6 | 已完成 | 待报告提交 | 原版 73 primary 逐项复核 | 发现 5 个 P1、4 个 P2 及 suite 工作树副作用 |
| 7 | 已完成 | `c2e2a9b`、待最终报告提交 | GCC 13/ASan/UBSan 各 414/414 pytest、9/9 CTest；MCP direct 等价通过 | 最终门禁通过 |

### Action 覆盖汇总

| 项目 | 已完成 | 目标 |
| --- | ---: | ---: |
| 原版 primary XOUT | 73 | 73 |
| FST primary JSON/XOUT | 73 | 73 |
| 原版逐项评审 | 73 | 73 |
| FST 修复后逐项验收 | 73 | 73 |

### 2026-08-13 基线与首轮捕获记录

- 原版正式入口：`XVERIF_TEST_EXECUTION_ENV=host .conda-xverif/bin/python -m pytest --xverif-gate nightly --xverif-suite xdebug.native_xout_all -q`。
- 原版结果：1/1 通过，1270 项未选择，73 个 primary、错误族、保护场景及进制变体实际执行；运行使用 `xverif_fixture` 既有缓存，没有 prepare、重建或 fallback。
- 原版 suite 会重写 tracked 评审报告中的耗时、缓存临时路径和当前 build id；运行后已只恢复该报告，`xverif` 工作树重新干净。这一副作用将列入最终基础设施评审。
- 候选入口：GCC 13 二进制下执行全量 pytest，同时设置测试专用 `XDEBUG_XOUT_AUDIT_CAPTURE=1`，使同一次 stateful action 响应同时留下 canonical JSON 与 XOUT，避免重放副作用请求。
- 候选最终基线捕获：全量 pytest 通过，捕获 1067 组同响应 JSON/XOUT；临时证据 `/tmp/xdebug-fst-xout-audit-20260813-v2.ndjson` 共 1067 行，SHA-256 为 `a8a256939ebdeec9682109af0066cf6adeac31130717d71b8bfae57b70d6dc42`。对应 action coverage 证据 1219 行，SHA-256 为 `23b8a3a2062c20a9045605405773485b584550f34b95d8450148b3d78966b115`。
- 候选冻结 catalog 的 73/73 Action 同响应语义审计通过；审计 JSON SHA-256 为 `0003ad58626a8e63fb2bfc6fcb47cfd75c9a990c983ca68f4909d8d63e939683`，Markdown SHA-256 为 `844e25918b10cebccc9c09647ba0e1951a34efa47fc2802b02f135252a5438b8`。
- Catalog 漂移发现：当前原版 HEAD 与候选冻结基线虽然均为 73 项，但原版独有 `apb.export`，候选独有 `session.kill`。本 Goal 明确禁止改变冻结 public schema/action 能力，因此本轮不擅自增删 Action；该差异作为原版版本漂移和跨版本评审边界写入最终报告，逐项对照以各自冻结的 73 项为准。
- 首轮 P1 发现：`value.at` JSON 正确包含多信号多时间值，但候选通用 XOUT 丢失 `samples[].values[]`；首轮专用 renderer 已使 2 信号 × 5 时间矩阵完整显示。
- 首轮 P1 发现：候选通用 renderer 对对象数组固定裁剪为 20 行，且没有有效的省略提示；首轮实现已移除该静默裁剪并递归投影嵌套集合，待全矩阵验证。

### 2026-08-13 最终 XOUT 捕获与评审记录

- GCC 13 全量 pytest：414/414 通过。
- 最终同响应 JSON/XOUT 证据 1071 行，SHA-256 为 `7ed4b817746c2ac83fc45b6ebc887fbd65998b5ab568147367844143ec522213`。
- 最终 Action coverage 证据 1223 行，SHA-256 为 `68a969d053673820dde0c1eee4ea6342bd78eb15a044ea0646617104479862fe`。
- 最终审计 JSON SHA-256 为 `61c3e808b879d73368acdfdda595f7a6702e1015aa48e68ef303fef308aee1e7`，冻结 73/73 Action 通过。
- 原版 P1：`session.open` 未显示会话身份；`axi.export`、`event.export`、`list.export`、`stream.export` 未显示实际 artifact 路径。
- 原版 P2：`actions`/`batch` 重复计数和 build telemetry；`session.close`/`session.gc` 输出缓存路径、socket、PID、设备号和 inode；正式 suite 会改写 tracked 报告的易变字段。
- 候选剩余 P2：异构 `batch` 以及配置、sampling、validation 的字段路径式嵌套 section。它们语义完整、没有静默裁剪，且有助于映射 JSON 字段，已在最终报告明确接受。
- 真实 xverif MCP direct 集成测试已证明：无状态 one-shot XOUT 与原生 one-shot 逐字一致，managed session XOUT 与原生 UDS 路由逐字一致；未使用 TCP 或 fileport。
- GCC 13 普通构建 9/9 CTest 通过；GCC 13 ASan 构建 414/414 pytest、9/9 CTest 通过；GCC 13 UBSan 构建 414/414 pytest、9/9 CTest 通过。

### 2026-08-13 Trace 源码上下文遗漏修正

- 用户复核发现 `trace.active_driver_chain` 只有 file/line/hop 表，没有原版的源码窗口；阶段 5
  和阶段 7 的旧结论在源码展示这一子能力上不成立，已重新打开并修复。
- 已逐项检查全部五个 Trace Action，并直接复核原版 `trace_source_path_formatter`：默认上下文
  3 行、同文件相邻点小于 10 行合并、活动行标记和 `active_signals` 关联均已实现。
- `trace.driver/load/active_driver/active_driver_chain` 补全冻结 JSON 中已有的
  `source_context`；`trace.x_origin` 不改冻结 schema，只增强 XOUT。
- 源文件仅依据当前 session 的绝对 DesignDB/FST 路径确定性解析；不可读时不伪造、不 fallback，
  并保留 paths/hops 表防止信息丢失。
- 新增五 Action XOUT、JSON context、合并窗口、缺源边界和审计器领域投影测试；新鲜同响应
  审计 73/73 通过。Wellen、Verilator、XDD ABI、FST 与 transport 均未修改。
