# xdebug-fst 大规模 RTL Trace 性能实测报告

## 一、结论

1K、2K、4K、8K、16K、32K、64K 七档复杂 SystemVerilog 全部完成 Verilator 前端、
仿真模型构建、DesignDB 构建、FST 仿真、session open 和代表性 trace Action。所有规模都
使用同一 patched Verilator、Wellen、xdebug-fst 以及私有 GCC/G++ 13.3.1，没有 fallback，
没有重建现有 fixture cache。

主要结论如下：

- FST 文件大小随 RTL 规模接近线性增长，64K 时约 2.48 MiB；对固定单信号执行
  `signal.changes` 的中位延迟始终约 4–6 ms。因此本次场景中，原始 FST 的局部读取不是首要
  瓶颈。
- DesignDB 信号数从 887 增长到 63,771，接近线性；但生成的 DesignDB C++ 和 `.so` 从
  16K 开始超线性膨胀。64K 的 C++ 为 196.23 MiB，`.so` 为 330.60 MiB，私有 G++ 编译峰值
  RSS 为 6.25 GiB。
- Verilator 前端是最大的构建时间瓶颈。16K、32K、64K 分别为 68.41 s、433.95 s、
  1769.09 s；RTL 只翻倍，耗时分别增长 6.34 倍和 4.08 倍。
- 首次 `value.at` 从约 61 ms 增长到 2821 ms，符合当前 Wellen backend 首次按名称访问时
  建立完整 hierarchy/signal index 的实现特征。
- 单个目标的 `trace.active_driver_chain` 中位延迟从 8.07 ms 增长到 5338.42 ms，远快于
  信号数增长；这是最明显的运行时扩展性问题。根据当前实现可推断，interface/modport 跨边界
  连接选择仍包含与全设计静态表规模相关的扫描，但本次基准只证明症状，尚未用 profiler
  定位到具体循环。
- `signal.resolve` 保持在 8–11 ms 首次延迟，普通 `trace.active_driver` 中位延迟到 64K 时
  为 18.18 ms，说明静态名称查找和局部 predicate 求值本身仍可控。

因此，大型设计的首要风险不是“Wellen 是否能打开 FST”，而是 DesignDB emitter/产物规模、
DesignDB 原生编译内存，以及跨 interface 的 active-driver chain 查询复杂度。

## 二、测试设计

### 2.1 规模与语义

七档生成 RTL 的实际行数精确等于目标：

```text
1024, 2048, 4096, 8192, 16384, 32768, 65536
```

每档都包含：

- package、parameter、enum、packed struct 和 function；
- parameterized interface 及 producer/consumer/monitor modport；
- 六级 chain module，最大展开 hierarchy 深度为 7；
- generate-for、generate-if 和命名 generate scope；
- `always_comb`、`always_ff`、continuous assignment；
- 五层嵌套 `if/else`、普通 `case/default` 和 `casez`；
- packed/unpacked array、切片、拼接、移位、算术和归约；
- 跨 module port、interface/modport 的静态 driver/load 路径；
- 固定 48 个仿真 step 和确定性输入。

源码尾部不足一个完整 semantic tile 时使用实际 `localparam` 声明补齐，没有使用大段注释
伪造行数。生成器 metadata 同时记录 tile 数、最小展开 instance 数、语法特征和查询锚点。

### 2.2 工具和测量边界

- CPU 并行数：8；仿真模型构建使用 `make -j8`。
- GCC/G++：13.3.1，来自 xdebug 私有工具链。
- Verilator：`5.051 devel` patched build。
- xdebug-fst revision：`ed103938d7afb037f15e5ff6a2cc8f70d070d22e`。
- 操作系统 page cache 未清理，因此模式标记为 `clean_build_os_cache_uncontrolled`。
- 每档使用独立全新 obj_dir；Action 在同一 session 内执行 5 次。
- GNU time 分别测量 Verilator 前端、仿真模型构建、DesignDB G++ 和仿真。
- managed session 使用短临时 HOME，避免输出目录长度影响 UDS `sockaddr_un` 上限；该调整只
  缩短控制面 socket 路径，不改变 FST、DesignDB、Action 或被测产物。

## 三、规模合同

| RTL 行数 | Semantic tiles | 最小展开 instances | DesignDB signals | RTL bytes |
| ---: | ---: | ---: | ---: | ---: |
| 1,024 | 11 | 89 | 887 | 36,527 |
| 2,048 | 23 | 185 | 1,835 | 74,099 |
| 4,096 | 48 | 385 | 3,810 | 147,510 |
| 8,192 | 99 | 793 | 7,839 | 292,599 |
| 16,384 | 200 | 1,601 | 15,818 | 584,813 |
| 32,768 | 402 | 3,217 | 31,776 | 1,169,241 |
| 65,536 | 807 | 6,457 | 63,771 | 2,336,367 |

## 四、构建与产物

### 4.1 Wall time

| RTL 行数 | Verilator 前端(s) | 仿真模型构建(s) | DesignDB G++(s) | 仿真/FST(s) |
| ---: | ---: | ---: | ---: | ---: |
| 1,024 | 0.30 | 10.70 | 0.43 | 0.14 |
| 2,048 | 0.83 | 11.55 | 0.80 | 0.02 |
| 4,096 | 2.37 | 17.66 | 1.22 | 0.02 |
| 8,192 | 10.17 | 22.76 | 2.98 | 0.05 |
| 16,384 | 68.41 | 38.29 | 9.15 | 0.08 |
| 32,768 | 433.95 | 73.11 | 29.67 | 0.20 |
| 65,536 | 1769.09 | 157.60 | 170.38 | 0.32 |

Verilator 前端相邻翻倍倍率依次为 2.77、2.86、4.29、6.73、6.34、4.08。8K 之后已经明显
偏离线性。DesignDB G++ 在 32K→64K 增长 5.74 倍，也出现明显超线性。

### 4.2 峰值 RSS

| RTL 行数 | Verilator 前端(KiB) | 仿真模型构建(KiB) | DesignDB G++(KiB) | 仿真(KiB) |
| ---: | ---: | ---: | ---: | ---: |
| 1,024 | 21,124 | 298,172 | 44,140 | 4,836 |
| 2,048 | 27,784 | 298,388 | 60,920 | 5,432 |
| 4,096 | 42,028 | 298,208 | 109,560 | 6,364 |
| 8,192 | 73,572 | 310,776 | 223,308 | 8,000 |
| 16,384 | 150,508 | 343,800 | 573,244 | 11,528 |
| 32,768 | 365,956 | 537,452 | 1,817,476 | 19,168 |
| 65,536 | 1,079,360 | 604,864 | 6,553,748 | 34,280 |

64K 的 DesignDB 编译峰值约 6.25 GiB，是本轮最高内存阶段。Verilator 前端峰值约
1.03 GiB；仿真模型构建虽然使用 8 并行，但 GNU time 报告的最大单进程 RSS 约 591 MiB。

### 4.3 产物大小

| RTL 行数 | DesignDB C++ bytes | DesignDB `.so` bytes | Simulator bytes | FST bytes |
| ---: | ---: | ---: | ---: | ---: |
| 1,024 | 564,103 | 827,952 | 437,400 | 37,701 |
| 2,048 | 1,247,769 | 1,827,376 | 555,696 | 77,243 |
| 4,096 | 2,909,454 | 4,321,840 | 933,648 | 157,983 |
| 8,192 | 7,243,992 | 11,149,872 | 1,543,952 | 322,886 |
| 16,384 | 19,824,169 | 31,547,952 | 2,776,008 | 650,231 |
| 32,768 | 60,721,545 | 99,762,736 | 5,220,208 | 1,301,210 |
| 65,536 | 205,761,726 | 346,661,424 | 10,292,888 | 2,599,864 |

FST 每次翻倍约增长 2.00–2.05 倍。DesignDB C++ 和 `.so` 增长更快，说明静态表发射规模
不只由 signal 数决定；driver、load、predicate 和跨端口连接表共同放大产物。

## 五、运行时性能

### 5.1 Session 与内存

| RTL 行数 | session.open(ms) | server RSS after open(KiB) |
| ---: | ---: | ---: |
| 1,024 | 51.453 | 7,012 |
| 2,048 | 44.278 | 7,832 |
| 4,096 | 51.834 | 10,864 |
| 8,192 | 47.738 | 17,296 |
| 16,384 | 54.238 | 37,572 |
| 32,768 | 125.959 | 105,772 |
| 65,536 | 287.800 | 348,428 |

session.open 在 16K 前基本保持 44–54 ms，32K 和 64K 才随 DesignDB 装载规模明显上升。
server RSS 与 DesignDB `.so` 大小高度相关，64K 打开后约 340 MiB。

### 5.2 Action 延迟

下表为同一 session 内 5 次调用的中位延迟；“首次 value”单列，因为它包含 Wellen 完整信号
名称索引首次建立成本。

| RTL 行数 | resolve(ms) | 首次 value(ms) | 后续 value(ms) | driver(ms) | active-driver(ms) | active-chain(ms) | changes(ms) |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,024 | 4.270 | 60.959 | 7.985 | 4.637 | 2.037 | 8.073 | 4.667 |
| 2,048 | 1.404 | 63.676 | 4.433 | 3.924 | 3.711 | 12.307 | 5.174 |
| 4,096 | 1.026 | 59.168 | 2.784 | 2.318 | 2.384 | 29.784 | 5.761 |
| 8,192 | 1.939 | 105.999 | 2.442 | 3.024 | 3.648 | 92.425 | 5.267 |
| 16,384 | 1.048 | 235.694 | 3.066 | 6.604 | 6.873 | 348.091 | 4.200 |
| 32,768 | 0.963 | 761.728 | 4.523 | 10.250 | 10.566 | 1309.525 | 4.034 |
| 65,536 | 1.496 | 2821.278 | 3.899 | 22.677 | 18.184 | 5338.420 | 5.103 |

说明：`signal.resolve` 表中使用 5 次调用的中位数；首次调用实际为 8.11–11.27 ms，仍未随
规模明显恶化。所有 Action 响应均 `ok=true`；提供完整性字段的响应全部
`analysis_complete=true`、`response_truncated=false`。

## 六、瓶颈判断

### 6.1 已由数据证明

1. Verilator `--design-db` 路径在复杂 RTL 上存在强超线性前端时间增长。
2. 当前“巨大 C++ → G++ → `.so`”的 DesignDB 载体在 64K 已需要 6.25 GiB 编译内存。
3. Wellen 首次全 hierarchy 名称索引建立随设计规模显著增长，但建立后局部值查询恢复到
   约 3–8 ms。
4. interface/modport active-driver chain 即使只查询固定第一个 tile，仍从 8 ms 增长到
   5.34 s，存在与全设计规模耦合的运行时算法。
5. 固定信号、固定变化数量的 `signal.changes` 没有随全设计规模增长，说明按需信号加载路径
   在本场景有效。

### 6.2 尚不能由本基准证明

- 本次仿真只有 48 个时间点，不能代表长时间、高变化率 FST；因此不能据此断言 GB 级 FST
  的 time table 和高频信号加载没有问题。
- 本次每个 tile 的接口和链结构同构，不能替代真实 SoC 中不均匀 fanout、多时钟、超宽总线
  和复杂 generate hierarchy。
- 尚未采集 Verilator/DesignDB emitter profiler，因此不能把前端超线性归因到某一个函数。
- 尚未采集 active-chain profiler，因此“跨连接扫描”只是基于症状和实现结构的待验证推断。

## 七、建议优先级

1. 对 16K/32K 运行 Verilator profiler，拆分普通 elaboration、DesignDB signal/driver/load/
   predicate/port 表收集与 C++ 发射时间。
2. 为 DesignDB emitter 增加表项计数和各表字节统计，确认 32K→64K 的膨胀来自哪类记录。
3. 对 `trace.active_driver_chain` 做采样 profiler，并检查 port connection、flattened output、
   alias 选择是否每 hop 扫描全设计。
4. 把 Wellen 名称索引建立从首次 `value.at` 隐式成本改成可观测阶段，进一步评估原生索引或
   分层懒加载。
5. 若超大设计是正式目标，评估将 DesignDB 从生成 C++/`.so` 迁移到版本化紧凑二进制加
   mmap；保留小型 C ABI loader，而不是让 G++ 编译数百 MiB 静态初始化代码。
6. 另建长时间/高变化率矩阵，独立测量 time point、单信号 change count 和被加载信号数，
   不把本轮静态规模结论外推到全部 FST 工作负载。

## 八、证据与复现

- 结构化原始结果：本地 benchmark 输出目录中的 `benchmark-results.json`，SHA-256：
  `76be874293ca6e33bee6775aa7535c5c43e617c60104842de7330c8e60dd9682`。
- 本地自动 Markdown：`benchmark-results.md`，SHA-256：
  `86e29b1e383dc3c7d96b69bc8108d08539fe379217bdfc66b4dcf3394945fbe6`。
- 生成物、obj_dir、FST、DesignDB 和原始日志未提交 Git。
- 复现入口：`tools/benchmark_large_rtl_trace.py`；调用者必须显式提供新的绝对输出目录。
