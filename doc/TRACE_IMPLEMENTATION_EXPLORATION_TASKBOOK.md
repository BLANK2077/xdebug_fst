# xdebug-fst Trace 实现路线探索任务书

## 一、目标

基于现有 1K～64K 大规模复杂 RTL 基准，自主探索并实现多种静态 Trace DesignDB
构建、装载和查询方式，对比端到端耗时、峰值内存、磁盘产物、session 常驻内存与
代表性 Trace Action 延迟，最终给出有数据支撑的推荐路线，并把可验证的优胜方案
实现留在 xdebug-fst 仓库。

本任务把问题拆成两个相互独立的成本面：

1. 数据生产与装载：Verilator 抽取静态事实后，如何编码、落盘并由 xdebug-fst 打开；
2. 查询执行：xdebug-fst 如何解析信号、反查端口连接、绑定 selector 并沿 driver 图遍历。

现有基线已经表明 64K 规模下 DesignDB C++ 为 205,761,726 bytes，G++ 编译耗时
170.38 s、峰值 RSS 6,553,748 KiB、共享库为 346,661,424 bytes；active-driver chain
中位延迟为 5,338.420 ms。新方案必须分别说明改善了哪一类成本，不能用局部计时替代
端到端结果。

## 二、固定边界

- 所有实现、补丁、基准脚本和报告均保存在 xdebug-fst 仓库。
- Verilator 与 Wellen 继续引用配置文件锁定的官方 master revision；修改以本仓库 patch
  形式维护，不依赖个人 fork 分支。
- C/C++ 编译必须使用 xdebug_oc 隐藏目录中的私有 GCC/G++ 13.3.1；禁止回退到系统编译器。
- 允许因 Verilator trace 数据生成方式变化重建独立实验构建，但不覆盖或隐式刷新现有
  fixture cache；只有语义 fixture 确需变化时才单独记录并重建。
- 不提交生成 RTL、obj_dir、FST、DesignDB、profile 原始文件、本机绝对路径或大体积基准产物。
- 每种实现使用独立输出目录；不修改旧基线 `/tmp/xfst-large-rtl-full-v2`。
- 不执行 VCS、NPI、VIP 或其他 proprietary EDA 流程。
- 不采用静默 fallback。实现或工具不可用时保留失败结果并停止该路线，必要时向用户申请调整。

## 三、待比较方案

### A：现有 C++ 静态表共享库

保留 patched Verilator 生成 `__DesignDb.cpp`、G++ 编译 `.so`、xdebug-fst 通过 C ABI
`dlopen` 的路径，作为语义和性能基线。

除已完成的 `-O2` 数据外，增加 `-O0` 与 `-Os` 编译策略实验。该实验只回答“C++ 编译参数
能否低成本降低构建资源”，必须分别报告：

- 复用同一生成 C++ 后的纯 DesignDB 编译时间；
- `.so` 大小、打开时间、server RSS 和查询延迟；
- 与完整 Verilator 前端时间分离后的端到端估算。

编译参数实验不能被包装成新的存储实现，也不能替代后续二进制路线。

### B：版本化紧凑二进制 + mmap 后端

设计 `xdebug.design-db.binary.v1`，使用小端固定宽度记录、去重字符串池、section
offset/count 和完整边界校验保存 signal、name index、driver、load、port connection 事实。
xdebug-fst 新增直接实现 `IDesignBackend` 的 mmap 只读后端，bundle manifest 显式声明格式，
不再为每个设计生成和编译巨型 C++ 共享库。

该路线分两步验证：

1. 语义原型：从同一份 Verilator 抽取事实生成二进制，验证与 `.so` 后端逐项等价；
2. 直接生产：修改本仓库的 Verilator patch，让 Verilator 在已有单次前端展开中直接写出
   二进制，完整测量前端、落盘、session 与查询成本。

原型转换时间必须单列，不能从最终数据中隐藏。最终推荐数据以“Verilator 直接生成”为准。

### C：运行时反向索引与查询缓存

保留 `IDesignBackend` 事实语义，在 session open 时一次建立下列只读索引：

- connected signal → 按方向分类的 port signals；
- scope → input/output port signals；
- selector base → 已物化 selector 最大值或有序区间；
- 必要时缓存 signal name、scope 和 hierarchy depth。

用索引替换 `ports_connected_to()`、`annotate_output_instance_identities()`、
`last_materialized_selector()` 中的全表扫描。该方案必须同时支持 A/B 后端，并分别报告
open-time/RSS 增量和 Action 延迟收益，防止用不可控内存换取局部加速。

### D：组合优胜方案

在 A/B/C 单项结果稳定后组合“优胜存储后端 + 优胜查询索引”，用完整规模矩阵验证端到端
收益。若某项单独没有收益，不进入组合方案，并在报告中保留原因和测量数据。

## 四、语义与格式合同

### 4.1 后端等价

对同一设计，A/B 后端必须在下列数据上完全等价：

- signal count、名称、类型、宽度、源码位置、方向；
- name resolve 的成功、失败和返回 index；
- 每个 signal 的 driver/load/port connection 数量与记录内容、顺序；
- activation predicate、dependency role、kind 等字符串的字节内容。

增加后端无关的 parity 工具或测试，输出去路径化摘要/hash。发现差异时失败关闭，不以排序、
丢字段或回退 `.so` 方式掩盖。

### 4.2 二进制安全

二进制文件至少包含 magic、schema major/minor、endianness、总长度、section directory、
记录数和字符串池边界。打开时检查：

- 文件大小、整数溢出、offset/count 乘法与范围；
- section 重叠和未知必需 section；
- 字符串 offset 与 NUL 终止；
- signal/driver/load/port index 范围；
- name index 排序和 group start 单调性；
- schema 不兼容与截断文件必须明确拒绝。

### 4.3 Bundle 兼容

保留 `xdebug.design-db-bundle.v1` 的 `.so` 行为；二进制使用新的严格 manifest schema，
不得根据扩展名猜测或失败后自动改开另一后端。session evidence 要明确报告实际 backend format。

## 五、性能测量方法

### 5.1 规模与筛选

- 快速开发：1K/2K 验证格式、错误处理和语义 parity。
- 初筛：8K/16K/32K，识别构建时间、RSS 和查询复杂度趋势。
- 决赛：所有可行方案执行 1K、2K、4K、8K、16K、32K、64K 完整矩阵。
- 旧 `.so -O2` 基线可复用已验收结果；新增路线必须从独立输出目录实测。

若路线在 32K 已因确定性资源错误失败，保留失败状态；不缩减 RTL 复杂度或更换工具链继续。

### 5.2 构建指标

每个规模记录：

- Verilator 前端 wall/user/sys、max RSS；
- DesignDB 数据编码或 C++ 编译 wall/user/sys、max RSS；
- 中间源码/二进制/共享库 bytes 和 SHA-256；
- 仿真模型构建与 FST 生成指标，确认静态 Trace 修改未影响仿真数据面；
- 完整冷构建总耗时和峰值 RSS。

### 5.3 Session 与查询指标

每个规模记录 session open latency、打开后 RSS、首次与稳态 latency（median、p95、min、max）：

- `signal.resolve`；
- `value.at`；
- `trace.driver`；
- `trace.active_driver`；
- `trace.active_driver_chain`；
- `signal.changes`。

查询索引实验额外采集索引建立时间、索引占用估算、全表扫描次数和访问记录数。所有 Action
必须保持相同请求、limit、FST、DesignDB 事实和结果完整性。

### 5.4 Profile

先对 64K active-driver chain 做可复现 profile 或内部阶段计时，区分：

- DesignDB resolve/driver/port API；
- 全表反向扫描与 selector 扫描；
- predicate parse/evaluate 与 Wellen signal lookup/load；
- chain JSON 组装和 source context。

profile 工具不可用时不私自换方法；先记录原因并向用户申请。优化前后使用相同采样方法。

## 六、阶段、提交与进度

### 阶段 0：任务书、goal 与基线冻结

- [x] 确认当前 C++ ABI 已有 name index 和按 signal 分组的关系表。
- [x] 确认 consumer 存在按 `signal_count()` 扫描的反向端口和 selector 查询。
- [x] 写入本任务书。
- [x] 建立 goal，明确验收标准。
- [x] 校验旧基线 hash、工具 revision、私有 GCC/G++ 与 fixture cache 状态。

计划提交：`文档：建立 Trace 多实现性能探索任务书`

### 阶段 1：可观测性与统一比较框架

- [x] 增加后端 parity 检查与统一 variant 描述。
- [x] 增加构建、session、查询和内部热点的结构化测量。
- [x] 对当前 64K 路径完成 profile，形成优化前归因。
- [x] 测量 A 路线 `-O0/-O2/-Os`，筛选低成本编译策略。

计划提交：`测试：增加 Trace 实现统一对比与热点测量`

### 阶段 2：紧凑二进制格式与 mmap 后端

- [x] 实现版本化二进制读写和严格校验。
- [x] 增加 `IDesignBackend` mmap 实现及显式 bundle schema。
- [x] 增加格式损坏、越界、版本不兼容和 A/B parity 测试。
- [x] 用 1K/2K 完成端到端原型验收。

计划提交：`功能：增加紧凑二进制 DesignDB mmap 后端`

### 阶段 3：Verilator 直接生成二进制

- [x] 修改本仓库 Verilator patch，在单次前端展开中直接输出 binary v1。
- [x] 使用锁定官方 revision 与私有 GCC/G++ 重建独立 Verilator 实验构建。
- [x] 验证仿真模型与 FST 不因 DesignDB 编码变化而改变。
- [x] 测量 8K/16K/32K 初筛数据。

计划提交：`构建：让 patched Verilator 直接生成二进制 DesignDB`

### 阶段 4：运行时索引优化

- [x] 以 profile 为依据实现反向端口、scope 和 selector 索引。
- [x] A/B 后端共用同一查询语义与索引实现。
- [x] 验证所有 active-driver/chain fixture 行为不变。
- [x] 测量索引构建成本、RSS 增量和稳态 Action 收益。

计划提交：`性能：增加 DesignDB Trace 运行时反向索引`

### 阶段 5：完整矩阵、报告与验收

- [x] 对可行单项与组合优胜方案运行 1K～64K 完整矩阵。
- [x] 输出构建、产物、session、查询的绝对值和相对基线倍率。
- [x] 给出推荐方案、适用规模、剩余瓶颈和不采用路线的原因。
- [x] 运行相关 C++/Python 测试与全量 pytest，检查 fixture cache 和绝对路径门禁。

计划提交：`文档：记录 Trace 多实现性能对比与推荐结论`

## 七、验收标准

1. 至少完成 A 编译策略、B 二进制 mmap、C 查询索引三类实现的可运行对比；D 组合方案由数据决定。
2. B 与 A 在完整字段、关系顺序、resolve 行为和代表性 Action 响应上通过自动 parity 验证。
3. 所有决赛方案完成 1K～64K 七档；失败路线有确定阶段、退出状态与资源数据，不被静默跳过。
4. 报告完整覆盖构建 wall time/max RSS、产物大小、session open/RSS、六项 Action 延迟。
5. 64K active-driver chain 有优化前后热点归因，查询优化不改变 complete/limitations 与事实内容。
6. 二进制后端对损坏、截断、越界和不兼容 schema 失败关闭，旧 `.so` bundle 保持兼容。
7. 使用锁定官方 Verilator/Wellen revision 和私有 GCC/G++ 13.3.1，无系统工具链 fallback。
8. 不提交本机绝对路径或大体积生成物；除非语义需求明确变化，否则 fixture cache 保持不变。
9. 分阶段中文详细 commit，最终工作树无无关修改；是否推送由用户另行明确指示。

## 八、风险与停止条件

- 直接二进制 emitter 会修改较大的 Verilator patch：先以 parity 原型冻结格式，再改 producer，
  避免同时调试编码与消费端。
- mmap 降低装载拷贝不等于降低文件 page cache；报告同时保留进程 RSS 与文件大小，不把二者混淆。
- open-time 全量索引可能让小设计变慢：所有规模都报告绝对值，允许按数据决定是否改为懒构建，
  但不得未经记录切换策略。
- 64K 完整矩阵耗时长、内存峰值高：按初筛门禁推进，不并行运行多个重负载编译，避免互相污染。
- 若语义 parity、工具链约束或 profile 能力无法满足，则停止对应阶段并请求用户决定，不做 fallback。

## 九、进度记录

- 2026-08-19：完成现有 ABI 与 consumer 静态检查。确认生成 `.so` 已具备二分 name resolve 和
  分组关系表，但 active trace 的反向 port、scope 和 selector 查询仍存在设计规模线性扫描；完成
  任务书，尚未重建 Verilator、fixture cache 或任何大规模 RTL。
- 2026-08-19：建立 goal 并冻结旧矩阵、官方依赖 revision、patch hash、私有 GCC/G++ 与
  fixture 状态；完成统一 benchmark、64K profile 和 `-O0/-O2/-Os` 对照。
- 2026-08-19：完成 `binary-v1` 格式、严格 mmap reader、转换/parity 工具及损坏文件测试；
  patched Verilator 支持在一次前端中直接流式生成 `.xddb`，七档输出与优化前 emitter 逐字节一致。
- 2026-08-19：完成后端无关查询索引和 Wellen definitive-miss 优化。64K active-driver chain
  中位延迟从 5,338.420 ms 降至最终 40.907 ms；二进制 session RSS 从 `.so` 基线的
  348,428 KiB 降至 102,024 KiB。
- 2026-08-19：完成 1K～64K 七档最终矩阵与报告；六类代表 Action 的 `.so`/binary
  `summary/data/limitations` 自动一致性测试、全量 pytest、CTest 7/7 和 Verilator 14 个
  `t_xdd*` 回归全部通过。`testdata/` 无 diff，未重建 fixture cache。
