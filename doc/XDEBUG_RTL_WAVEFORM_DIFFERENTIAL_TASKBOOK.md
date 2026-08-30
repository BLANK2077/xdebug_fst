# xdebug 原版 RTL 与波形差分补测任务书

## 一、任务身份与最终目标

- Goal：`01a050fa-b864-7ce2-af88-56083d84ea21`
- 状态：active
- 当前仓库：`xdebug_fst`
- Goal 起点：`fix/per-session-registry-flock`，
  `302d0c2b40eff1547d7479c6838b6866c8ab7c7f`
- 建立日期：2026-08-30

本任务逐项比较当前 xdebug-fst 与只读原版 xdebug 的测试 RTL、fixture 生成合同、波形和公开
Action 可观察语义。文件数量差异本身不等于功能缺口；只有完成资产发现、RTL 场景归一化和
FSDB/FST 公开 Action 证据对照后，才能判定 exact、semantic-equivalent、partial、missing 或
proven-unobservable。所有 partial/missing 都必须先保存红灯证据，再在当前仓库补齐测试和必要
实现，直到最终矩阵没有未关闭项。

本文件既是固定任务书，也是唯一持续进度记录。阶段状态、命令、结果、commit 和剩余差异都
追加到本文件，避免计划与执行状态分叉。

## 二、最高优先级写入边界

唯一允许写入的真实路径根是 Goal objective 中冻结的当前仓库物理路径；仓库内统一记为：

```text
${REPO_ROOT}
```

以下规则高于各阶段便利性：

1. 只允许在上述根及其真实子路径创建、修改或删除文件；禁止用软链接、`..`、环境变量、工具
   默认缓存或系统临时目录逃逸。
2. 原版 `${XDEBUG_ORIGINAL_ROOT}`、`${WELLEN_HOME}`、`${VERILATOR_HOME}`、用户 HOME、系统
   临时目录和其他仓库全部只读。不得在其中 build、format、生成波形、刷新 fixture、commit、
   stash、clean、reset 或安装文件。
3. 外部 Git 查询统一设置 `GIT_OPTIONAL_LOCKS=0`，防止只读审计触发可选 index refresh。
4. 可能写文件的测试、构建和工具运行必须把 `HOME`、`TMPDIR`、`XDG_CACHE_HOME`、pytest
   `--basetemp`、session、日志和产物目录显式落到当前仓库的 `build/goal-runtime/` 或另一个已
   核对的仓库内目录。无法证明写入边界时不得启动命令。
5. 原版 NPI、VIP、VCS 和其他 EDA 动作必须在沙箱外直接运行；“沙箱外”不扩大可写范围，
   所有可配置输出仍必须进入当前仓库。
6. 运行 EDA 前先按 `xverif` skill 读取完整 73 Action guide，再查询所用 Action schema；不得
   按记忆猜参数。失败后禁止更换 simulator、backend、transport、格式、fixture、数据或测试
   层级。
7. 每批开始和结束都复核三处外部仓库 HEAD、status 和 dirty/untracked 内容哈希。发生变化时
   本批立即失败；只在逐资产证明冻结内容未变后，才能显式更新“最新审计态”。
8. 若完成任务必须修改外部仓库，停止实现并向用户申请新授权。不得把依赖源码直接修改伪装成
   当前仓库修复；允许的依赖表达只有当前仓库内的 patch、lock 和构建合同。

## 三、权威基线

### 3.1 原版行为与资产是两条独立基线

原版行为金标准来自 [`dependencies.lock.json`](../dependencies.lock.json)：

| 项 | 冻结值 |
| --- | --- |
| runtime revision | `8eecf71271cc523d93bf03f6b9f9b6fa04ed3ee8` |
| schema revision | `c45099040abf3dbe194d3ba27c207d7637b39ba9f9d662fad3d9d50dda99fb2c` |
| baseline manifest SHA-256 | `5d2882cdc06bbdc8bc03f6d9bb888bc9de316d8936cbeb67188f26da53f0` |
| public Action | 严格 73 项 |

原版测试资产基线来自 Goal 建立时的只读工作树：

| 项 | Goal-start 值 |
| --- | --- |
| xverif HEAD | `478a944d2b1efefff55afb6f4130944ed19fee1b` |
| branch | `master` |
| dirty/untracked 条目 | 33 |
| 资产内容 | manifest 中逐文件 SHA-256 |

行为 revision 与资产 HEAD 不相同，不能静默选其中一方。矩阵必须同时保留：

- 冻结 runtime/schema 对公开请求和响应的语义裁决；
- Goal-start 测试资产对 RTL 场景、刺激、生成方式和 oracle 的定义；
- 两者不一致时登记为独立差异，不能靠更新 baseline 消失。

### 3.2 机器冻结文件

[`rtl-wave-assets.manifest.json`](../compat/xdebug-v1/rtl-wave-assets.manifest.json) 保存：

- Goal-start 外部 HEAD、dirty/untracked 状态和内容哈希；
- 最新一次经审计的外部只读状态；
- 原版 23 个 xdebug fixture 的 source、capability、inputs、builder、outputs、probe 和 catalog
  引用行；
- 原版 fixture source、波形相关测试消费者、registry 和共享生成器；
- 当前 tracked/pending fixture、测试消费者、生成器和构建合同；
- 每个资产的相对路径、角色、fixture 归属、类型、大小和 SHA-256；
- 原版直接 output 以及 probe glob 声明的 FSDB 波形输出。

生成器为 [`freeze_rtl_wave_assets.py`](../tools/freeze_rtl_wave_assets.py)。它只能写入解析后仍位于
当前仓库的输出路径；默认没有环境变量时失败，不猜原版或依赖路径。已有 manifest 时，冻结的
原版资产集合或任一内容哈希变化都会失败关闭。外部 HEAD/status 变化还需人工只读复核，并用
`--accept-audited-external-drift` 明确确认；该选项只更新最新审计态，不替换 Goal-start 资产
基线。

## 四、初始资产事实

### 4.1 汇总

| 侧 | RTL/HDL | 波形 | fixture | 相关资产候选 | 测试消费者 |
| --- | ---: | --- | ---: | ---: | ---: |
| 原版 | 103 | 源树不提交 FSDB；registry 声明 25 个直接或 glob 输出 | 23 | 358 | 259 |
| 当前 | 11 | 12 个 FST、1 个 VCD | 11 个目录加 root FST | 180 | 45 |

这里的 259 个原版 consumer 是机器发现候选，不表示 259 个独立语义。P1 要把多个 consumer 对
同一 fixture/Action 的重复检查归并成场景，同时保留每个文件的反向引用。

### 4.2 原版 103 个 HDL 的分组

| 分组 | HDL 数 | 内容 |
| --- | ---: | --- |
| `testdata/combined` | 5 | active driver、active semantics、zero evidence、interface root、X propagation |
| `testdata/design` | 8 | hierarchy types、P3 semantics、UART |
| `testdata/waveform` | 27 | complex wave、stream、APB/AXI、XAMBA、SVA、XIF event |
| active trace `p0_composability` | 6 | assign/flop/module/generate/mux/procedural 边界 |
| active trace `composite` | 21 | 20 case 与共享 DUT |
| active trace `phase4` | 21 | 20 case 与共享 DUT |
| active trace `phase5` | 2 | DUT 与 TB |
| active trace `timing` | 13 | 12 case 与共享 timing DUT |

### 4.3 当前 11 个 RTL fixture

`apb`、`axi`、`case`、`counter`、`interface_modport`、`matches`、`output_mixed`、`phase5`、
`ref_port`、`stream`、`xprop`。根目录另有一份复用 counter 波形。当前数量明显更少，但多个
当前 fixture 已聚合许多原版语义，所以必须在 P1 做 feature-level 映射，不能按文件一对一移植。

## 五、差分单位与状态定义

### 5.1 最小差分单位

一个场景至少包含：

```text
fixture + RTL construct + stimulus/time + observable signal/path
+ public Action/request + expected response/evidence
```

只有 RTL 文本相似而没有相同刺激和公开观察点，不算覆盖；只有 Action 名相同而波形缺少对应
边界值，也不算覆盖。

### 5.2 状态

| 状态 | 判定要求 | 最终是否允许 |
| --- | --- | --- |
| `exact` | RTL、刺激、观察点和归一化结果都相同 | 允许 |
| `semantic-equivalent` | 实现/层级名称不同，但完整可观察语义相同 | 允许，必须有双侧证据 |
| `partial` | 只覆盖原版场景的一部分维度或边界 | 不允许留到完成 |
| `missing` | 当前没有可达到的等价测试 | 不允许留到完成 |
| `proven-unobservable` | 冻结 73 Action/schema 证明该差异不可观察，并有静态合同测试 | 允许，但不能作为免测口袋 |

禁止 `N/A`、`skip`、`xfail`、放宽断言、扩大易变字段白名单或只比较 success 状态来关闭差异。

## 六、阶段计划与提交边界

### P0：任务书、只读快照和资产发现门禁

交付：

- 本任务书及同文件进度记录；
- 冻结 manifest；
- manifest 生成器；
- registry/probe 解析、Git porcelain、输出边界、资产唯一性、哈希、初始计数、动态 FSDB glob
  的单元测试；
- live `--check` 重复性证据和外部零写入证据。

门禁：

- 原版 103 个 HDL 全部有 fixture 归属；
- 原版和当前资产路径唯一、相对、存在且有 SHA-256；
- active trace 五组 `cases/*/out/waves.fsdb` 不能因 registry output 为目录而漏掉；
- Goal-start 与最新外部审计态分离；
- 重复 `--check` 不修改 manifest；
- 外部仓库无本 agent 写入。

计划提交：`文档：冻结原版 RTL 与波形测试资产基线`

### P1：RTL 场景语义矩阵

新增机器可校验矩阵，每行至少包含：

- `scenario_id`、原版 fixture/path/line、当前 fixture/path/line；
- construct：hierarchy、port/modport、width/signedness、four-state、alias、generate/array、
  blocking/NBA、event control、case/casez/casex/matches、force/release、multi-driver、X-prop、
  protocol/reset/clock/seed/end-time；
- stimulus 和精确观察时间/窗口；
- 相关 Action、request example、response 字段和完整性字段；
- 状态、证据路径、缺口 owner、预定 P3 批次。

门禁：103 个 HDL、23 个 fixture、25 个声明波形输出和所有发现 consumer 均能反查到场景；
`partial/missing` 全部进入 P3 队列；零无证据排除、零悬空映射。

计划提交：`测试：建立原版 RTL 场景语义映射矩阵`

### P2：FSDB/FST 公开 Action 波形差分设施

使用等价公开请求比较语义，不读取或比较 FSDB/FST 二进制：

- scope/signal/path/type/width/timescale/start/end；
- 全量 transition、同时间 delta 顺序、0/1/X/Z、real/string/event；
- clock/reset 点读和 sampled window；
- stream/APB/AXI transaction、stall、outstanding、latency；
- driver/load、active driver/chain、X origin 的 file:line 和终止状态；
- `scan_complete`、`analysis_complete`、`response_truncated` 及 Action 专用完整性字段。

原版 NPI/VIP/VCS 只在沙箱外运行，所有输出进入仓库内隔离目录；提交物只保留小型输入、请求、
去敏规范 JSON/golden 和脚本，不保留 FSDB、daidir、二进制或日志缓存。比较器必须有能抓住缺
信号、类型/宽度、X/Z、time/delta、值和协议差异的负例。

计划提交：`测试：建立 FSDB 与 FST 公开语义差分门禁`

### P3-A：基础波形与类型补测

范围：complex wave、counter、hierarchy、boundary time、delta、four-state、real/string/event、
list/event/window/statistics。每个缺口先提交或记录能稳定失败的断言，再补 fixture/test/实现。

计划提交族：`测试：补齐基础波形与类型差分场景`。

### P3-B：design 与 combined 补测

范围：hierarchy types、P3、UART、active driver/semantics/zero evidence、interface root、X-prop。
任何静态事实缺口优先在当前 consumer 或已有 DesignDB 数据上解决；只有证明确需依赖能力时，
才修改当前仓库 patch/lock，绝不直接改 sibling。

计划提交族：`测试：补齐设计与活动驱动差分场景`。

### P3-C：active trace 补测

逐项覆盖 p0、composite、phase4、timing、phase5 的 assign/flop/module/interface/generate/mux/
procedural、多 driver、NBA、force、反馈和 max-depth/max-nodes/truncation。共享 DUT 不得让单个
代表用例替代其余 case 的控制/时间差异。

计划提交族：`测试：补齐 active trace 原版场景矩阵`。

### P3-D：stream/APB/AXI 补测

覆盖 backpressure、连续传输、空/多结果、multi-ID、out-of-order、outstanding、stall、latency、
limits/truncation/completeness 以及固定/随机 seed。协议配置先按 schema load/validate/describe；
不因 VIP 不可用改用另一个 fixture 或简化协议。

计划提交族：`测试：补齐流与总线协议差分场景`。

### P3-E：XIF、SVA/NPI 与新发现项

公开 Action 可观察的行为必须有 FST 回归；不可观察的 SVA/NPI 专属信息必须逐字段引用冻结
schema 证明并建立静态门禁。P1/P2 新发现项只能加入本批或修订计划，不能静默忽略。

计划提交族：`测试：裁定并补齐其余原版波形场景`。

### P4：全量门禁和最终报告

至少执行：

- manifest live check、矩阵完整性和 differential 正/负例；
- 所有新增 focused tests；
- 完整 pytest、CTest；
- 按改动风险执行 ASan/UBSan、session/transport、73 Action 兼容审计；
- 涉及原版/VIP/NPI 的正式 host gate；
- 外部三仓 goal-start、批前、批后状态审计；
- 当前仓库路径、专有产物、fixture cache 和 Git 提交边界审计。

最终要求：原版相关资产覆盖 100%，`partial=0`、`missing=0`、`unclassified=0`、无证据排除
`=0`；所有 in-scope 差分与测试通过；无 fallback；外部零写入；只提交合法源文件、小型确定性
FST 和去敏证据。最终报告逐条链接验收证据后，才允许把 Goal 标记 complete。

## 七、fixture 与缓存规则

1. 未修改 fixture 输入时不重建已有缓存。
2. 修改/新增 fixture 时只重建受影响项，记录完整命令、工具版本、seed、输入哈希和输出哈希。
3. 当前生产测试只打开原始 `.fst`。VCD 只能作为已有可读 fixture 源描述，不能作为 Action 输入
   或 FST 失败后的替代。
4. 小型 FST 必须由仓库脚本确定性重建；FSDB、daidir、仿真 ELF、obj_dir 临时输出、专有头库和
   未去敏日志不得提交。
5. 重生成发生内容变化时先判断是计划内语义变化还是工具漂移；不能直接接受全量 golden 更新。

## 八、每批统一 red→green 门禁

每个补测批次按以下顺序：

1. 保存原版 RTL:line、fixture/seed/time、公开请求和完整响应。
2. 在当前仓库增加最小失败断言，确认失败属于缺失语义而非路径、缓存、工具或环境错误。
3. 只做关闭该差异所需的最小实现/fixture 改动。
4. 运行 focused test，证明红灯转绿；再运行相邻回归和阶段门禁。
5. `--check` manifest，确认未丢资产、未换 fixture、外部冻结内容未漂移。
6. 核对 `git status --short` 与 staged 白名单，只提交本批文件。
7. 使用详细中文标题和正文，写清动机、范围、红灯、实现和测试结果。

生产修复与大规模测试资产尽量分开提交。不得 `git add .`，不得把其它进程文件带入 commit。
不创建 PR、不推送远端，除非用户另行明确要求。

## 九、停止条件

出现以下任一情况时停止当前路径，不做 fallback：

- 冻结原版资产集合或哈希变化，且不能从 Goal-start Git object/已冻结证据恢复；
- EDA 不能把全部写入重定向到当前仓库；
- 需要直接修改 Wellen/Verilator/xverif；
- 原版与冻结 runtime/schema 发生不可裁决冲突；
- fixture、backend、transport 或测试层级必须改变才能变绿；
- 需要新增易变字段白名单或放宽完整性断言。

前三次遇到同一阻塞只记录并继续所有仍可进行的只读/静态工作；达到 Goal blocked 规则阈值且
确实无其他进展时，才标记 blocked。

## 十、进度记录

### 2026-08-30：Goal 与 P0 启动

- Goal 已建立，未设置 token budget。
- 当前仓库起点干净，HEAD 为 `302d0c2b40eff1547d7479c6838b6866c8ab7c7f`。
- 读取 `xverif` skill、完整 73 Action guide 和 xdebug capability；P0 未运行 EDA 或 Action。
- 初盘确认原版 103 个 HDL，当前 11 个 RTL、12 个 FST、1 个 VCD。

### 2026-08-30：P0 manifest 初版

- 新增 `tools/freeze_rtl_wave_assets.py`，以 `GIT_OPTIONAL_LOCKS=0` 读取外部 Git，并对输出
  realpath 做当前仓库边界检查。
- 首次冻结得到原版 23 fixtures、358 个资产、103 HDL、259 consumer、25 个直接/glob 波形
  输出；当前 180 个资产、11 RTL、12 FST、1 VCD、45 consumer。
- active trace registry 的 output 只声明 `cases/` 目录，初版进一步解析 probe `--fsdb-glob`，
  把五组 `cases/*/out/waves.fsdb` 纳入波形合同。
- focused test 首轮发现 registry/shared generator 没有 fixture 归属；保持红灯后修复为
  `original.registry`/`original.cross_fixture`。随后补充软链接逃逸负例，当前 7 项测试通过。

### 2026-08-30：外部 HEAD 漂移门禁实测

- 在本 agent 未执行任何外部写命令期间，原版 HEAD 从 `478a944d...` 变为 `5110099482b9433a...`
  （`feat(xdebug): 改用 XAMBA 产品 filelist 构建真实协议 fixture`）。生成器立即拒绝刷新。
- 只读 diff 显示 13 个已发现的 XAMBA/registry/test 资产由 dirty/untracked 进入新 commit；逐个
  比较冻结 manifest 与 live 文件，358 个原版资产内容漂移为 0。
- Goal-start dirty 文件中只有范围外计划文档内容哈希变化；它仍保留在 Goal-start 外部快照中，
  不替换资产基线。
- 无确认参数的 `--write` 按预期失败；完成上述审计后显式使用
  `--accept-audited-external-drift`，仅把最新审计态更新为 xverif HEAD `5110099...`、18 个
  dirty/untracked 条目。Wellen `afab0abd...` 和 Verilator `bf01d667...` 仍干净且 HEAD 未变。

### P0 当前状态

| 项 | 状态 | 证据 |
| --- | --- | --- |
| 任务书/同文件进度 | 已完成初版 | 本文件 |
| Goal-start 外部快照 | 已冻结 | manifest goal-start snapshot |
| fixture/asset manifest | 已生成 | 103/11/12/1，零 missing、零 unassigned HDL |
| 动态 FSDB glob | 已覆盖 | active trace 五组 probe contract |
| 单元测试 | 通过 | 7/7 |
| 重复 live check | 已通过 | 连续两次通过，manifest SHA-256 `8700147784993e4e4e38ca6f199398691359cf9fb3721e9661d53350bfc24fa6` |
| 相邻静态门禁 | 已通过 | manifest/compat/path 11/11；compat baseline OK；local path audit OK |
| 外部批后零写入复核 | 已通过 | live check 通过；三仓 HEAD/status/hash 与最新审计态一致 |
| P0 中文详细 commit | 已完成 | `994bc5d 文档：冻结原版 RTL 与波形测试资产基线` |

### 2026-08-30：P1 RTL 场景语义矩阵

- 新增 `tools/build_rtl_wave_semantic_matrix.py`，所有原版输入先对照 P0 manifest 做逐文件
  SHA-256 校验；输出路径经过 realpath 边界检查，只能写入当前仓库。
- 解析冻结 `cases.v1.yaml` 得到 68 个 active trace catalog case，并另外登记 README 声明、
  但目录仅有 `.gitignore` 且 YAML 无条目的 `p0_4_interface_modport` declared-only orphan。
- 生成 `tests/coverage/rtl_wave_semantic_matrix.json`：88 个场景完整反向覆盖 23 fixture、103 HDL、
  25 个声明波形输出和 259 consumer；每个原版 HDL 保存 SHA-256、construct 分类及精确行号。
- 73 个公开 Action 全部链接冻结 request example、response schema 和完整性字段。当前候选测试只
  标记“相关能力”，在 P2 用同一请求完成双波形比较前不得升级为等价。
- Phase5 的 DUT 和 S1～S13 刺激在当前 fixture 中近似复现，但现有
  `test_trace_active_driver_chain_matches_original_phase5_public_semantics` 把多个 scene 统一断言为
  `ambiguous`，与原版逐 scene oracle 不同，不能再由测试名宣称完整匹配。
- 原版自身还存在两个冻结冲突：S6 的 YAML 为 `ambiguous`、报告为 `control_only`；S8 的 YAML
  为 `primary_input`、报告为 `control_only`。矩阵同时保留两值，排入 P2 runtime 实测裁决。
- P1 初始状态为 `partial=86`、`missing=2`、`unclassified=0`、`unqueued_gap=0`；88 个未关闭项
  已全部分配到 P3-A～E，当前阶段没有以能力族代表测试静默关闭具体场景。
- 新增 6 项矩阵门禁，连同 P0 资产及 compat/path 相邻门禁共 21/21 通过；覆盖
  parser/construct、软链接写逃逸、全量反向索引、证据哈希与行号、73 Action 合同、Phase5
  冲突和 p0_4 孤儿；独立 compat baseline 与 local path audit 也通过。

### P1 当前状态

| 项 | 状态 | 证据 |
| --- | --- | --- |
| 68 个 active catalog case | 已映射 | matrix `active.*` |
| p0_4 declared-only orphan | 已显式登记 missing | `active.p0.declared_only_p0_4` |
| 103 HDL / 23 fixture / 25 wave output | 100% 可反查 | matrix `coverage` |
| 259 consumer | 100% 可反查 | matrix `coverage.original_consumers` |
| 73 Action request/schema/完整性字段 | 已链接 | matrix `action_contracts` |
| 缺口队列 | 已完成 | P3-A 1、P3-B 8、P3-C 70、P3-D 6、P3-E 3 |
| P1 focused/adjacent gate | 已通过 | 21/21；compat baseline OK；local path audit OK |
| P1 中文详细 commit | 已完成 | `bd602e3 测试：建立原版 RTL 场景语义映射矩阵` |

### 2026-08-30：P2 公开 Action 差分设施

- 新增 `tools/compare_public_action_responses.py`。比较器只读取公开请求/响应 JSON，不读取波形
  二进制；严格校验 plan、FSDB bundle 和 FST bundle 的 observation 集合与 Action 身份。
- 时间统一转换为整数 fs，四态字面量保留 width、signed、X 与 Z，数组顺序保持不变，因此同一
  时刻 delta 或协议 transaction 顺序变化不会被排序掩盖。typed string/event 的 payload 即使
  恰好写成 `1ns` 或 `4'hf`，也不会被误当作时间或逻辑值归一化。
- ignore/rewrite 必须逐 JSON Pointer 写明理由；rewrite 还有精确旧值前置条件。输出 realpath
  只能位于当前仓库，并以同目录临时文件原子替换。没有全局易变字段白名单。
- synthetic 正例覆盖 `signal.changes` 的 type/width/timescale/delta/四态/real/string/event、
  AXI ID/latency/outstanding/stall，以及 Phase5 active trace。负例覆盖缺信号、type/width、X/Z、
  delta 与顺序、typed payload、协议字段、trace termination、宽度完整性、scan/analysis/truncation、
  source-line rewrite 前置条件和输出软链接逃逸。
- P2 纯差分与去敏审计门禁 31/31 通过；CLI 正例报告 3 个 observation、0 difference，返回成功。

### 2026-08-30：P2 锁定原版 Phase5 实测

- 先通过已查询的 `session.open`、`trace.active_driver_chain`、`session.close` schema，再运行原版
  NPI。所有 HOME、TMP、cache、session、socket 和编译产物都进入当前仓库；外部 xverif、
  Wellen、Verilator 只读。
- 发现 Goal-start 可执行文件自报 `478a944d2b1e/6ace27b2...`，不是冻结 runtime。该版本的十个
  scene 结果可作旁证，不能作为金标准。
- 在当前仓库 ignored build 目录从冻结 Git object 构建 `8eecf71271cc/c4509904...`；二进制
  身份、schema 和 73/73 Action 门禁通过。复用既有 Phase5 cache，FSDB SHA-256 为
  `9fdc31f039e65a24a25cb2808f0d62c232e3c4d91ea90e0cf6252c9e1d2df7ba`，未重建 fixture。
- 冻结 runtime 的 S1～S6、S9 均为 `ambiguous/multiple_rhs_sources`；S7、S8、S10 均为
  `ambiguous/multiple_active_candidates`。十项都 `scan_complete=true`、
  `analysis_complete=true`、`response_truncated=false`。这与 Goal-start 可执行文件一致。
- 因此冻结 catalog 有 8 个 termination 漂移、旧报告有 9 个漂移；S6/S8 的 catalog/report
  内部冲突继续保留。runtime 只裁决当前行为，不反写或伪造历史资产 oracle。
- 当前 FST 十场景 termination/ambiguity 子集门禁 1/1 通过。第一次测试未进入业务断言，原因是
  隔离 HOME 生成的 UDS 路径超过 Linux `sockaddr_un` 容量；按仓库已有
  `XVERIF_TEST_TMPDIR` 合同改用当前仓库内短路径后，同一 binary/backend/fixture/test 原样通过，
  没有 fallback。
- S1 完整响应 spot check 仍发现可观察差异：原版公开宽度不完整诊断与 unsized hop value、
  RHS sample 顺序、statement kind/driver，以及等价 RTL 的 file:line/source_context。故十个 scene
  仍为 `partial`，全部留在 P3-C；不得因终止子集一致升级为 `semantic-equivalent`。
- 去敏 `phase5.runtime-audit.json` 只保留 revision、哈希、计数和相对证据，不含绝对路径、FSDB、
  daidir、二进制或日志。原版 session 正常关闭；查询前后外部 xverif porcelain 内容哈希相同。

### P2 当前状态

| 项 | 状态 | 证据 |
| --- | --- | --- |
| JSON 差分比较器 | 已完成 | 严格 plan/bundle、顺序保真、类型化归一化、结构化 diff |
| 合成正负门禁 | 已通过 | P2 与 Phase5 audit 合计 31/31 |
| 锁定 runtime 身份 | 已通过 | `8eecf71271cc/c4509904...`，Action 73/73 |
| Phase5 原版查询 | 已完成 | S1～S10，完整性全真、零裁剪，fixture cache 未重建 |
| 当前 Phase5 子集回归 | 已通过 | focused 1/1；termination/ambiguity 10/10 |
| 完整响应差异 | 未关闭 | Phase5 保持 partial，进入 P3-C |
| P0～P2 相邻静态门禁 | 已通过 | 44/44；compat baseline OK；local path audit OK |
| 外部零写入 | 已通过 | xverif 查询前后 status 内容哈希一致；Wellen/Verilator 未变 |
| P2 中文详细 commit | 待提交 | `测试：建立 FSDB 与 FST 公开语义差分门禁` |

### 下一步

1. 重复 manifest/matrix live check、compat/path 相邻门禁，完成 P2 独立中文详细 commit。
2. 进入 P3-A，按矩阵先补基础波形/type/delta/four-state 的逐观察点差分。
3. P3-C 单独关闭 Phase5 完整响应差异；不得用当前 termination 子集门禁替代 width、顺序、
   statement 和源码证据。
