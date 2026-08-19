# xdebug-fst Trace 多实现路线性能报告

## 一、结论

推荐路线是：Verilator 单次前端直接生成版本化 `binary-v1` DesignDB，xdebug-fst 以
`mmap` 只读打开，在 session 建立小型反向索引，同时把 Wellen 全量名称索引建立后的 miss
视为确定 miss。

这条路线不需要为 trace 再编译一次巨型 DesignDB C++：仿真模型仍只构建一次，静态 trace
事实随同一次 Verilator 前端直接写入 `.xddb`。64K 的旧 `.so` 是 346,661,424 bytes，紧凑
二进制是 78,964,953 bytes；旧 DesignDB G++ 阶段需要 170.38 s 和 6,553,748 KiB 峰值
RSS，新路线完全删除该阶段。

运行时最大的收益并非来自存储格式本身。`perf task-clock` 证明，优化前 64K active-chain
有 78.57% 时间落在 Wellen hierarchy recursive iterator，另有 16.45% 在
`wellen_scope_at`。原因是首次名称索引建立后，每个不存在于 FST 的 DesignDB 临时信号仍
触发一次完整 hierarchy 重扫。删除这次冗余重扫后，同一 64K 查询由 5,338.420 ms 降到
约 37～41 ms；`.so` 与 binary 后端查询延迟接近，说明二进制的主要价值是构建资源、产物
和 session 常驻内存。

## 二、比较边界

- RTL：同一确定性复杂 SystemVerilog 生成器，1K、2K、4K、8K、16K、32K、64K 七档；
  包含 package、enum、packed struct、function、parameterized interface/modport、六级
  hierarchy、generate、always_comb/always_ff、五层条件、case/casez、packed/unpacked
  array、切片和跨 module/interface trace。
- 依赖：锁定官方 Verilator revision `c3be3c80501bec8d4c3b7f5256890332df62d410`
  和 Wellen revision `34f40595c4330bcac364397679cdc3d5ae6b5c0c`；修改仅以本仓库 patch
  保存。
- 编译器：只使用 xdebug_oc 私有 GCC/G++ 13.3.1；构建脚本遇到版本或真实路径不匹配时
  直接失败，不回退系统工具链。
- 测量：GNU time 分阶段记录 wall/user/sys/max RSS；每个 Action 在同一 session 调用 5 次，
  记录首次、median、p95、min、max。page cache 和共享主机负载未控制，因此小幅 wall-time
  差异不作因果解释。
- 产物：每种路线使用独立临时目录；原始 RTL、obj_dir、FST、DesignDB、perf data 和日志
  均不提交 Git。

## 三、方案 A：C++ 静态表与编译策略

64K 使用同一份 205,761,726-byte `__DesignDb.cpp`：

| G++ 策略 | 编译 wall(s) | 峰值 RSS(KiB) | `.so` bytes |
| --- | ---: | ---: | ---: |
| `-O0` | 115.98 | 6,554,892 | 346,661,648 |
| `-O2` | 170.38 | 6,553,748 | 346,661,424 |
| `-Os` | 113.66 | 6,555,372 | 346,354,224 |

`-O0/-Os` 比 `-O2` 快约 32%，但三者都需要约 6.25 GiB 峰值 RSS，产物也都约 330 MiB。
调整优化级别只能缩短一部分 wall time，不能解决内存和产物膨胀，因此不作为推荐路线。
三种库会再用最终 xdebug-fst 对同一 64K FST 复测 session 与六类 Action；它们发布相同静态
表，预期运行差异只反映共享库代码生成策略，不改变事实语义。

最终同 FST 复测如下；`-O2` 首轮 open 1,263.549 ms 为主机抖动，立即复测恢复到表中数值：

| G++ 策略 | open(ms) | session RSS(KiB) | index(ms) | value first/median(ms) | active-chain median(ms) |
| --- | ---: | ---: | ---: | ---: | ---: |
| `-O0` | 321.074 | 362,204 | 45.514 | 3089.567 / 4.127 | 43.594 |
| `-O2` | 319.691 | 362,388 | 54.019 | 3341.722 / 11.939 | 55.301 |
| `-Os` | 325.403 | 362,016 | 56.161 | 3330.208 / 26.768 | 42.748 |

三者的 open/RSS 和 active-chain 位于同一量级，运行抖动大于优化级别带来的稳定差异；因此
编译策略只按构建成本筛选，不能用这些 Action 样本声称某一优化级别运行更快。

## 四、方案 B：紧凑二进制与 mmap

### 4.1 格式

`binary-v1` 是固定小端格式：176-byte header 包含 magic、major/minor、endianness、文件总长
和九个 section 的 offset/count。section 依次为 signals、name index、drivers、driver
starts、loads、load starts、ports、port starts 和去重字符串池。

reader 在 mmap 后先完整验证：

- schema、endianness、header/file size；
- offset/count 乘法溢出、8-byte 对齐、文件范围和 section 重叠；
- 字符串 offset 与 NUL 终止；
- signal/关系 index 范围；
- name index 严格排序且对 signal 构成一一映射；
- starts 首尾、单调性及每条关系与所属 group 的一致性；
- 所有对外返回 `int` 的 record count 都不超过可表达范围。

binary bundle 使用严格 manifest：

```json
{
  "database": "Vdesign__DesignDb.xddb",
  "format": "binary-v1",
  "schema_version": "xdebug.design-db-bundle.v2"
}
```

旧 `.so` bundle 继续使用严格 v1。两者均检查声明扩展名、bundle 内 canonical path 和普通文件；
不根据扩展名猜测，也不在打开失败时换后端。

### 4.2 转换原型

先从同一 ABI-v2 `.so` 逐项读取并生成 binary，以隔离 reader/格式语义：

| RTL | 转换 wall(s) | 转换峰值 RSS(KiB) | binary bytes |
| ---: | ---: | ---: | ---: |
| 1K | 0.16 | 20,648 | 239,139 |
| 64K | 21.75 | 689,424 | 78,964,953 |

转换原型证明格式可行，但它仍依赖先生成和编译 `.so`，所以不进入最终路线。

### 4.3 Verilator 直接生成

本仓库 patch 增加显式 `--design-db-binary`。它与旧 `--design-db` 互斥：前者只生成 `.xddb`，
后者只生成 C++，从而保持旧调用方行为且避免重复产物。

初版 binary emitter 为按 signal 分组复制完整 driver/load/port 对象，并在内存拼接整个输出。
32K Verilator 峰值为 435,292 KiB，64K 为 1,203,992 KiB。最终 emitter 改为只重排指针并
把 header/section 流式写盘；32K 峰值降至 331,808 KiB（-23.8%），输出 SHA-256 不变。

最终七档结果：

| RTL | Verilator wall(s) | Verilator RSS(KiB) | binary bytes | 独立 DesignDB 编译(s/RSS) |
| ---: | ---: | ---: | ---: | ---: |
| 1K | 0.32 | 21,204 | 239,139 | 0 / 0 |
| 2K | 0.61 | 27,944 | 522,771 | 0 / 0 |
| 4K | 2.42 | 42,112 | 1,202,463 | 0 / 0 |
| 8K | 10.58 | 70,632 | 2,961,155 | 0 / 0 |
| 16K | 91.12 | 142,060 | 7,918,023 | 0 / 0 |
| 32K | 469.51 | 331,808 | 23,707,555 | 0 / 0 |
| 64K | 2077.90 | 960,956 | 78,964,953 | 0 / 0 |

最终 64K 峰值比初版 binary emitter 低 20.2%，比旧 C++ emitter 的 1,079,360 KiB 低
11.0%。受共享主机负载影响，最终 Verilator wall time 比旧测量更长；可确定的端到端改进是
删除了后续 170.38 s / 6.25 GiB 的 DesignDB G++ 阶段，不能用两次不同负载下的前端 wall
差值继续宣称额外加速。

仿真数据面保持独立，最终各档如下：

| RTL | 仿真模型构建 wall(s) / RSS(KiB) | 仿真 wall(s) / RSS(KiB) | FST bytes |
| ---: | ---: | ---: | ---: |
| 1K | 11.19 / 298,436 | 0.02 / 4,732 | 37,701 |
| 2K | 11.43 / 298,128 | 0.02 / 5,240 | 77,243 |
| 4K | 17.00 / 298,432 | 0.03 / 6,308 | 157,983 |
| 8K | 23.42 / 310,688 | 0.04 / 7,932 | 322,886 |
| 16K | 39.39 / 344,532 | 0.16 / 11,548 | 650,231 |
| 32K | 87.70 / 536,632 | 0.18 / 19,124 | 1,301,210 |
| 64K | 173.18 / 601,988 | 0.32 / 34,404 | 2,599,864 |

## 五、方案 C：session 查询索引

`DesignQueryIndex` 在打开 DesignDB 后单次扫描静态事实，建立：

- connected signal → 按方向分类的 port signals；
- scope → input/output/inout ports；
- selector base → 最大已物化 numeric selector；
- port signal ↔ connected signal 邻接表。

它替换 active trace 中的反向端口、scope port、selector 和 predicate 邻接全表扫描。索引构建
记录实际扫描的 signal/port record 数，并报告容器 payload 的保守估算字节；估算不把 allocator
和 hash bucket 元数据冒充精确 heap usage。

| RTL | index build(ms) | 估算 bytes | signals scanned | port records scanned |
| ---: | ---: | ---: | ---: | ---: |
| 1K | 0.649 | 62,641 | 887 | 650 |
| 2K | 0.941 | 128,881 | 1,835 | 1,346 |
| 4K | 5.295 | 266,689 | 3,810 | 2,796 |
| 8K | 4.122 | 547,633 | 7,839 | 5,754 |
| 16K | 13.100 | 1,104,193 | 15,818 | 11,612 |
| 32K | 22.869 | 2,217,313 | 31,776 | 23,328 |
| 64K | 52.055 | 4,448,881 | 63,771 | 46,818 |

单独加入这些索引时，64K active-chain 由 5,238.850 ms 变为 5,163.436 ms，只有小幅收益；
它消除了已知 O(N) consumer 扫描，但不是 5 秒瓶颈的主因。索引的价值是固定查询复杂度，
而非把 profile 中的 Wellen 重扫收益归到自己名下。

## 六、Wellen miss 路径与热点归因

对 64K active-chain 使用同一 `perf record -e task-clock -g`：共 21K samples，无 lost
samples。优化前 self overhead：

| Symbol | self overhead |
| --- | ---: |
| Wellen hierarchy recursive iterator `next` | 78.57% |
| `wellen_scope_at` | 16.45% |
| `wellen_var_full_name` | 1.09% |
| `WellenFstBackend::normalize_path` | 0.23% |
| `WellenFstBackend::find_signal` 本体 | 0.07% |

`build_signal_index()` 已遍历每个 scope/var，并建立 exact/local/packed-selection 索引。索引 miss
以后再次遍历同一 hierarchy 不可能发现新变量；active trace 又会查询多个只存在于 DesignDB、
不出现在 FST 的中间临时信号，于是每 hop 重复付出全设计成本。最终实现把 exact 和 packed
selection 都失败后的 miss 直接返回。

64K 分离对比：

| DesignDB / 查询实现 | session.open(ms) | session RSS(KiB) | active-chain median(ms) |
| --- | ---: | ---: | ---: |
| `.so` 旧查询 | 287.800 | 348,428 | 5,338.420 |
| binary，无 session 索引 | 180.410 | 99,664 | 5,238.850 |
| binary，加 session 索引 | 196.063 | 103,264 | 5,163.436 |
| `.so` + 索引 + definitive miss | 333.929 | 363,552 | 37.461 |
| binary + 索引 + definitive miss | 203.596 | 103,240 | 36.675 |
| 最终 binary 完整矩阵 | 199.712 | 102,024 | 40.907 |

前三行证明换存储和加反向索引都没有掩盖主瓶颈；后两行证明 definitive miss 才是约 145 倍
收益来源。`.so` 与 binary 的最终查询时间基本相同，也再次区分了存储收益和查询收益。

## 七、最终 session 与 Action 矩阵

下表均为最终 binary + session 索引 + definitive miss。`value first` 包含 Wellen 首次完整名称
索引；其余是 5 次中位数。

| RTL | open(ms) | RSS(KiB) | resolve(ms) | value first/median(ms) | driver(ms) | active(ms) | chain(ms) | changes(ms) |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1K | 78.742 | 6,460 | 2.441 | 48.793 / 2.758 | 1.942 | 3.003 | 50.381 | 19.476 |
| 2K | 33.857 | 7,576 | 1.822 | 59.899 / 2.824 | 2.469 | 3.814 | 6.145 | 3.914 |
| 4K | 56.892 | 8,616 | 1.536 | 58.513 / 2.339 | 2.760 | 2.710 | 6.770 | 5.698 |
| 8K | 39.173 | 11,668 | 1.341 | 100.376 / 2.497 | 3.625 | 3.676 | 8.002 | 3.998 |
| 16K | 74.124 | 18,836 | 1.303 | 273.995 / 2.535 | 5.650 | 5.998 | 12.683 | 3.709 |
| 32K | 133.841 | 38,784 | 1.391 | 835.616 / 3.166 | 10.161 | 10.300 | 21.591 | 5.035 |
| 64K | 199.712 | 102,024 | 1.050 | 2931.398 / 5.036 | 21.070 | 19.270 | 40.907 | 4.230 |

1K 的 chain/changes 受到共享主机短时负载影响，显著高于相邻规模；响应合同和后续档位正常，
因此保留原始值，不删除异常样本。最终结论使用 64K、独立前后对照和 profile，而不是用该
1K 抖动推导复杂度。

64K 五次分布用于保留 p95/min/max 证据：

| Action | first(ms) | median(ms) | p95(ms) | min(ms) | max(ms) |
| --- | ---: | ---: | ---: | ---: | ---: |
| `signal.resolve` | 8.971 | 1.050 | 8.971 | 0.878 | 8.971 |
| `value.at` | 2931.398 | 5.036 | 2931.398 | 4.099 | 2931.398 |
| `trace.driver` | 29.807 | 21.070 | 29.807 | 16.168 | 29.807 |
| `trace.active_driver` | 30.169 | 19.270 | 30.169 | 18.119 | 30.169 |
| `trace.active_driver_chain` | 62.061 | 40.907 | 62.061 | 37.112 | 62.061 |
| `signal.changes` | 23.427 | 4.230 | 23.427 | 3.937 | 23.427 |

## 八、语义与失败关闭

- 最终 1K legacy `.so` 与 direct `.xddb` 完整 parity：887 signals、2,585 drivers、
  2,515 loads、650 ports；逐 signal 比较名称、类型、宽度、文件、行、方向，逐关系比较内容
  与顺序，并比较 name index 和所有名称/缺失名称 resolve 结果。canonical SHA-256 为
  `1fd2e3678c8cebcb76faf82fc3c05d1ad41f31d7eb9a22c4875bd2c3d2943eb5`。
- backend Action parity 使用 GCD fixture 自动比较 `.so`/binary 的六类代表请求 response
  `summary`、`data` 和 `limitations`；最终七档 binary 矩阵中的同类请求均 `ok=true`，带完整性
  字段者均 analysis/scan complete 且未截断。
- 自动损坏用例覆盖截断 header、不兼容 major、section overlap、字符串 offset 越界、未终止
  字符串、重复 name-index target 和 driver group owner 不一致；全部在 session start 失败
  关闭。
- binary manifest 指向伪 `.so` 时按声明格式拒绝，不尝试 dlopen；旧 v1 `.so` bundle 回归
  保持兼容。

## 九、建议与剩余边界

1. 默认采用 `--design-db-binary`，删除 trace 专用 DesignDB C++/.so 编译；保留旧
   `--design-db` 作为兼容路径，不让两者同时生成。
2. 保留 session 反向索引。其 64K payload 只有数 MiB，构建几十毫秒，能把已知 consumer
   扫描从查询热路径移出；但报告中不把它宣传成 145 倍优化来源。
3. 保留 Wellen definitive miss。完整信号索引和 packed-selection 查询后再重扫 hierarchy
   没有语义增益，profile 已直接证明它是主热点。
4. 64K 之后仍需关注 Verilator 前端超线性时间；本轮消除了第二次巨型 G++ 编译，但没有把
   elaboration/静态事实收集变成线性算法。
5. binary-v1 字符串 offset 为 32 bit。当前 64K 字符串池远低于上限；面向极端 SoC 前应给
   producer 增加明确的 4 GiB 容量错误，或设计 binary-v2 64-bit offset，不能静默截断。
6. 本轮 FST 只有固定 48 step，不能外推到 GB 级长时间、高切换率波形；该问题应使用独立的
   time-point/change-count 矩阵，不与静态 RTL 规模混为一谈。

## 十、证据与仓库卫生

- 旧完整 `.so -O2` 结果 SHA-256：
  `76be874293ca6e33bee6775aa7535c5c43e617c60104842de7330c8e60dd9682`。
- direct binary 初版完整矩阵 SHA-256：
  `d6b947da282d129aa4caaccd262b6d520447445656f0713b41f9e76fe364f79c`。
- 最终 optimized binary 完整矩阵 SHA-256：
  `366517c98d045300162480a905526caac11bfba5ae94fa9afc7ced6d7ddb4a3d`。
- patch SHA-256 与 `dependencies.lock.json` 一致，构建输出证明官方 revision、tree、patch
  fingerprint 和私有 GCC/G++ 13.3.1。
- `testdata/` 无 diff；没有重建或提交 fixture cache。生成 benchmark 产物只在临时目录。
- 最终验收中 CTest 7/7、定向/全量 pytest 和 Verilator 14 个 `t_xdd*` 均通过；patch
  SHA-256 为 `226de48a441d2b7983dee74da385a91b5af3f45a22d5a6d8e4e15c7afa30f64a`，
  与 lock 一致。七档优化前后 `.xddb` 逐字节一致；绝对路径门禁和 `git diff --check` 通过。
