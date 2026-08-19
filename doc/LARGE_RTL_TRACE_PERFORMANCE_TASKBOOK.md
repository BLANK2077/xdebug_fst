# xdebug-fst 大规模 RTL 与 Trace 性能任务书

## 一、目标

借鉴 xverif `xcov/fixtures/large_summary/generate_large_fixture.py` 的确定性大规模 RTL
生成方法，在 xdebug-fst 仓库建立一个本地性能基准，测量 RTL 源码从 1K、2K、4K、8K、
16K、32K 到 64K 行增长时，下列阶段的耗时、峰值内存和产物规模变化：

1. RTL 确定性生成与语义合同检查；
2. patched Verilator 单次前端展开，同时启用 `--trace-fst --design-db`；
3. Verilator 仿真模型原生编译和链接；
4. 生成的 `__DesignDb.cpp` 使用私有 GCC/G++ 编译为共享库；
5. 仿真运行并生成原始 FST；
6. xdebug-fst 打开 FST/DesignDB session；
7. 首次信号解析、`value.at`、静态 driver、active-driver、active-driver chain 和变化扫描。

本任务不修改既有 fixture，不重建 fixture cache，不把生成 RTL、obj_dir、FST、DesignDB、
原始计时日志或机器绝对路径提交到 Git。

## 二、边界与固定环境

- 只修改 xdebug-fst 仓库；xverif 生成器仅作为只读参考。
- 复用当前统一构建中的 patched Verilator、Wellen 和 xdebug-fst，不重新构建依赖缓存。
- C/C++ 编译器必须来自仓库相邻隐藏工具链目录，通过脚本解析并验证为 GCC/G++ 13.3.1；
  禁止回退到系统 GCC/G++。
- 生成物默认写入调用者显式给出的绝对输出目录；推荐使用 `/tmp` 或仓库忽略的本地目录。
- 不执行 VCS、NPI、VIP 或其他 proprietary EDA 流程。
- 不清理或覆盖用户已有构建目录；每个规模使用独立子目录。
- 不通过 drop-caches、CPU governor、管理员权限等方式改变宿主机全局状态。

## 三、RTL 生成合同

### 3.1 规模矩阵

固定目标源码行数：

```text
1024, 2048, 4096, 8192, 16384, 32768, 65536
```

每个目标必须精确生成对应行数。允许最后不足一个完整 tile 的尾部生成，但尾部必须由
可编译的声明、组合表达式或实例组成，不允许用大段注释伪造规模。

### 3.2 语义复杂度

生成设计至少覆盖：

- package、parameter、localparam、typedef enum、packed struct；
- parameterized interface、interface instance 和 producer/consumer/monitor modport；
- 多层 module hierarchy，固定最大层级深度不低于 6；
- generate-for、generate-if 和命名 generate scope；
- continuous assignment、`always_comb`、`always_ff`、同步/异步控制；
- 嵌套 `if/else`、普通 `case/default`、`casez`，控制嵌套深度不低于 5；
- packed/unpacked array、切片、拼接、归约、移位、算术和条件表达式；
- 多个静态 driver/load 关系、跨 module port 和 interface/modport 边界；
- 可被运行时 FST 观察的时钟、复位、selector、状态、数据和输出信号。

复杂结构必须随规模增加而增加，不能只重复空 module 或注释。生成器同时输出 JSON metadata，
记录实际行数、module/interface/instance 数、最大层级深度、语法特征计数、仿真周期和查询锚点。

### 3.3 仿真合同

- testbench 使用固定 seed 和固定周期数，保证各规模行为可重复。
- 启用 FST tracing，并确保 DesignDB predicate、RHS 和 interface alias 所需信号进入 FST。
- 每个规模必须生成非空 FST、DesignDB C++、DesignDB `.so` 和 bundle manifest。
- 查询锚点必须由 metadata 给出，runner 不根据规模猜测层级名称。

## 四、基准测量设计

### 4.1 阶段拆分

runner 对每个规模独立测量：

| 阶段 | 边界 | 核心指标 |
| --- | --- | --- |
| generate | Python 生成器 | wall time、RTL bytes、实际行数 |
| verilate-build | Verilator 前端及仿真模型 build | wall/user/sys、max RSS、退出码 |
| design-db-build | 私有 G++ 编译共享库 | wall/user/sys、max RSS、`.cpp/.so` bytes |
| simulate | 运行仿真并写 FST | wall/user/sys、max RSS、FST bytes |
| session-open | 打开 FST 和 DesignDB | latency、server RSS 增量 |
| first-resolve | 首次完整层级索引建立 | latency |
| actions | 代表性 trace/waveform action | 首次与后续 latency、结果完整性 |

外部进程计时使用 GNU time 的结构化输出；Python 自身步骤使用 monotonic clock。每条记录包含
工具 revision、工具链版本、CPU 数量、目标/实际 RTL 行数和产物哈希，不包含本机绝对路径。

### 4.2 冷热边界

- 每个规模使用全新 obj_dir，测量一次干净编译；不复用其他规模的生成 C++ 或对象文件。
- 不清除操作系统 page cache，因此编译数据标记为 `clean_build_os_cache_uncontrolled`。
- session 内首次 resolve 单独记录；后续 Action 至少执行 5 次，报告 median、p95、min、max。
- Action 失败、超时、结果不完整或命中 limit 均写入结果，禁止把失败样本丢弃。

### 4.3 查询矩阵

每个规模至少执行：

- session open/close；
- `signal.resolve`：触发首次 hierarchy index；
- `value.at`：单时刻类型化读取；
- `trace.driver`：DesignDB 静态查询；
- `trace.active_driver`：DesignDB predicate 与 FST 运行值组合；
- `trace.active_driver_chain`：跨 module 与 interface/modport 的有界链；
- `signal.changes`：固定物理窗口的变化扫描。

每项校验响应成功、目标信号一致、事实源正确，并保留返回的 complete/limitations 摘要。

## 五、实现结构

计划新增：

```text
tools/benchmark_large_rtl_trace.py       # 统一生成、构建、运行和汇总入口
tools/large_rtl_trace_generator.py       # 确定性复杂 RTL 生成器
tests/test_large_rtl_trace_generator.py  # 快速生成器合同测试，不构建大型 fixture
doc/LARGE_RTL_TRACE_PERFORMANCE_TASKBOOK.md
```

本地输出目录包含每个规模的 RTL、metadata、obj_dir、FST、DesignDB 和结构化阶段日志；目录整体
必须被 `.gitignore` 排除。最终只把去路径化的汇总 JSON/Markdown 结果写入 `doc/`，是否提交
实测报告在完成测量后根据数据稳定性决定。

## 六、阶段与提交策略

### 阶段 0：任务书与基线

- [x] 定位并阅读 xverif 大规模 RTL 生成器。
- [x] 确认当前 patched Verilator 与私有 GCC/G++ 13.3.1 可用。
- [x] 写入本任务书。
- [ ] 建立 goal 与验收标准。

计划提交：`文档：建立大规模 RTL Trace 性能测试任务书`

### 阶段 1：生成器与合同测试

- [ ] 实现按精确目标行数生成的复杂 SystemVerilog。
- [ ] 输出稳定 metadata 与查询锚点。
- [ ] 增加 1K/2K 快速合同测试，检查精确行数、确定性和全部语法特征。
- [ ] 验证所有规模生成结果不包含本机绝对路径。

计划提交：`测试：增加确定性大规模复杂 RTL 生成器`

### 阶段 2：构建和测量 runner

- [ ] 强制解析统一构建产物与私有 GCC/G++，禁止系统 fallback。
- [ ] 分阶段采集 GNU time 指标和产物大小/hash。
- [ ] 创建 DesignDB manifest，运行仿真并校验 FST/DesignDB。
- [ ] 驱动 xdebug-fst session 和代表性 Action，采集冷热延迟。
- [ ] 增加 dry-run、单规模和规模列表参数，便于定向复现。

计划提交：`测试：增加大规模 RTL Trace 分阶段性能基准`

### 阶段 3：1K 到 64K 实测与修复

- [ ] 按 1K→64K 依次执行完整矩阵。
- [ ] 若某规模失败，保存阶段和诊断，不跳过、不 fallback。
- [ ] 修复生成器或 runner 的确定性问题，并重跑受影响规模。
- [ ] 不因脚本变化重建现有 fixture cache。

计划提交：仅在实现需要修复时提交；原始生成物不提交。

### 阶段 4：结果分析与验收

- [ ] 汇总各阶段 wall time、max RSS、产物大小和相邻规模倍率。
- [ ] 区分源码规模、展开实例规模、time-point 数和 FST 变化量对性能的影响。
- [ ] 标记近线性、超线性、固定成本和异常点。
- [ ] 给出当前可接受边界、主要瓶颈和下一步优化建议。
- [ ] 运行生成器单测、静态路径门禁和仓库状态检查。

计划提交：`文档：记录大规模 RTL Trace 性能实测结果`

## 七、验收标准

1. 七个目标规模全部生成，实际 RTL 行数精确等于目标。
2. metadata 证明 interface/modport、层级深度、generate 和嵌套控制均存在并随规模扩展。
3. 七个规模均使用同一 patched Verilator revision 和私有 GCC/G++ 13.3.1。
4. 每个规模都得到非空仿真 executable、FST、DesignDB `.so` 和 manifest。
5. 每个规模都完成 session open 和全部代表性 Action；失败必须有明确、可复现诊断。
6. 汇总报告包含各阶段耗时、峰值 RSS、产物大小、Action 延迟和相邻规模倍率。
7. 不修改或重建既有 fixture cache，不提交生成物或本机绝对路径。
8. 所有新增快速测试通过，当前仓库无无关修改。

## 八、风险与处置

- 64K 复杂 RTL 的 Verilator/G++ 编译可能耗时较长或达到内存限制：保留失败数据并报告，
  不降低语义复杂度、不静默跳过、不改用系统编译器。
- interface/modport 或复杂语法可能触发 patched Verilator 已知边界：先最小化失败，再决定是否
  修生成器或记录真实工具限制；不私自改走不含 interface 的 fallback。
- FST 过大：使用固定且较短的仿真周期，规模主要由静态设计复杂度驱动；仍保留足够变化来
  测量 trace，不能用零活动设计取巧。
- 单次机器负载会影响数据：记录 CPU 数、每阶段 user/sys/wall 和重复 Action 分布，不伪造
  统计显著性。

## 九、进度记录

- 2026-08-19：完成 xverif 生成器调研、工具链核验和任务书初稿；尚未生成或构建任何大型 RTL。
