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
| P2 中文详细 commit | 已完成 | `e2101b0 测试：建立 FSDB 与 FST 公开语义差分门禁` |

### 2026-08-30：P3-A ai_complex 基础波形与类型补测

- 新增 `current.ai_complex`：RTL 保留原版非 AXI 场景的可观察层级、刺激时间、宽度和四态值；
  `generate_ai_complex_fst.cpp` 直接写 FST，不经过 VCD、JSON、私有索引或 export 回灌。受锁定
  Verilator `fstcpp` 的向量 X/Z 写入限制仅以本仓库 fixture-local patch 表达，未修改 sibling。
- `tools/regenerate_ai_complex_fixture.sh` 把 HOME、TMP、编译输出和 patched header 全部限制在
  本仓库 `build/fixtures/ai_complex`。连续两次受影响 fixture 重建得到相同 983-byte FST，SHA-256
  均为 `71218e2a72fb43567cc4b1fe6070caeb1d9319568412cfa2d05adffc44fcbea9`；未重建任何未变 fixture
  cache，未提交 FSDB、daidir 或构建物。
- 先用锁定原版公开 runner 固化失败观察点，再在实现提交 `ddf9064` 中关闭六类语义差异：
  `value.at` 反向目标 edge/直接未知 signal、`event.find` reset load、`stream.query` before/after 与
  顶层 data alias、表达式共同宽度及无尺寸进制常量、`counter.statistics` 的 `@cursor` 窗口。
- 锁定 runtime `8eecf71271cc/c4509904...` 的 `run_complex_wave.py` 和
  `run_counter_statistics.py` 分别在原版 FSDB 与当前 FST 上运行，四个 suite 全部通过；原版
  `ai_complex` FSDB cache 只读复用，SHA-256 为
  `6a30388f464162d29bb538af92992c702667fe55c57a423095e27066d0636152`，没有重建或提交。
- 新增 7 项当前 fixture 回归和 3 项去敏 runtime-audit 门禁；本批 focused 31/31、实现相邻
  `waveform/list-event/counter/stream` 128/128 通过。audit 逐项锁定两个 runner、73 Action runtime、
  当前 fixture/test 哈希、双侧完整通过标记、零 fallback 及外部只读快照，不保存绝对外部路径。
- 修正 consumer-to-fixture 解析，使动态构造的源路径和导入的 `NONAXI_FSDB` 常量都归属
  `xdebug.ai_complex_wave`；原版仍为 358 资产、103 RTL、23 fixture、259 consumer 和 25 个声明
  波形输出。当前清单更新为 197 资产、12 RTL、13 FST、1 VCD 和 56 consumer，零 missing asset、
  零 unassigned original HDL。
- 语义矩阵只把 `fixture.ai_complex_wave` 从缺口队列升级为 `semantic-equivalent`，并绑定去敏
  runtime audit；状态从 `partial=86, missing=2` 变为 `semantic-equivalent=1, partial=85,
  missing=2`。P3-B～E 的 87 个缺口保持排队，没有用 P3-A 的能力覆盖替代逐场景证据。

### P3-A 当前状态

| 项 | 状态 | 证据 |
| --- | --- | --- |
| 直接四态 FST fixture | 已完成 | 两次确定性重建 SHA-256 `71218e2...` |
| 原版 complex/counter suite | 已通过 | 锁定 runner、只读复用原版 FSDB cache |
| 当前 complex/counter suite | 已通过 | 同 runner、同 mode、当前原始 FST |
| 当前仓库 focused gate | 已通过 | 31/31 |
| 实现相邻 gate | 已通过 | 128/128 |
| manifest/matrix 重现 | 已通过 | live check；85 partial、2 missing、1 semantic-equivalent |
| compat/path gate | 已通过 | compat baseline OK；local path audit OK |
| P3-A 实现 commit | 已完成 | `ddf9064 修复：对齐基础波形采样与表达式语义` |
| P3-A 证据 commit | 已完成 | `40a0677 测试：补齐 ai_complex 基础波形差分场景` |

### 2026-08-30：P3-B design 与 combined 补测

- P0 资产复核发现两个传递 consumer 合同缺口：`run_active_driver_fixture.py` 虽已冻结，但错误
  归入 cross-fixture；`run_semantics.sh` 没有进入清单。生成器现用显式映射分别绑定
  `active_driver/interface_port_root` 和 `design_p3/design_uart`。只允许这两项经审阅的传递
  consumer 补入或重分配，新增文件还必须与 Goal-start Git object `478a944...` 字节一致，原有
  冻结资产继续逐文件失败关闭。清单变为原版 359 资产/260 consumer，当前 215 资产、15 RTL、
  16 FST、1 VCD、59 consumer；103 个原版 HDL、23 fixture、25 个波形输出仍全部可反查。
- 使用锁定 runtime `8eecf71271cc/c4509904...` 和既有只读 fixture cache 运行原版门禁：
  active-driver/interface leaf runner 10/10、active-semantics 1/1、active-zero 16/16、X-prop 1/1、
  design semantics 1/1 全部通过，session 正常关闭。七个受用 cache 均未重建，工具身份为
  `X-2025.06`；FSDB、daidir、cache、日志和二进制均未复制或提交。
- 原版行为 revision 和 Goal-start 资产 HEAD 继续分开保存。active-driver、active-semantics、
  active-zero 和 `run_semantics.sh` 在两条基线上的 runner 哈希不同，审计同时记录 locked 与
  Goal-start SHA-256；当前侧使用移植或归一化后的完整公开断言，明确标记“没有直接复用同一
  原版 runner”，避免把等价证据夸大成同一可执行文件双侧运行。
- 新增 `current.active_driver`、`current.interface_port_root`、`current.active_zero_evidence`。
  三份 RTL 与原版逐字节相同，SHA-256 分别为 `03592650...`、`93793abd...`、`fcb281bb...`；
  由受锁定 Verilator/DesignDB 直接生成四态 FST，不经 VCD/JSON 转换。连续两次重建的 FST
  SHA-256 分别稳定为 `ff589ebe...`、`856a5d21...`、`ee351fdf...`。仓库内 Verilator patch 将
  FST header 日期固定为 1970，依赖 lock 升级为 `xdebug-design-db-v4-deterministic-fst`，未修改
  sibling Verilator。
- 实现补齐无 `top.` 前缀的原版公开路径兼容、递归活动驱动、force/pass-through/default、精确
  `active_time`、内部零证据与 primary input 区分、interface/modport alias、真实 module/interface
  分类、`scope.roots` 稳定性及专用 XOUT；显式 `top.` 请求仍保持原当前语义。新增 18 项原版
  oracle 测试全部通过。
- P3-B 八个场景中，`active_driver`、`active_semantics`、`active_zero_evidence`、
  `interface_port_root`、`trace_x_xprop`、`design_uart` 以双侧公开行为证据升级为
  `semantic-equivalent`。`design_hierarchy` 在锁定 runtime 中没有对应测试，且 Goal-start 测试
  所需 `interface_array/gen_scope/modport/mpport` kind 与六个扩展 data group 均被冻结
  `scope.list` schema 排除；`design_p3` 在 locked/Goal-start `run_semantics.sh` 中都只用于
  `session.open`，语义查询数为 0。两者以哈希锚定静态合同判为 `proven-unobservable`，没有作为
  泛化免测入口。
- 新增去敏 `p3b.runtime-audit.json`，锁定 runtime/schema/73 Action、原版 runner 与 cache、
  当前 fixture/test 哈希、八项裁决、零 fallback 和外部三仓前后快照。矩阵从
  `semantic-equivalent=1, partial=85, missing=2` 更新为 `semantic-equivalent=7,
  proven-unobservable=2, partial=77, missing=2`；P3-B 队列为空，剩余 P3-C/D/E 队列分别为
  70/6/3。
- CTest 首轮为 5/7：两个 session 脚本在进入业务断言前失败。根因不是 backend/fixture，而是
  脚本把调用方仓库内 `XVERIF_TEST_TMPDIR` 再包入长前缀临时目录并覆盖该变量，使 UDS
  `sun_path` 再次超长。修复后，显式提供仓库内临时根时只在该根下使用短前缀；未提供变量时
  默认行为不变。同一 binary、UDS transport、fixture 和测试重跑 7/7 通过，没有 fallback。
- 补跑 live `check_compat_baseline.py` 时按预期失败：当前外部工作树的 binary/schema 已不是
  Goal 冻结的 `8eecf71271cc/c4509904...`，工具逐项报告 runtime、engine、schema 和 actions
  drift。该红灯不以另一目录或 live 新版替代；P3-B 行为仍由已通过身份与 73/73 Action 门禁的
  冻结 runtime 裁决，P4 继续保留这项双基线冲突。独立 local path audit 使用正确参数重跑通过。

### P3-B 当前状态

| 项 | 状态 | 证据 |
| --- | --- | --- |
| 三个原版精确 RTL/FST 镜像 | 已完成 | RTL 哈希相同；连续两次 FST 重建相同 |
| 锁定原版动态门禁 | 已通过 | 10/10 + 1/1 + 16/16 + 1/1 + 1/1 |
| 当前 active fixture 门禁 | 已通过 | 18/18 |
| design 合同门禁 | 已通过 | 13/13 |
| combined/waveform/binary 相邻门禁 | 已通过 | 146/146 |
| P0/matrix/P3-B 静态门禁 | 已通过 | 27/27 |
| CMake build / CTest | 已通过 | build 完成；7/7 |
| manifest/matrix 重现 | 已通过 | 连续 live check；P3-B queue=0 |
| live compat baseline | 红灯保留 | live 原版已漂移；未替换冻结 runtime、未接受漂移 |
| local path audit | 已通过 | `check_no_local_paths.py --repo-root` |
| 外部零写入/零 fallback | 已通过 | xverif 18 个既有 dirty；Wellen/Verilator clean；前后快照一致 |
| P3-B 实现 commit | 已完成 | `b4b810e 修复：对齐活动驱动递归与接口层级语义` |
| P3-B 证据 commit | 已完成 | `56348d1 测试：补齐设计与活动驱动差分场景` |

### 2026-08-30：P3-C active trace P0 六场景补测

- 先以 `28fbdeb` 固定显式 `top.*` 在 HDL 顶层也名为 `top` 时被解析为 `top.top.*` 的红灯，
  再以 `e3c268a` 修正 binary/XDD/Wellen 与 combined 的公共路径投影。随后 `a523096` 逐字节
  镜像 P0 六份原版 RTL，保存六个原版 native NPI chain oracle，并把尚未对齐的六项语义断言
  作为稳定红灯提交；没有在修复后补写“曾经失败”的伪证据。
- 原版 oracle SHA-256 为 `78fd3616cd961b66d59afcd55eb682b71ddb5549db8e99c580f4f141552298f2`；
  runner SHA-256 为 `f7e80398cf4b1f95b29d33c45373ff08bdcfd9c56bb20195b4467b952090f237`，
  NPI 为 `X-2025.06-SP1`。原版 P0 cache 只读复用，`fixture_rebuilt=false`，未复制或提交
  FSDB/daidir。当前六个 fixture 只在红灯批次按选择性生成器连续构建两次；本次实现没有修改
  fixture 输入，因此没有重建缓存。
- 六个 `fixture.sha256` 的传递锁分别为：assign `2b616bff...`、flop `6e81e26d...`、module
  `f395cf7c...`、generate `1ece7c03...`、mux `247b9803...`、procedural-for `6ace9a5e...`。
  每个锁精确覆盖该 case 的字节相同 RTL、公共 dump probe、原始 FST、binary-v1 XDD 与 manifest；
  14 项 P0 门禁逐文件重算全部哈希，不靠目录存在性判定。
- 锁定原版结果逐项为：`p0_0` ambiguous/3 hops，`p0_1` primary_input/4 hops，`p0_2`
  control_only/1 hop，`p0_3` control_only/1 hop，`p0_5` ambiguous/1 hop，`p0_6`
  primary_input/1 hop；六项都只有一个 native temporal boundary、`truncated=false`、limitations
  为空。当前请求逐项核对 signal/time、hop 顺序、源码行、active time、值/knownness、候选切换、
  termination、计数及 scan/analysis/response-truncation 完整性，没有用共享 DUT 代表其余 case。
- `2b65e5c` 用 DesignDB/FST 通用事实关闭红灯：区分重复 HDL top 与无前缀兼容投影，解码
  Verilator generate selector，恢复 module/generate 实例本地端口，按同一源语句合并 data/control
  依赖，并以查询时刻前后的真实 transition 选择唯一因果源或判定 control_only/ambiguous；同时
  补齐 statement-only 输入与 procedural-loop 边界。生产代码没有硬编码 P0 case、fixture 路径或
  oracle 值。
- 原版 native collector 与冻结公开 v1 schema 有两项不可直接编码的边界，原始 oracle 保持不改：
  `p0_6` 的 `file="", line=0` 在公开 hop 中投影为 `<unknown>:1` 加空 source context；`p0_2/p0_3`
  的 control_only branch candidates 不能放入仅允许 ambiguous termination 的
  `ambiguity_evidence`，因此维持 control_only，并用同一原始 FST 上的 `value.at` 分别核对每个候选
  前后值。另有两个 collector 表示差异被显式冻结：generate packed bit 的 native
  `value_known=true` 但 value 为空，当前只比较 knownness 并保留实际已知 bit；native
  `temporal_boundary` 与当前 root relation/时间关系逐 hop 对照。上述项目均有静态 schema 断言，
  不是易变字段白名单或宽松 success-only 比较。
- 构建后实现聚焦回归 129/129 通过：P0 14、combined 85、original-active 18、binary DesignDB
  production E2E 2、binary backend 10；P3-B runtime-audit/producer 相邻回归 12/12 通过。
  `runtime-schema-validator`、`xdd-design-backend`、`wellen-fst-backend` CTest 3/3 通过，
  `git diff --check` 通过。
- manifest/矩阵/P0 联合门禁 33/33 通过；manifest 与 matrix 都经过 write→check 稳定点复现。
  manifest 更新为原版 359 assets/103 RTL/23 fixture/260 consumer/25 outputs，当前 251 assets/
  22 RTL/22 FST/1 VCD/61 consumer，零 missing asset、零未归属原版 HDL；SHA-256 为
  `210753cab44e8b756e0c29590181bbcbeb195a5987db21c2cfaff11d2769c6cb`。
- 六项 P0 从 partial 升级为 semantic-equivalent，矩阵变为 `semantic-equivalent=13,
  proven-unobservable=2, partial=71, missing=2`；P3-C 队列从 70 降为 64。原版只声明但没有
  RTL/catalog oracle 的 `p0_4_interface_modport` 仍为 missing，`fixture.active_trace_runner` 也未由
  六个 case 顺带关闭。矩阵 SHA-256 为
  `6cdea4215affc55777bc33a4ccfb4303680d7ae05139c9fca647c25419d186a4`。
- 外部权威审计用 manifest 的完整 porcelain+内容口径复核：xverif HEAD `5110099482b...` 的
  18 个既有 dirty/untracked 条目逐路径、状态、大小、SHA-256 完全相同，359 个原版资产零增删、
  零哈希漂移；Wellen `afab0abd...` 与 Verilator `bf01d667...` 仍 clean。一次只含 tracked 项的
  `git status -uno` 原始哈希不能与含 untracked 的冻结口径直接比较，随后结构化审计已证明并非
  外部变化。本批没有越过唯一可写根，也没有 fallback。

### P3-C P0 当前状态

| 项 | 状态 | 证据 |
| --- | --- | --- |
| 六份原版 RTL/当前镜像 | 已完成 | 逐字节相同；六个 fixture 传递哈希锁 |
| 锁定原版 native oracle | 已完成 | 6/6；只读 cache；oracle `78fd3616...` |
| P0 精确差分 | 已通过 | 14/14；含 limits 与两项 schema 表达边界 |
| 实现/相邻回归 | 已通过 | 129/129 + 12/12；CTest 3/3 |
| manifest/matrix 门禁 | 已通过 | 33/33；连续 write/check 稳定 |
| P0 矩阵关闭 | 已完成 | 6 semantic-equivalent；剩余 P3-C queue=64 |
| p0_4 declared-only orphan | 未关闭 | 保持 missing，禁止用 P0 六项代表 |
| 外部零写入/零 fallback | 已通过 | 完整 snapshot 相同；原版资产漂移 0 |
| 顶层路径 red/green commit | 已完成 | `28fbdeb` / `e3c268a` |
| P0 六场景 red/green commit | 已完成 | `a523096` / `2b65e5c` |

### 2026-08-30：P3-C Phase4 二十场景补测

- 红灯提交 `1a3d152` 逐项镜像 `case_01`～`case_20`、`phase4_dut.sv` 和共享
  `composite/chain_dut.sv`，共 22 份 RTL；全部与原版逐字节相同。每个 case 都有独立原始 FST、
  binary-v1 XDD、manifest 和覆盖 7 个传递输入的 `fixture.sha256`，没有用一个共享波形代表二十种
  参数/刺激组合。选择性生成器以 `--group phase4 --jobs 4 --repeat 2` 连续构建两次，20 组
  FST/XDD 全部确定；本次 green 没有修改 fixture 输入，因此没有重建缓存。
- 原版只读 cache 指纹为
  `6a9d0fea1e9c68057e2ef12906dfd23f73a8a14622fc09b3be9798ee859b09ae`，复用实例后缀
  `prepare-m3d4_3z7`；native runner SHA-256 为
  `f7e80398cf4b1f95b29d33c45373ff08bdcfd9c56bb20195b4467b952090f237`，NPI 为
  `X-2025.06-SP1`。oracle
  `tests/data/rtl_wave_differential/p3c-phase4.original-oracle.json` 的 SHA-256 为
  `08e729d6370a48fde04611acb479c66c2458e5e7f193fd7747268a0d56b69368`；20 行均
  `fixture_rebuilt=false`、`fallback_used=false`、`truncated=false`、limitations 为空。
- 锁定原版结果为 10 个 `primary_input`、10 个 `ambiguous`；hop 分布为
  `10:1, 11:11, 12:3, 16:4, 17:1`。每行恰有两个 temporal boundary；十个 generate 场景各
  锁定 `in[0]`～`in[7]` 八个逐 bit 候选及 `4/8` 同时切换，五个 interface 场景锁定
  `file="", line=0` 的 sink modport endpoint。所有 hop 值均已知且非空。
- 红灯门禁共 42 项：22 项 oracle/RTL/fixture/边界锁通过，20 项精确语义测试失败；二十项都先在
  `termination` 处显示当前 `control_only`，并且只有一个时序边界。失败证据在修复前独立提交，
  没有 `xfail`、skip、放宽断言或修复后补写“曾经失败”。
- 实现提交 `829b0ed` 只使用 DesignDB/FST 通用事实：抑制已在子 scope 内 RHS 的输出端口反射；
  module output 直接恢复实例 input；sink modport 保留无源码 endpoint；仅在重复 driver 数、目标宽度
  和端口宽度同时相等时恢复 generate-for 静态 bit selector；仅解析常量递增、单位步长的受限
  procedural-for 域，其余继续 fail closed；NBA 只保留数据 RHS，并在 duplicate-top 投影中禁止
  同值重写越过最后真实 transition。生产代码没有 case 名、fixture 路径或 oracle 值特判。
- 完整原版链最长 17 hops，而冻结公开 request schema 的 `max_depth` 默认值为 8。产品默认值保持
  不变；精确差分显式请求从锁定 oracle 推导的 `max_depth=16`，并由独立 limits 用例继续验证
  `max_depth=1`、`max_nodes=1` 都返回可见 analysis boundary。这是公开合同内的显式请求，不是
  backend、数据、层级或工具 fallback。
- Phase4 正式门禁 42/42 通过；Phase4+P0 联合门禁 56/56 通过。实现聚焦回归 129/129 通过：
  P0 14、combined 85、original-active 18、binary DesignDB production E2E 2、binary backend 10。
  首轮 combined 抓到 4 个非 duplicate-top alias/NBA 时间回归，收窄条件后定向 46/46 和最终
  129/129 均通过。`runtime-schema-validator`、`xdd-design-backend`、`wellen-fst-backend`
  CTest 3/3 通过，完整 CMake build 通过。
- P3-B/producer 相邻门禁首轮 11/12：唯一失败是 P3-B 测试重复硬编码其阶段结束时的全局矩阵
  `7/77` 计数，已与先前 P0 的合法 `13/71` 升级冲突。精确全局计数仍由 matrix 专属门禁锁定；
  P3-B 测试改为验证自身逐行证据、状态全集、总数守恒和 P3-B 零队列，从而允许后续批次合法关闭
  gap。manifest/matrix/P3-B/producer 联合门禁随后 33/33 通过，不是删除失败断言。
- manifest 更新为原版 359 assets/103 RTL/23 fixture/260 consumer/25 outputs，当前 355 assets/
  44 RTL/42 FST/1 VCD/63 consumer；零 missing asset、零未归属原版 HDL。manifest SHA-256 为
  `0acafb12f4925c2dc32c5b221cc38ae6eaff23786869aac920764cc3c27ce280`；write→matrix
  write→manifest write 后，manifest 与 matrix `--check` 连续通过。
- 二十项 Phase4 从 partial 升级为 semantic-equivalent，矩阵变为 `semantic-equivalent=33,
  proven-unobservable=2, partial=51, missing=2`；P3-C 队列从 64 降为 44。矩阵 SHA-256 为
  `7f35032d2e54ded260bbd68eaac8b72359ad02cfbd04c64910eb91f207590a64`，每行只保留本 case
  的三份精确 RTL 和一份 FST 作为当前证据，且公开请求明确记录 `limits.max_depth=16`。
- Phase4 前后外部审计快照 SHA-256 均为
  `bf6fb878395c0dee2167f415a059e7559542ee9c0dc0dc1efb55a8b562ed1b86`：xverif 保持
  HEAD `5110099482b...` 和 18 个既有 dirty/untracked 条目逐路径/状态/大小/内容不变；Wellen
  `afab0abd...`、Verilator `bf01d667...` 均 clean。所有 HOME/TMP/cache/session/build 输出均在
  当前仓库；没有外部写入、fixture fallback、PR 或 push。

### P3-C Phase4 当前状态

| 项 | 状态 | 证据 |
| --- | --- | --- |
| 二十份 case RTL/两份共享 RTL | 已完成 | 22 份逐字节相同；20 个 fixture 传递哈希锁 |
| 锁定原版 native oracle | 已完成 | 20/20；只读 cache；oracle `08e729d6...` |
| Phase4 精确差分 | 已通过 | 42/42；完整链、两次时序边界、generate/interface 证据 |
| 实现/相邻回归 | 已通过 | 129/129；P3-B/producer 联合门禁已转绿 |
| CMake build / CTest | 已通过 | build 完成；关键 CTest 3/3 |
| manifest/matrix 门禁 | 已通过 | 联合 33/33；连续 write/check 稳定 |
| Phase4 矩阵关闭 | 已完成 | 20 semantic-equivalent；剩余 P3-C queue=44 |
| 外部零写入/零 fallback | 已通过 | 前后 audit snapshot `bf6fb878...` 相同 |
| Phase4 red/green commit | 已完成 | `1a3d152` / `829b0ed`；证据为当前提交 |

### 下一步

1. P3-C 保持进行中，下一批逐项处理 composite 20、timing 12、phase5 10，以及
   `fixture.active_trace_runner` 和 p0_4 declared-only orphan，共 44 项；共享 DUT 不能替代每个
   case 的控制、时间和完整响应证据。
2. Phase5 必须单独关闭完整响应差异；不得用当前 termination 子集门禁替代 width、顺序、
   statement 和源码证据。p0_4 若无法建立原版动态 oracle，必须按冻结资产/schema 给出受限的
   proven-unobservable 证明，不能编造原版 fixture。
