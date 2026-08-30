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

### 2026-08-30：P3-C composite 二十场景补测

- 测试提交 `ad19d52` 逐项镜像 `case_01`～`case_20` 的测试顶层；已有共享
  `composite/chain_dut.sv` 继续复用并参与每个 case 的哈希锁。21 份 RTL 均与原版逐字节相同。
  每个 case 提交独立原始 FST、binary-v1 XDD、manifest 和覆盖 case RTL、共享 RTL、dump probe、
  FST、XDD、manifest 六个传递输入的 `fixture.sha256`，共 120 条逐文件锁；没有用共享 DUT 或
  单个波形代表其余参数组合。
- 选择性生成器以 `--group composite --jobs 4 --repeat 2` 在当前仓库连续构建两轮；20 组
  FST/XDD 的大小和 SHA-256 逐项相同。原版 composite cache 指纹为
  `93b0b714ac405932d426e9f03c88daaacad2d7c3a1595777d4c080f9a0c1d676`，只读复用实例后缀
  `prepare-fpjmfmeo`；native runner SHA-256 为
  `f7e80398cf4b1f95b29d33c45373ff08bdcfd9c56bb20195b4467b952090f237`，NPI 为
  `X-2025.06-SP1`。没有重建或修改原版 fixture cache。
- 原版 oracle
  `tests/data/rtl_wave_differential/p3c-composite.original-oracle.json` 的 SHA-256 为
  `729f7a656f061d8bd5207217b987598ae81840dff3cd5bbaf4febc6b3cbcc5d7`。二十行均
  `fixture_rebuilt=false`、`fallback_used=false`、`truncated=false`、limitations 为空；20 个
  原版 FSDB 哈希各不相同，证明没有把 case 结果复用成同一份 oracle。
- 锁定原版结果为 10 个 `primary_input`、10 个 `ambiguous`，hop 分布为
  `6:11, 7:4, 11:3, 12:2`，共 151 hops；每行恰有一个 temporal boundary。十个 generate
  歧义场景各锁定 `in[0]`～`in[7]` 八个逐 bit data 候选，80 个候选均在 35ns 切换；五个
  interface 场景锁定 `file="", line=0` 的 source-less modport endpoint。
- 新增 42 项门禁：1 项 oracle/runtime/schema/写边界、20 项 RTL 与 fixture 哈希、20 项完整
  native chain 语义、1 项 limits。逐 case 对照 signal、hop 顺序、源码行、active time、四态值、
  temporal relation、termination、80 个候选及 scan/analysis/response-truncation；source-less
  endpoint 只按冻结公开 schema 投影为 `<unknown>:1` 和空 `source_context`，原始 oracle 不改写。
- composite 门禁第一次运行即 42/42 通过。真实缺口是矩阵此前仍为 partial 且缺少 20 份独立
  fixture/oracle/测试；Phase4 的通用实现已经覆盖相同共享 DUT 的较短复合链。这里以原 matrix
  partial 作为覆盖红灯并由 `ad19d52` 关闭，不制造生产失败、不回退旧实现，也不为满足形式要求
  添加 case 特判或无意义代码改动。因此本批没有单独生产实现 commit。
- 原版完整链最长 12 hops，公开 request schema 默认 `max_depth=8` 保持不变；精确差分显式使用
  从 oracle 推导的 `max_depth=11`。独立 limits 用例继续证明 `max_depth=1`、`max_nodes=1` 返回
  analysis boundary，而不是 response truncation 或 backend fallback。
- composite 正式门禁 42/42、P0+composite+Phase4 联合门禁 98/98、实现聚焦及相邻回归
  171/171 通过；后者包含 composite 42、P0 14、combined 85、original-active 18、binary
  DesignDB production E2E 2、binary backend 10。manifest/matrix/P3-B/producer 静态门禁
  35/35 通过，`runtime-schema-validator`、`xdd-design-backend`、`wellen-fst-backend` CTest
  3/3 通过，`git diff --check` 通过。
- manifest 更新为原版 359 assets/103 RTL/23 fixture/260 consumer/25 outputs，当前 457 assets/
  64 RTL/62 FST/1 VCD/65 consumer；零 missing asset、零未归属原版 HDL。manifest SHA-256 为
  `173ad7264a588e9dc394f397c8df8d89840412182f1eecbe7a75bc588d06743e`；矩阵 SHA-256 为
  `218d49b918b82e7468a9bd4a9cf85265d3f1a8002fb4683e049c997cad2ba0ff`。两者按
  manifest write→matrix write→manifest write→matrix check→manifest check 到达可重复稳定点。
- 二十项 composite 从 partial 升级为 semantic-equivalent，矩阵变为
  `semantic-equivalent=53, proven-unobservable=2, partial=31, missing=2`；P3-C 队列从 44
  降为 24。每行只保留本 case RTL、共享 DUT 和本 case FST 作为当前证据，公开请求明确记录
  `limits.max_depth=11`，剩余 timing、Phase5、runner 和 orphan 没有被 composite 顺带关闭。
- 批前批后 manifest 结构化外部审计一致：xverif 保持 HEAD `5110099482b...` 和 18 个既有
  dirty/untracked 条目逐路径、状态、大小、内容不变；Wellen `afab0abd...`、Verilator
  `bf01d667...` 均 clean。所有 HOME/TMP/cache/build/session 输出均在当前仓库；没有外部写入、
  fallback、PR 或 push。

### P3-C composite 当前状态

| 项 | 状态 | 证据 |
| --- | --- | --- |
| 二十份 case RTL/共享 RTL | 已完成 | 21 份逐字节相同；20 个 fixture、120 条传递哈希锁 |
| 锁定原版 native oracle | 已完成 | 20/20；只读 cache；oracle `729f7a65...` |
| composite 精确差分 | 已通过 | 42/42；完整链、单次时序边界、generate/interface 证据 |
| 实现/相邻回归 | 已通过 | 无新增生产修复；171/171 |
| CTest | 已通过 | 关键 CTest 3/3 |
| manifest/matrix 门禁 | 已通过 | 静态联合 35/35；连续 write/check 稳定 |
| composite 矩阵关闭 | 已完成 | 20 semantic-equivalent；剩余 P3-C queue=24 |
| 外部零写入/零 fallback | 已通过 | 完整外部 snapshot 与 Phase4 结束态一致 |
| composite 测试/证据 commit | 已完成 | `ad19d52` / 本批当前提交 |

### composite 批次结束时的下一步（已由 timing 批次推进）

1. P3-C 保持进行中，下一批逐项处理 timing 12、phase5 10，以及
   `fixture.active_trace_runner` 和 p0_4 declared-only orphan，共 24 项；共享 DUT 不能替代每个
   case 的控制、时间和完整响应证据。
2. Phase5 必须单独关闭完整响应差异；不得用当前 termination 子集门禁替代 width、顺序、
   statement 和源码证据。p0_4 若无法建立原版动态 oracle，必须按冻结资产/schema 给出受限的
   proven-unobservable 证明，不能编造原版 fixture。

### 2026-08-30：P3-C timing 十二场景补测

- 红灯提交 `ad6f5f3` 逐项镜像 `case_01`～`case_12` 和共享
  `timing_boundary_dut.sv`，13 份 RTL 均与原版逐字节相同。每个 case 提交独立原始 FST、
  binary-v1 XDD、manifest 和覆盖 case RTL、共享 RTL、dump probe、FST、XDD、manifest 六个
  传递输入的 `fixture.sha256`。选择性生成器连续构建两轮，12 组 FST/XDD 的大小和 SHA-256
  逐项相同；没有用单个波形或共享 DUT 结果代替其余参数/时间组合。
- 原版只读 cache 指纹为
  `1042c712bf8a59877d837e55ffd4e986a62eefc16aba01eaec2d6e459eca5fb5`，复用实例后缀
  `prepare-d9oyxhx2`；native runner SHA-256 为
  `f7e80398cf4b1f95b29d33c45373ff08bdcfd9c56bb20195b4467b952090f237`，NPI 为
  `X-2025.06-SP1`。oracle
  `tests/data/rtl_wave_differential/p3c-timing.original-oracle.json` 的 SHA-256 为
  `2a67dd929d5fdc5d2459a4d2579c30e2441b9282c64aeebd63f0be50ca8dd36c`；12 行均
  `fixture_rebuilt=false`、`fallback_used=false`、`truncated=false`、limitations 为空。
- 锁定原版 12 行均为 `temporal_boundary/1 hop`，driver 是共享 DUT 第 44 行的
  `cont_assign`，且 `temporal_boundary_stops=1`。活动时刻依次为
  `55ns, 25ns, 55ns×6, 45ns, 0, 55ns×2`；case 02 的值为 `A5`，case 10 保留 native
  `value_known=true` 但 value 空串，其余为 `5A`。case 10 不臆测原版空串，而用当前 FST 在
  原版活动时刻的 `value.at` 锁住投影后的实际已知值。
- 有效红灯共 26 项，结果为 15 pass/11 fail：oracle、13 份 RTL、12 组 fixture 哈希和 limits
  先通过；除 `NUM_PRE=0` 的 case 09 外，11 个动态 case 只在首 hop `active_time` 失败。原始
  Verilator FST 会把分级 unpacked-array 传播折叠到源 NBA 同槽，常见根变化为 15/45ns；VCS
  FSDB 在下一匹配敏感边沿观测为 25/55ns。case 09 没有前级数组传播，双方原始时刻同为 45ns。
  这项差异被明确记录为 simulator scheduling projection，没有宣称原始 FSDB/FST transition
  逐点相同。
- 实现提交 `31bdddf` 只使用通用 DesignDB/FST 事实：先确认重复 HDL top 兼容投影，沿连续
  driver 图识别同一 unpacked-array base 的传播负载，要求原始变化槽只有唯一 active NBA，并
  要求末端数组元素在同槽真实变化，再扫描下一匹配 sensitivity edge。所有结构门同时成立才把
  root 活动时刻投影到原版边界；不完整、歧义或 `max_nodes` 用尽即 fail closed。查询窗口在下一
  边沿前结束的 case 10 回退到前一 root transition 及其值，`NUM_PRE=0` 的 case 09 保持原始
  45ns。生产代码没有 case 名、fixture 路径、oracle 常量或工具后端特判。
- 原版 runner 的 `stop_on_temporal=true` 是私有采集开关，冻结公开 v1 request schema 明确
  `additionalProperties=false` 且没有该字段。当前不扩展 schema，而把原版单跳停止结果严格作为
  完整、非截断公开链的首 hop 前缀；矩阵逐行记录
  `native_stop_on_temporal_prefix` 和 `same_slot_nba_active_time_projection`，公开请求只保留
  signal/time，并显式使用 `max_depth=64,max_nodes=64`。limits 独立用例仍要求 1 节点/1 深度返回
  analysis boundary，不把限制当 response truncation 或 fallback。
- timing 正式门禁 26/26 通过；P0+composite+Phase4+timing+combined 聚焦回归 209/209 通过；
  manifest/matrix/P3-B/producer 静态门禁 37/37 通过；`runtime-schema-validator`、
  `xdd-design-backend`、`wellen-fst-backend` CTest 3/3 通过，`git diff --check` 通过。一次 bare
  pytest 被锁定解释器内自动加载的外部 xverif 编排插件拒绝，一次未设置短临时根导致所有动态项
  在业务断言前统一 UDS 启动失败；最终只禁用该无关 pytest 插件，并设置当前仓库内
  `XVERIF_TEST_TMPDIR=.tmp/socket` 后，用同一 binary、UDS、fixture、backend 和断言全部转绿，
  不是语义 fallback。
- manifest 更新为原版 359 assets/103 RTL/23 fixture/260 consumer/25 outputs，当前 520 assets/
  77 RTL/74 FST/1 VCD/67 consumer；零 missing asset、零未归属原版 HDL。manifest SHA-256 为
  `2117e51ce13742dbc3caa70314da893828e0c7f3fd76cf049270ca60a37e015a`；矩阵 SHA-256 为
  `27af6e7594d39574bb924c938819a49d28d3c1097e2c972437cb5afafe29e455`。两者按
  manifest write→matrix write→manifest write→matrix check→manifest check 到达可重复稳定点。
- 十二项 timing 从 partial 升级为 semantic-equivalent，矩阵变为
  `semantic-equivalent=65, proven-unobservable=2, partial=19, missing=2`；P3-C 队列从 24
  降为 12，只剩 Phase5 十项、`fixture.active_trace_runner` 和 p0_4 declared-only orphan。
  每行只绑定本 case RTL、共享 timing DUT 和本 case FST，未顺带关闭其余缺口。
- 本批前后 manifest 结构化外部审计的排序 JSON SHA-256 均为
  `b36b1c254efbe860544f135e0fd964b9cd6fedd0cbb1bb32c7b3aae5de8c4bcb`：xverif 保持
  HEAD `5110099482b...` 和 18 个既有 dirty/untracked 条目逐路径、状态、大小、内容不变；Wellen
  `afab0abd...`、Verilator `bf01d667...` 均 clean。所有 HOME/TMP/socket/build/session 输出均在
  当前仓库；没有越过 Goal 的唯一可写根，没有 fixture fallback、PR 或 push。

### P3-C timing 当前状态

| 项 | 状态 | 证据 |
| --- | --- | --- |
| 十二份 case RTL/共享 RTL | 已完成 | 13 份逐字节相同；12 个 fixture、72 条传递哈希锁 |
| 锁定原版 native oracle | 已完成 | 12/12；只读 cache；oracle `2a67dd92...` |
| timing 精确差分 | 已通过 | 26/26；私有停止前缀、活动时刻、值与 limits |
| 实现/聚焦回归 | 已通过 | `31bdddf`；209/209 |
| CTest | 已通过 | 关键 CTest 3/3 |
| manifest/matrix 门禁 | 已通过 | 静态联合 37/37；连续 write/check 稳定 |
| timing 矩阵关闭 | 已完成 | 12 semantic-equivalent；剩余 P3-C queue=12 |
| 外部零写入/零 fallback | 已通过 | 前后 audit snapshot `b36b1c25...` 相同 |
| timing red/green/证据 commit | 已完成 | `ad6f5f3` / `31bdddf` / 本批当前提交 |

### 2026-08-30：P3-C Phase5 十场景红灯冻结

- 本批以冻结公开 runtime `8eecf71271cc523d93bf03f6b9f9b6fa04ed3ee8`、schema
  `c45099040abf3dbe194d3ba27c207d7637b39ba9f9d662fad3d9d50dda99fb2c` 和
  73/73 Action 身份门禁为原版权威。执行文件 SHA-256 为 `0f54515f...`，wrapper SHA-256 为
  `c9569332...`，NPI 固定 `X-2025.06-SP1`。复用只读 Phase5 cache
  `2ee4a76b...-prepare-tydl8ogq`，原始 FSDB SHA-256 为 `9fdc31f0...`；没有重建 cache，
  没有接受 live catalog/report 漂移，也没有 fallback。
- `tools/collect_p3c_phase5_public_oracle.py` 通过公开 JSON Action 在 UDS combined session 内采集
  S1～S10，严格校验 runtime 身份、Action 数、响应完整性和 session 正常关闭；HOME、TMP、cache、
  socket 与 session 写入全部位于当前仓库。原版仓库、FSDB/daidir 和 cache 只读，输出还会拒绝
  绝对路径泄漏。连续采集两次得到相同 oracle：
  `tests/data/rtl_wave_differential/p3c-phase5.public-oracle.json` SHA-256 为
  `2da78f7e629356e4fe55bad144a9e42a1b799073999fa13a67aae1d7c2108cca`。
- 锁定公开响应与旧 catalog 预期存在历史漂移，因此本批不把旧 termination 摘要冒充实测。
  S1～S6、S9 均为 `ambiguous/multiple_rhs_sources`，一个 hop、DUT 第 37 行、六个 RHS；S7、S8、
  S10 均为 `ambiguous/multiple_active_candidates`，零 hop、DUT 第 34/39 行、九个 RHS。十项都
  `scan_complete=true`、`analysis_complete=true`、`response_truncated=false`。原版 NPI 对
  unpacked `dout` 元素保留显式 width diagnostic 和 unsized hop；`flag` 宽度完整。
- 当前仓库补入 `dut.sv`/`tb.sv` 两份逐字节镜像，SHA-256 分别为 `168e18c...`、
  `ea4c1d23...`。选择性生成器仅构建 Phase5，连续两轮得到相同 FST/XDD：SHA-256 分别为
  `f242c3fe...`、`065f226b...`；`fixture.sha256` 锁住两份 RTL、dump probe、FST、XDD、manifest
  共六个传递输入，未重建其他 fixture 缓存。
- `tests/test_p3c_active_trace_phase5.py` 对每个场景逐字段比较完整公开响应：termination/detail、
  完整性、statement/源码行、RHS 集合与顺序、before/after 值及时刻、hop/源码上下文及宽度投影；
  另设 `max_trace_signals=1` 的 analysis-boundary 门禁。唯一允许的表示投影是当前 FST/DesignDB
  给出比 NPI 更强的精确 8-bit 宽度，以及通用 `proc_assign` kind；源码和行为事实仍须严格一致。
- 有效红灯结果为 2 pass/11 fail：oracle 身份/去敏与 RTL/fixture 哈希先通过；十个完整响应和
  limits 边界全部失败。当前实现仍混入特殊分支 statement，把 S1 错判为两候选/七个 RHS，
  native flattened leaf 的局部端口映射、RHS 稳定排序、after evidence time 和 summary 宽度字段
  也尚未对齐。失败发生在业务断言内，不是 UDS、fixture、NPI 或环境失败。
- 随后的通用实现修复没有使用 Phase5 case、fixture 路径或 oracle 常量。duplicate-top 公共路径
  现在能区分 materialized unpacked element 与 packed selection，并用内部 base 绑定 loop selector；
  已绑定用户选择器的 procedural loop 保留可表示的 statement/RHS 证据，不再误投影成无源码
  endpoint。表达式临时量展开后重新执行唯一局部端口映射，普通 RHS 与 `rhs_loop_selected` 标记
  同步映射；native dependency group 排除 target loop index，但保留 `mask_a[ln]`/`mask_b[ln]`。
- 歧义响应按投影后的公共信号名稳定排序，before 仍指向前一真实变化，after 的 `value_time` 明确
  使用 evidence active time；绑定 loop selection 的证据使用目标实际活动时刻。精确 materialized
  loop element 不套用“唯一变化 RHS”消歧，packed selection 仍按完整 selector domain 评估。
  `trace.active_driver_chain` 还显式发布 `value_width_complete=true,width_diagnostics=[]`，反映当前
  FST/DesignDB 的精确宽度事实。
- Phase5 专项从 2 pass/11 fail 转为 13/13 通过；P0 14、composite 42、Phase4 42、timing 26、
  Phase5 13、combined 85 的联合相邻回归共 222/222 通过。`runtime-schema-validator`、
  `xdd-design-backend`、`wellen-fst-backend` CTest 3/3 通过，增量构建和 `git diff --check` 通过；
  未重建 fixture 或 CMake cache。
- 红灯执行使用既有 `.conda-xverif` pytest 解释器，只禁用与本地单测无关的自动编排插件；
  `XVERIF_TEST_TMPDIR` 与 `--basetemp` 均显式指向当前仓库。整个批次遵守 Goal 唯一可写根
  `${REPO_ROOT}`（仅解析为本任务当前仓库根），未修改原版/依赖仓库，未发起 PR 或 push。
- 矩阵收口不再引用旧 `phase5.runtime-audit.json` 的 termination/ambiguity 子集作为等价性权威，
  而是逐行绑定完整公开 oracle、两份精确 RTL 镜像、本 Phase5 FST 和四项专项测试。旧 audit 仍以
  `historical_subset_audit/status=partial` 保留，S6/S8 的 catalog/report 冲突也继续显式记录，避免
  通过覆盖历史证据来制造“等价”。十项公开请求均固定 `render_time_unit=ns` 和
  `max_depth=64,max_nodes=64`。
- `tools/build_rtl_wave_semantic_matrix.py` 新增 fail-closed Phase5 oracle validator：锁住 Goal、
  runtime/schema/binary/wrapper/cache/session 身份、零 fallback/只读来源、catalog/RTL/fixture 传递
  哈希、十行顺序、完整性、statement/RHS 顺序与计数、动态 selector 哨兵、sample evidence time、
  hop 和宽度诊断。对应 mutation 门禁会分别拒绝错误 Goal、不完整响应、RHS 重排、after 时刻漂移
  和 fallback 标记漂移。
- 允许的两项 schema projection 逐场景显式列出：所有场景只允许 NPI textual `assignment` 到
  DesignDB `proc_assign` 的 kind 映射；S1～S6/S9 额外允许原版 unsized hop + 明示 width diagnostic
  被当前精确 8-bit FST/DesignDB 事实加强。其余 statement、源码行、RHS、值、时刻、termination
  和完整性不得投影。
- Phase5 十项已从 `partial` 升级为 `semantic-equivalent`。矩阵总计变为
  `semantic-equivalent=75, proven-unobservable=2, partial=9, missing=2`；P3-C queue 从 12 降为
  2，只剩 `fixture.active_trace_runner` 与 `active.p0.declared_only_p0_4`，没有借 Phase5 证据顺带
  关闭 runner 或无原版 RTL/catalog request 的 orphan。
- manifest/matrix 按 write→write→write→check→check 到达可重复稳定点。manifest 保持原版
  359 assets/103 RTL/23 fixture/260 consumer/25 outputs，当前更新为 529 assets/79 RTL/75 FST/
  1 VCD/69 consumer，零 missing asset、零未归属原版 HDL；SHA-256 分别为
  `1f2417891e3dceecf59ba1796b822370f6ae7ad11c5197ab3f50e3dd2c42d70c` 与
  `b2889f3f452287e4b1494486f18b73041b85dbc51741d2f24abcbb462f7b4748`。
- 最终门禁为 Phase5 13/13、P0/composite/Phase4/timing/Phase5/combined 相邻回归 222/222、
  manifest/matrix/P3-B/producer 静态联合 39/39、关键 CTest 3/3、两项生成器 `--check` 和
  `git diff --check` 全通过。没有重建任何 fixture 或 CMake cache。
- manifest 结构化外部审计在本批前后排序 JSON SHA-256 均为
  `b36b1c254efbe860544f135e0fd964b9cd6fedd0cbb1bb32c7b3aae5de8c4bcb`；原版 xverif 的既有
  dirty/untracked 状态以及 Wellen/Verilator clean 状态逐项未变。全部运行时临时文件仍位于当前
  仓库，零外部写入、零 fixture/backend fallback。

### Phase5 实现当前状态

| 项 | 状态 | 证据 |
| --- | --- | --- |
| 锁定原版公开 oracle | 已完成 | S1～S10 完整响应；SHA-256 `2da78f7e...` |
| 两份 RTL 与当前 fixture | 已完成 | RTL 逐字节相同；FST/XDD 连续两轮确定 |
| red 门禁 | 已完成 | 2 pass/11 fail；11 项均为目标语义差异 |
| runtime 写边界/fallback | 已通过 | 写入仅当前仓库；外部只读；零 fallback |
| 通用实现修复 | 已完成 | Phase5 13/13；联合相邻回归 222/222 |
| CTest | 已通过 | 关键 CTest 3/3 |
| manifest/matrix 门禁 | 已通过 | 静态联合 39/39；连续 write/check 稳定 |
| Phase5 矩阵关闭 | 已完成 | 10 个 semantic-equivalent；P3-C queue=2 |
| Phase5 red/green/证据 commit | 已完成 | `77e628b` / `9c68af5` / 本批提交 |

### 下一步

1. 单独裁决 `fixture.active_trace_runner` 与 p0_4 declared-only orphan。p0_4 没有冻结 RTL/catalog
   request 时不得编造原版 fixture；只能补成有权威来源的动态合同，或给出受 schema/资产哈希约束
   的有限 proven-unobservable 证明。
2. P3-C queue 清零并通过独立门禁后，再进入 P3-D 性能/大波形与 P3-E 剩余 Action，最后执行 P4
   全量验收；不得用 Phase5 的局部成功提前宣告 Goal 完成。

### 2026-08-30：P3-C runner/orphan 有限不可观察证明红灯

- `xdebug.active_trace_runner` 的冻结输入只有 Makefile、`chain_test.cpp`、`chain_test.h`，输出仅为
  私有 native NPI 测试可执行文件 `build/chain_test`；它不接受 RTL，不生成波形，也不是冻结 73
  Action 中的公开请求。P0/composite/timing/Phase4 共 58 个 catalog case 已由同一锁定 runner
  SHA-256 `f7e80398...` 的 native oracle 覆盖；Phase5 十项由冻结公开 runtime 完整 oracle 覆盖。
  因此待证明的是“无额外公开观察点”，不是把私有 runner 输出伪装成当前公开响应等价。
- `p0_4_interface_modport` 在冻结原版中只有 README 第 22 行声明和目录内一个 `.gitignore`；没有
  RTL/TCL、catalog row、signal/time 请求、具体 FSDB 或可执行 stimulus。当前已有的 interface/
  modport 动态用例只能证明当前能力，不能反向创造原版合同。待建立的证明严格限定在冻结资产与
  `trace.active_driver_chain` 必填 signal/time 的公开 schema；不泛化为 interface/modport 免测。
- 新增红灯 `tests/test_p3c_active_trace_closure.py`，要求仓库内存在去敏、可重建 audit，runner 的
  68 个既有公开观察点全部闭合、额外观察点为零，orphan 的冻结目录/catalog/request 计数均为零，
  两项才可升级为 `proven-unobservable` 并清空 P3-C queue。当前 audit 尚不存在、矩阵仍为
  partial/missing，预期三项失败；不运行 EDA、不重建 fixture/cache，也不允许用 current 候选替代。

### 2026-08-30：P3-C runner/orphan 有限不可观察证明转绿

- 新增 `tools/build_p3c_active_trace_closure_audit.py`，只读冻结 manifest、原版目录、既有 native/
  public oracles 和当前 request schema，确定性生成去敏 audit；输出路径由仓库边界检查保护，工具
  同时提供 `--write/--check`，不接受默认外部根或 fallback。audit SHA-256 为
  `c9d29f6e93c598501013c011243b62aa81574f104ffdd8d53f5474a8ac51c7fc`。
- runner 证明锁住 fixture 的三项输入与唯一 `build/chain_test` 输出、源文件哈希、私有 NPI 入口、
  runner binary SHA-256 `f7e80398...`、catalog 68 行和 `trace.active_driver_chain` 必填
  signal/time 的封闭 request schema。P0/composite/timing/Phase4 的 58 行逐项链接 native oracle，
  Phase5 十行链接完整公开 runtime oracle；所有 68 个目标场景必须先是 semantic-equivalent，矩阵
  才允许把 runner 自身标为 proven-unobservable。
- 原版 runner consumer 扫出的四个 Action 也没有被汇总计数掩盖：`session.open`、
  `trace.active_driver_chain`、`session.close` 的可执行请求由 Phase5 公开 oracle 覆盖；
  `trace.active_driver` 只出现在 README 第 9/10/27 行，冻结 consumer 中没有可执行 JSON 请求。
  audit 要求剩余未映射 consumer Action 和额外公开观察点都为零。
- orphan 证明锁住 README 第 22 行、原版目录唯一 `.gitignore` 及其 SHA-256、catalog SHA；并要求
  RTL、stimulus、具体 waveform、catalog row、权威 signal/time 请求均为零。矩阵移除 current
  interface/modport candidate，避免以当前能力反向创造原版行为；proof scope 明确只关闭此冻结
  空骨架，后续若出现权威 RTL/request，interface/modport 仍必须正常差分。
- matrix validator 对 Goal/schema、只读/零重建/零 fallback、绝对路径、runner fixture/source、
  公开/私有 schema 边界、四份 native oracle、一份 Phase5 oracle、68 行顺序、consumer Action
  disposition 和 orphan 清单全部 fail closed；mutation 门禁会拒绝 fallback、未关闭观察点、把私有
  helper 冒充公开 Action、或凭空增加 orphan RTL。
- 红灯 3/3 失败已转为 closure/matrix 20/20 通过；manifest/matrix/P3-B/producer/closure 静态联合
  43/43、active-trace 相邻回归 222/222、关键 CTest 3/3、audit/manifest/matrix 可重复 `--check`
  和 `git diff --check` 全通过。没有运行 EDA，没有重建 fixture 或 CMake cache。
- runner 与 orphan 均升级为有限 `proven-unobservable`；矩阵变为
  `semantic-equivalent=75, proven-unobservable=4, partial=8, missing=1`，P3-C queue 已清空，剩余
  只有 P3-D 六项和 P3-E 三项。manifest 为原版 359 assets/103 RTL/23 fixture/260 consumer/
  25 outputs，当前 532 assets/79 RTL/75 FST/1 VCD/71 consumer；manifest/matrix SHA-256 分别为
  `b01771b8c2bf3aad9c58fa31c974681cec6d7f71a02f8cb3c623b38196a3e79a`、
  `7a8a21ab9c8071641f00b26e083e6bd91f016da88f3b5aa47f79954242062c56`。
- 前后结构化外部审计 SHA-256 仍为
  `b36b1c254efbe860544f135e0fd964b9cd6fedd0cbb1bb32c7b3aae5de8c4bcb`；原版 xverif 的既有
  dirty/untracked 清单和 Wellen/Verilator clean 状态未变，零外部写入、零 fallback。

### P3-C 最终状态

| 项 | 状态 | 证据 |
| --- | --- | --- |
| 五组 68 个 catalog 场景 | 已完成 | 75 个矩阵 semantic-equivalent 中含全部 68 行 |
| 私有 runner 独立观察面 | proven-unobservable | 58 native + 10 public；四个 consumer Action 全裁决 |
| p0_4 空骨架 | proven-unobservable | 零 RTL/stimulus/request/waveform；当前候选不作等价 |
| P3-C queue | 已清空 | 矩阵不再包含 P3-C gap |
| 门禁 | 已通过 | closure 4/4；静态 43/43；相邻 222/222；CTest 3/3 |
| red/green/证据 commit | 已完成 | `6b09512` / 本批提交 / 本批提交 |

### 下一阶段

1. 进入 P3-D，按 stream/APB/AXI 六个 fixture 逐项建立 red→green 差分；不得把协议工具 runner、
   XAMBA wrapper 或代表性 FST 混成同一场景。
2. P3-D 清零后进入 P3-E 的 `npi_fsdb_sva`、`xif_event` 与 cross-fixture consumer，最后执行 P4
   全量门禁和最终差异报告。

### 2026-08-30：P3-D1 stream_v1 原版公开语义红灯

- P3-D 的六项不再共用“代表性协议 fixture”结论，拆成 D1 stream_v1（同时裁决私有
  differential runner）、D2 APB VIP/XAMBA、D3 AXI VIP/XAMBA 三个可独立回滚子批。每个子批
  均先提交锁定原版 oracle 与有效业务红灯，再补当前 fixture/实现，最后单独更新矩阵和证据。
- 原版 `stream_v1_top.sv` 固定 `N=20000`，在 5 ns 半周期时钟上生成七类流：valid-only、
  ready、backpressure、ready packet、bp packet、negedge ready+bp packet 和双通道交织 packet。
  它不是当前 `testdata/fixtures/stream` 的 4-transfer 小样例。锁定原版预期分别为 20,000、
  15,059、17,142、20,000、20,000、20,000、20,000 次 transfer；ready/bp stall 分别为
  3,764/2,858，四类 packet 各 5,000 包，并有 1 次 ready/bp 冲突。
- 新增 `tools/collect_p3d_stream_v1_public_oracle.py`，仅通过冻结 runtime
  `8eecf71271cc/c4509904...` 的公开 JSON Action 读取既有只读 FSDB cache；runtime wrapper/
  binary SHA-256 分别为 `c9569332...`/`0f54515f...`，73/73 Action 与 NPI
  `X-2025.06-SP1` 身份门禁通过。复用 cache 版本
  `5eca27af...-prepare-c54cyr7t`，FSDB SHA-256 为 `0507c9c...`，没有重建 fixture。
- oracle 共 58 个完整观察点：七个 stream 的 config/list/describe/validate、summary、first/last、
  transfer window，以及部分包、包索引、越界、stable mismatch、stall、negedge、交织、beat/
  packet/channel filter 和 invalid-interleaving 错误。所有 query/validate 都要求 scan/analysis
  完整，允许的裁剪只能是请求明确造成的 response 侧 line limit；不保存 FSDB 内容、导出物、
  私有分析缓存或绝对路径。oracle SHA-256 为 `8c40c3ec...`。
- 新增 `tests/test_p3d_stream_v1.py`。第一项先锁住 runtime/cache/RTL/config/FSDB/expected 哈希、
  58 项 observation 集合、正常 session close、零 fallback 和去敏边界；第二项要求当前仓库提供
  逐字节相同 RTL/config、固定 expected、可重建原始 FST/hash lock，并逐项严格比较 58 个公开
  响应。有效红灯为 1 pass/1 fail，唯一失败是当前 `testdata/fixtures/stream_v1/stream_v1_top.sv`
  缺失；失败已进入目标业务断言，不是 runtime、NPI、socket 或 cache 错误。
- 本批所有 HOME/TMP/cache/socket/session 写入都在当前仓库
  `build/goal-runtime/p3d-stream-v1-original`；外部审计的规范 JSON SHA-256 仍为
  `b36b1c254efbe860544f135e0fd964b9cd6fedd0cbb1bb32c7b3aae5de8c4bcb`。没有 FSDB→FST/
  JSON 转换，没有 export 回灌，没有 fixture/backend fallback，也没有修改外部仓库。

### 2026-08-30：P3-D1 stream_v1 export/XOUT 补充红灯

- 58 项 query/config 主体转绿后的相邻 `tests/test_stream.py` 暴露两项旧断言：stall reason 仍锁
  `vld_without_rdy`，export preview scope 仍锁 `response_rows`。前者已由上述原版 oracle 证明应为
  `rdy_low`；后者没有凭当前实现或旧测试裁决，而是回到同一冻结 runtime 补采 export 证据。
- 按 xverif 流程请求 `actions args.output.view=guide` 时，冻结 schema 明确以
  `INVALID_REQUEST/args.output.view` 拒绝；本批未切换 surface、transport、runtime 或 fixture，改为
  锁住该版本真实能力事实，同时复核普通 73-Action catalog，并查询
  `xdebug.stream.export.request.v1` 精确 schema。这个版本差异写入 oracle，不伪称 guide 成功。
- 新增 `tools/collect_p3d_stream_v1_export_oracle.py`，复用同一只读 FSDB cache，在仓库内 UDS session
  采集 transfer/packet/packet_beats 三类 preview 和三类小窗口 written artifact，共 6 个观察点；
  两次独立采集逐字节一致，oracle SHA-256 为
  `66a9c58192c4f6ff95edc70ff67537425ddc1aaaa52ce32d667ec25b113b8c29`。
- 原版 preview 的 `row_count/total_count` 是完整结果数，`returned_count` 才受 line limit 限制，裁剪
  scope 固定为 `response_preview`；written 必须写全量、`response_truncated=false`，并生成字段清单和
  完整 summary meta。锁定小窗口 transfer、packet、packet-beats artifact SHA-256 分别为
  `b23f0101...`、`ed57d7dd...`、`2fe74fd9...`；精确值由 oracle 逐字节保存。
- 原版 preview XOUT 同时留下既有缺口：不投影 requested/scanned range，packet preview 还省略中间
  beat 值；当前侧不复制这项退化，而以 XOUT 语义审计要求完整投影。JSON 和 artifact 仍要求与原版
  严格一致，这一“原版缺口、当前增强”是显式裁决，不是白名单放宽。
- 新增 `tests/test_p3d_stream_v1_export.py`。红灯为 1 pass/1 fail：runtime/schema/oracle/XOUT 缺口锁
  通过，当前侧在第一个 `transfer_preview` 的 summary 公开语义处失败；尚未进入后续 artifact 比较，
  因而不是伪造文件差异。所有采集输出仍只写当前仓库，没有 fallback 或 fixture 重建。

### 2026-08-30：P3-D1 stream_v1 主体与 export 转绿

- 当前新增 `testdata/fixtures/stream_v1`：RTL 和 config 与原版逐字节一致；Verilator 只在仓库内
  build copy 上应用条件屏蔽专有 `$fsdbDump*` task 的 compile-only overlay，提交的 RTL 不改一字。
  C++ timing harness 直接生成 FST，不经过 FSDB/VCD/JSON 转换；RTL 自写 expected 与冻结 expected
  逐字节一致。FST 为 1,398,608 bytes，SHA-256 固定为 `ead0ce4e...`。
- `fixture.manifest.json` 锁住原版 RTL/config/expected 哈希、`N=20000`、无随机化 seed、1ns/1ps、
  5ns 半周期，以及 Verilator revision/tree/fingerprint/patchset 和 GCC 13.3.1。生成脚本在安装 FST
  前先核对 expected、size 和 SHA，禁止外部 cache 重建与 FSDB conversion；`fixture.sha256` 逐文件
  覆盖 RTL/config/expected/overlay/harness/manifest/FST。
- 使用两个全新的仓库内目录 `stream_v1-determinism-b/c` 完整编译和运行，二者及提交 FST 的
  SHA-256 均为 `ead0ce4e...`，`cmp` 全部通过。第一次隔离尝试发现生成脚本仍硬编码默认 source
  path；在计入门禁前已修为每次使用各自 work-dir 的 compile copy，随后才执行有效的 b/c 双重
  复现。没有删除或重建任何既有 fixture cache。
- stream 实现补齐 alias/表达式 clock、valid/ready/bp/sop/eop/data/channel、before/after/negedge
  采样、ready+bp conflict、stable field、partial/interleaved packet、exact/range/mask filter、channel、
  stall boundary 和原版错误包络。58 个 config/describe/validate/query 公开响应逐字段严格一致。
- `stream.export` 改为完整结果计数与有限 preview 分离：preview scope 为 `response_preview`；written
  不受 preview line limit 裁剪，并按原版字段顺序输出 transfer/packet/packet-beats 及完整 meta。
  六项 JSON、三份 artifact/meta 的 size/SHA/文本全部与原版一致。当前 XOUT 继续投影 range、channel、
  stable field 和多 beat，关闭原版已登记的信息缺口，不以逐字符复制退化求一致。
- 原有 `tests/test_stream.py` 的 stall reason、preview scope、导出 header/packet row 断言同步为已证明
  的原版值。P3-D1 query/export focused 与相邻 stream 回归合计 44/44 通过；没有 skip、xfail、响应
  白名单或 completeness 放宽。
- 本提交只关闭 `stream_v1` 主体和 export 实现。`stream_differential_tool` 私有 legacy comparator、
  cache probe 与矩阵状态仍保留在本批后续门禁，未在此处提前宣告 P3-D1 完成。

### 2026-08-31：P3-D1 differential/cache 有限闭环红灯

- 原版 `xdebug.stream_differential_tool` 已确认不是独立 RTL/波形 fixture：builder 仅执行
  `make stream-differential-test-dist`，输出 frontend 与 engine 两个可执行文件，RTL 输入数和波形
  输出数均为零；consumer 复用 `xdebug.stream_v1` FSDB，并导入原版 action matrix 与 cache contract
  两项测试。因此矩阵不得把专用工具伪装成另一份当前 FST，也不得把它与 `stream_v1` 重复计数。
- 复用只读 differential cache `d165237d...-prepare-jat8dddw`，frontend/engine/legacy object
  SHA-256 分别为 `9a5467c8...`、`ac584ab5...`、`d8e3621c...`；build identity 固定为
  `846edd6800bd/6ace27b2...`，Action 为 73/73，NPI 为 `X-2025.06-SP1`。engine strings 同时锁住
  `legacy stream differential oracle failed` 与 `stream columnar differential mismatch` 两个失败哨兵，
  三个公开 call site 则锁住 query/export/validate 均通过编译期 guard 调用 comparator。
- 新增 `tools/collect_p3d_stream_differential_audit.py`：使用上述专用 binary 回放已经冻结的 58 个
  query/config 观察和 6 个 export 观察；结果为 64/64 零差异、3/3 artifact 一致、3/3 XOUT 一致，
  comparator 失败数为零。它同时查询三份 stream request schema，73 个 Action 和 schema 均不存在
  differential/probe 公开入口。
- cache consumer 被严格拆成公开与私有两层。13 个 base 公开观察、6-child batch 和三次 soft-budget
  query 都有完整响应 oracle；`scanner_invocations/hits/misses/evictions/resident_bytes` 等十个字段只在
  `XDEBUG_TEST_ANALYSIS_PROBE_PATH` 私有 JSONL 中出现，原版源码明确声明不属于 Action/schema/JSON/
  XOUT，故只对冻结的私有 metric 作有限 `proven-unobservable` 裁决。hard=1 不可归入私有层：两个
  batch child 都公开返回 `ANALYSIS_MEMORY_LIMIT_EXCEEDED`、handler layer、recoverable、protocol、
  hard limit 和两条 next action，必须由当前实现通过门禁。
- 两个全新的当前仓库工作目录 A2/B 独立执行全部采集，正式 audit 三份逐字节比较结果一致，SHA-256
  为 `c606046efa26998c5be060f8e33658e0c56dcb475fa440c3a7c92a3d17770607`。唯一允许的当前表示投影是
  hard error 中 16-hex `key_summary` 可因原版 FSDB 与当前原生 FST source identity 不同而变化；错误
  结构和其余字段仍逐项严格比较。
- 新增 `tests/test_p3d_stream_differential_closure.py`。有效红灯为 2 pass/2 fail：audit 身份边界以及
  batch/soft-budget 全部公开结果先通过；base 在第一个 `dynamic=false` 静态 validate 处失败，唯一
  差异是原版 `summary.scan_complete=true`、当前为 false；hard-limit 失败则是当前两个 child 均错误地
  返回成功。此前一次长 UDS 路径导致 engine 启动失败、一次缺少 `--basetemp` 父目录，均发生在业务
  断言前且已排除，不计入红灯。
- 所有 HOME/TMP/cache/socket/session/audit/artifact 写入均位于当前仓库 `.tmp` 或正式测试目录；外部
  原版源码、两份 fixture cache 和 FSDB 只读，没有重建 fixture/CMake cache，没有切换 runtime、
  transport、surface 或 backend，也没有 fallback。

### P3-D1 differential/cache 红灯门禁

| 项 | 状态 | 证据 |
| --- | --- | --- |
| 专用 comparator 公开回放 | 已通过 | 58 query/config + 6 export；零差异 |
| 私有 cache metric 边界 | 已冻结 | 73 Action/三份 schema 零泄漏；probe 源码与 object/hash 锁 |
| 当前 base 公开 cache 语义 | 红灯 | static validate `scan_complete` 漂移 |
| 当前 hard-limit 公开错误 | 红灯 | hard=1 两个 child 错误返回成功 |
| 矩阵关闭 | 待办 | 两项当前门禁转绿后才允许更新候选分类 |
