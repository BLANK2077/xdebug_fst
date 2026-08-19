# xdebug-fst 项目交接文档

> **当前 Goal 的不可漂移范围（2026-08-10）**：本文件包含早期探索记录，其中关于 VCD/GHW 输入、VCD fixture 或 fallback 的描述不再代表实施方案。项目只需要并且也必须完整适配 FST 波形；生产、测试、差分和验收只允许 Wellen 在当前 session 中直接按需读取原始 `.fst`。禁止将 FST 转成 VCD、JSON、私有索引、离线数据库或全量内存快照后分析。Verilator DesignDB 仅提供静态设计事实，显式 export 仅是最终输出且不得回灌。权威范围以 [`doc/XDEBUG_FULL_PARITY_TASKBOOK.md`](doc/XDEBUG_FULL_PARITY_TASKBOOK.md) 为准。

> 作者: BLANK2077 + Claude Code (Claude Fable 5)
> 日期: 2026-08-04
> 版本: 1.0

---

## 目录

1. [项目目标](#1-项目目标)
2. [整体架构](#2-整体架构)
3. [Wellen 探索与实验](#3-wellen-探索与实验)
4. [Verilator --design-db 修改](#4-verilator---design-db-修改)
5. [xdebug 代码分析](#5-xdebug-代码分析)
6. [实施计划](#6-实施计划)
7. [当前进度](#7-当前进度)
8. [关键实现细节](#8-关键实现细节)
9. [待解决事项](#9-待解决事项)
10. [附录：文件清单](#10-附录文件清单)

---

## 1. 项目目标

构建 **xdebug-fst**——xdebug 的开源等价实现。用 FST (Fast Signal Trace) 波形格式替代 Synopsys FSDB，用 Verilator `--design-db` 生成的静态设计数据库替代 NPI (Novas Programming Interface) 设计查询。最终实现：

- **二进制级兼容**：`xdebug-fst` 与 `xdebug` 具有相同的 CLI 接口 (`--json -`, `--stdio-loop --json`, `--server`)
- **协议级兼容**：接受并返回相同格式的 JSON 请求/响应
- **语义级兼容**：所有 waveform action 返回与 xdebug 语义等价的结果
- **零侵入**：不修改 `${XDEBUG_ORIGINAL_ROOT}` 的任何文件。通过 `XDEBUG_BIN` 环境变量切换后端

### 许可证

| 组件 | 许可证 |
|------|--------|
| wellen (Rust FST 解析) | BSD-3-Clause |
| wellen_capi (C FFI) | BSD-3-Clause |
| xdebug-fst (C++ 引擎) | BSD-3-Clause |
| fst-reader (Rust crate) | BSD-3-Clause |
| nlohmann/json (C++ JSON) | MIT |
| Verilator --design-db 生成的 .so | LGPL-3.0 / Artistic-2.0 (动态链接无传染性) |

---

## 2. 整体架构

```
┌──────────────────────────────────────────────────────────────┐
│                xverif MCP Server (不变，不修改)                │
│  adapters/xdebug.py → McpSessionManager                      │
│    └─ 通过 XDEBUG_BIN 环境变量指向二进制文件                    │
│    └─ target_key="fsdb" 不变，值改为指向 .fst 文件             │
└──────────────────────┬───────────────────────────────────────┘
                       │ stdio-loop JSONL 协议 & one-shot JSON
           ┌───────────┴───────────────┐
           │                           │
    ┌──────┴──────┐            ┌──────┴──────────────┐
    │   xdebug    │            │     xdebug-fst       │
    │ (C++, NPI)  │            │  (C++, ${REPO_ROOT}/../  │
    │  不变/不动   │            │    xdebug_fst/)      │
    │             │            │                      │
    │ npi_fsdb_*  │            │  ┌─────────────────┐ │
    │ npi_hdl_*   │            │  │ IWaveformBackend │ │
    │             │            │  │ IDesignBackend   │ │
    └─────────────┘            │  └───┬─────────┬───┘ │
                               │      │         │     │
                               │  wellen_capi  xdd_api│
                               │  (libwellen_  (libV  │
                               │   capi.so)    __De-  │
                               │      │    signDb.so) │
                               │  wellen (Rust)       │
                               │  fst-reader          │
                               └──────────────────────┘
```

### 工作目录结构

```
${REPO_ROOT}/../                          ← xdebug 开源化 monorepo
├── wellen/                           ← Wellen Rust workspace (已有, BSD-3)
│   ├── Cargo.toml                    ← 成员: wellen, pywellen, wellen_capi
│   ├── wellen/src/                   ← ~16,000行 Rust (VCD/FST/GHW解析)
│   ├── wellen_capi/                  ← 【新建】C FFI (Rust → C)
│   │   ├── Cargo.toml
│   │   ├── src/lib.rs               ← ~580行, 17个extern "C"函数
│   │   ├── include/wellen_capi.h    ← C/C++ 头文件
│   │   └── test/test_capi.c         ← C 验证测试
│   └── wellen/examples/
│       ├── clock_edge_experiment.rs  ← 时钟沿检测实验
│       └── clock_coincidence.rs      ← 全局信号同步性分析
│
├── verilator/                        ← Verilator (已有, LGPL/Artistic)
│   └── feature/design-db-for-xdebug  ← 【分支】新增 --design-db 功能
│       ├── include/xdd_api.h         ← DesignDB C API 定义
│       ├── src/V3EmitDesignDb.cpp    ← AST→C++ 代码生成
│       ├── src/V3EmitDesignDb.h
│       └── test_regress/t/           ← 测试: t_xdd_*.v, validate_trace.c
│
├── lz4/                              ← LZ4 (已有)
│
└── xdebug_fst/                       ← 【新建】xdebug-fst 源码仓库 (git)
    ├── PLAN.md                       ← 完整实施计划
    ├── HANDOFF.md                    ← 本文档
    ├── .gitignore
    ├── CMakeLists.txt                ← CMake 构建系统
    ├── third_party/
    │   └── nlohmann/                 ← JSON for Modern C++ (MIT, v3.11.2)
    ├── src/
    │   ├── main.cpp                  ← CLI 入口 (--json, --stdio-loop, --server)
    │   ├── server.cpp                ← 服务器/oneshot/stdio-loop 实现
    │   ├── api/
    │   │   └── json_types.h          ← Json = nlohmann::json 类型别名
    │   ├── backend/
    │   │   ├── waveform_backend.h    ← IWaveformBackend 抽象接口
    │   │   ├── design_backend.h      ← IDesignBackend 抽象接口
    │   │   ├── wellen_fst_backend.h  ← Wellen C FFI 实现
    │   │   ├── wellen_fst_backend.cpp
    │   │   ├── xdd_design_backend.h  ← Verilator DesignDB 实现 (dlopen)
    │   │   └── xdd_design_backend.cpp
    │   ├── engine/
    │   │   ├── engine_action_handler.h  ← 抽象 handler 接口
    │   │   ├── engine_globals.h/cpp     ← 全局状态 (waveform + design backend)
    │   │   ├── action_registry.h/cpp    ← Handler 注册表
    │   │   └── actions/
    │   │       ├── register_all.cpp     ← 所有 handler 注册
    │   │       ├── waveform/
    │   │       │   ├── scope_list.cpp     ← scope.list + scope.roots
    │   │       │   └── value_at.cpp       ← value.at + signal.changes
    │   │       ├── design/
    │   │       │   └── signal_resolve.cpp ← signal.resolve + trace.driver/load
    │   │       ├── combined/             ← [待实现]
    │   │       ├── protocol/             ← [待实现]
    │   │       └── stream/               ← [待实现]
    │   ├── waveform/                 ← [待实现]
    │   └── protocol/                 ← [待实现]
    ├── testdata/                     ← [待构建] Verilator 生成的测试 FST
    └── tests/                        ← [待实现] pytest 测试
```

---

## 3. Wellen 探索与实验

### 3.1 Wellen 概述

**Wellen** (`${WELLEN_HOME}/`) 是一个 Rust 波形文件解析库（BSD-3），由 Cornell 大学 Kevin Laeufer 维护。支持 VCD、FST、GHW 三种格式。

**核心数据模型：**

| Rust 类型 | 说明 | C FFI 映射 |
|-----------|------|-----------|
| `ScopeRef(NonZeroU32)` | 层级作用域引用 | `uint32_t` (1-indexed) |
| `VarRef(NonZeroU32)` | 变量引用 | `uint32_t` (1-indexed) |
| `SignalRef(NonZeroU32)` | 波形信号引用 | `uint32_t` (0-indexed) |
| `TimeTableIdx(u32)` | 时间表索引 | `uint32_t` |
| `Time(u64)` | 绝对时间值 | `uint64_t` |
| `DataOffset` | 二分查找结果 | 6 个字段的结构体 |

**关键 API：**

```rust
// 打开文件 (simple::read = read_header + read_body)
let mut wave = wellen::simple::read("dump.fst")?;

// 层级遍历
let h = wave.hierarchy();
for scope in h.all_scopes() {
    let name = h[scope].name(h);           // 本地名
    let full  = h[scope].full_name(h);     // 全路径名
    for var in h[scope].vars(h) {
        let signal_ref = h[var].signal_ref();
        let encoding   = h[var].signal_encoding(h);  // BitVector(w)/Real/String
    }
}

// 信号按需加载 (FST: 只读文件中相关section)
let refs = vec![clk_ref, data_ref];
wave.load_signals(&refs);

// 二分查找: O(log N)
let clk_signal = wave.get_signal(clk_ref).unwrap();
let offset = clk_signal.get_offset(time_idx).unwrap();
// offset.time_match: bool  -- 信号是否在此精确时刻变化
// offset.next_index: Option<NonZeroTimeTableIdx> -- 下一次变化的索引
// offset.start: usize -- data 数组中的偏移
// offset.elements: u16 -- 同一时刻的值数量 (delta cycle 时 >1)

// 读值
let value = clk_signal.get_value_at(&offset, 0);
// SignalValueRef 可为: BitVec(BitVecRef), Real(f64), String(&str), Event

// 卸载信号释放内存
wave.unload_signals(&refs);
```

**FST 加载模型（与 VCD 的关键区别）：**

- VCD: `read_body()` 全文解析+压缩 → `wavemem::Reader`（全部信号进内存）
- **FST**: `read_body()` 近乎空操作，只保存 FstReader handle
  - `load_signals()` 时才通过 `fst-reader` crate 解析对应 signal 的 section
  - FST 文件按 signal 分块索引，通过 `Filter` 指定要读的信号
  - memmap2 内存映射，OS 按需换页

### 3.2 实验 1：时钟沿检测 (`clock_edge_experiment.rs`)

**位置**: `${WELLEN_HOME}/wellen/examples/clock_edge_experiment.rs`

**目的**: 验证 Wellen 能否区分信号是否在时钟沿的精确时刻变化。

**方法**:
1. 打开 FST 文件，遍历层级找到 clk 信号
2. 扫描所有 1-bit 信号，按变化频率排序，选前 5 个最活跃的
3. 加载 clk + 5 个数据信号
4. 遍历 clk 的 time_indices，检测上升沿 (0→1) 和下降沿 (1→0)
5. 对每个沿，对每个数据信号调用 `get_offset(edge_time_idx)`
6. 检查 `DataOffset.time_match` — `true` = 信号在此沿变化
7. 输出 before / middle / after 值

**结果** (picorv32.vcd.fst):

```
--- Edge #0  time=5000 (idx=1) ↓ ---
  [CLK]  0  (changed at this edge: true)
  testbench.top.uut.picorv32_core.count_cycle  0x0000..00  0x0000..00  0x0000..01
  testbench.top.uut.picorv32_core.mem_state     00           00           01
  testbench.top.uut.picorv32_core.mem_xfer      0            0            1
  
  → 数据信号在此时钟沿没变 (time_match=false)，但在 future 会变 (after ≠ middle)
```

### 3.3 实验 2：全局同步性分析 (`clock_coincidence.rs`)

**位置**: `${WELLEN_HOME}/wellen/examples/clock_coincidence.rs`

**目的**: 证明 Wellen 能检测到多个信号在同一时刻同步变化——这是验证 `trace_active_driver` 所需的核心能力。

**方法**:
1. 加载所有 1-bit 信号 (289个)
2. 对每个信号的每次变化，记录 `(time_idx, signal_name)`
3. 按 `time_idx` 聚合，找出多个信号同时变化的时刻
4. 输出并发度最高的时刻

**结果** (picorv32.vcd.fst):

```
Time points with ≥2 1-bit signals going high simultaneously:
time_idx    count        time  example signals
936            28     4680000  clk, mem_axi_rready, mem_axi_arvalid, ...
952            28     4760000  (下一个时钟沿)

=== 详细分析 time_idx=936 ===
Signal                          bef  mid  aft  verdict
clk                              0    1    0  0⟶1  CHANGED AT EDGE
mem_axi_rready                   0    1    0  0⟶1  CHANGED AT EDGE
mem_axi_arvalid                  0    1    0  0⟶1  CHANGED AT EDGE
mem_valid                        0    1    0  0⟶1  CHANGED AT EDGE
mem_instr                        0    1    0  0⟶1  CHANGED AT EDGE
alu_lts                          x    1    0  x⟶1  CHANGED AT EDGE
...
28 signals total
```

**结论**: Wellen 的 `time_match` 机制完美工作——能在 O(log N) 时间判定信号是否在某一精确时刻发生了变化。before/middle/after 三值直接对应 xdebug 的 `ClockPointSampler` 机制。

### 3.4 Wellen 内部数据结构细节

**FST 加载路径** (`wellen/src/fst.rs`):

```rust
// read_header: 只解析 metadata (层级结构、时间表)
let reader = FstReader::open_and_read_time_table(input)?;
let hierarchy = read_hierarchy(&mut reader)?;

// read_body: 近乎空操作
let time_table = reader.get_time_table().unwrap().to_vec(); // 拷贝时间表
let source = SignalSource::new(Box::new(FstWaveDatabase { reader }));
// FstWaveDatabase 持有 FstReader (包含 BufReader<File>)

// load_signals: 按需加载
fn load_signals(&mut self, ids: &[SignalRef], types: &[SignalEncoding]) -> Vec<Signal> {
    // 1. 构建 FST filter (指定只读哪几个信号)
    let filter = FstFilter::filter_signals(fst_ids);
    // 2. fst-reader 遍历匹配的 section, 通过 callback 产出值
    self.reader.read_signals(&filter, callback);
    // 3. SignalWriter 去重、自适应编码 (2→4→9 state)、产出 Signal
    signals.into_iter().map(|w| w.finish()).collect()
}
```

**Signal 内存布局** (加载后):

```rust
Signal {
    idx: SignalRef(4 bytes),
    time_indices: Vec<TimeTableIdx>,   // N × 4 bytes
    data: SignalChangeData(
        ChangeData::FixedLength {       // 定宽编码
            encoding: BitVector { max_states, width, meta_byte },
            bytes_per_entry: K,
            bytes: Vec<u8>,             // N × K bytes
        }
    ),
}
// 总内存 ≈ 72 + N×(4+K) bytes
// 例: 100万次变化的 1-bit 信号 ≈ 72 + 5MB ≈ 5MB
```

### 3.5 实验总结

| 能力 | Wellen 实现 | xdebug 等价 | 状态 |
|------|------------|------------|------|
| 按名查信号 | `h[var].full_name(h)` 遍历匹配 | `npi_fsdb_sig_by_name(path)` | ✅ O(N)遍历，可优化为 HashMap |
| 单点读值 | `get_offset(idx) + get_value_at()` | `npi_fsdb_sig_hdl_value_at()` | ✅ O(log N) 二分 |
| 时间区间变化迭代 | `iter_changes()` 或手动遍历 time_indices | `TimeBasedVcIter` | ✅ |
| 时钟沿检测 | `time_match` 字段 | `ClockPointSampler.middle` | ✅ 等价 |
| before 值 | `data().get_value_at(start-1)` 当 time_match=true | `ClockValueReader::read_before()` | ✅ 等价 |
| after 值 | `next_index` → 下一次变化的索引 | `ClockPointSampler.after` | ✅ 等价 |
| find_x_onset | 手动逆序遍历 time_indices | `SignalChangeCursor::prev_before()` | ✅ 可实现 |
| 批量多信号查询 | `values_at()` (C FFI 提供) | `npi_fsdb_sig_vec_value_at()` | ✅ 已实现 |
| 时钟采样扫描 | 基于 time_indices 实现 | `ClockSampleScanner` | ⚠️ 需后续实现 |

---

## 4. Verilator --design-db 修改

### 4.1 分支信息

- **仓库**: `${VERILATOR_HOME}`
- **分支**: `feature/design-db-for-xdebug`
- **基线**: Verilator master (v5.x)
- **修改文件**:
  - `src/V3Options.cpp` — 添加 `--design-db` 选项
  - `src/V3Options.h` — 添加 `m_designDb` 成员
  - `src/Verilator.cpp` — 编译管线中调用 `V3EmitDesignDb::emit()`
  - `src/CMakeLists.txt` — 添加新文件到构建
  - **新建** `src/V3EmitDesignDb.cpp` — 核心代码生成逻辑 (~350行)
  - **新建** `src/V3EmitDesignDb.h` — 头文件
  - **新建** `include/xdd_api.h` — C API 定义

### 4.2 xdd_api.h 完整接口

```c
// 生命周期
XddDb*   xdd_init(void);
void     xdd_close(XddDb* db);

// 信号解析
int      xdd_signal_count(XddDb* db);
XddSignal xdd_resolve(XddDb* db, const char* name);  // 返回索引或 -1

// 元数据
const char* xdd_signal_name(XddDb* db, XddSignal idx);
const char* xdd_signal_type(XddDb* db, XddSignal idx);  // "port"/"reg"/"wire"
int         xdd_signal_width(XddDb* db, XddSignal idx);
const char* xdd_signal_file(XddDb* db, XddSignal idx);
int         xdd_signal_line(XddDb* db, XddSignal idx);

// Driver 追踪
int  xdd_trace_driver_count(XddDb* db, XddSignal idx);
void xdd_trace_driver(XddDb* db, XddSignal idx, int i,
                      int* src_signal, const char** kind,
                      const char** file, int* line);

// Load 追踪
int  xdd_trace_load_count(XddDb* db, XddSignal idx);
void xdd_trace_load(XddDb* db, XddSignal idx, int i,
                    int* consumer_signal, const char** kind,
                    const char** file, int* line);
```

### 4.3 V3EmitDesignDb.cpp 实现原理

**Phase 1: 收集信号** (`collectSignals`):
- 遍历 AST 中所有 `AstVarScope` 节点
- 过滤：跳过 `__V*` 前缀、parameter、重复名
- 记录：hierarchical name (via `toSignalPath`), width, type (port/reg/wire), file, line

**Phase 2: 收集驱动和负载** (`collectDriversAndLoads`):
- 遍历所有 `AstNodeAssign` (包括 `AssignDly`, `AssignCont`, `AssignW`)
- 对每个赋值语句:
  - 找 LHS target signal
  - 找 RHS source signals
  - 找 enclosing `If`/`Case` 的控制信号 (control dependency)
  - 记录 driver 关系: `(target_signal, src_signal, kind, file, line)`
  - 记录 load 关系: `(src_signal, consumer_signal, "rhs_use", file, line)`
- kind 类型:
  - `"proc_assign"` — always_comb/always @(*) 中的阻塞赋值
  - `"cont_assign"` — assign 语句
  - `"nba"` — always_ff 中的非阻塞赋值 (<=)

**Phase 3: 生成 C++ 输出** (`emitCpp`):
- 生成 `lib<top>__DesignDb.cpp`
- 包含:
  - `kSignals[]` — 信号表 (name, type, width, file, line)
  - `kNameIndex[]` — 排序的名字索引 (用于 `xdd_resolve` 二分查找)
  - `kDrivers[]` + `kDriverStart[]` — driver 表
  - `kLoads[]` + `kLoadStart[]` — load 表
  - `xdd_init()`, `xdd_resolve()`, `xdd_trace_driver()`, 等函数实现

### 4.4 已完成的测试

`test_regress/t/validate_trace.c` — 全面的 C 测试程序:
- 通过 `dlopen()` 加载 `.so`
- `dlsym()` 获取所有函数指针
- 测试场景: simple, full (comprehensive), uart, metadata, ops
- 验证 driver/load 关系、信号元数据、名称解析、边界情况
- 支持嵌套 module (最多 3 层) 的跨模块 driver 追踪

测试 Verilog 文件:
- `t_xdd_trace.v` — proc_assign, cont_assign, nba, if/else 控制依赖
- `t_xdd_trace_full.v` — 全面的运算符覆盖
- `t_xdd_trace_ops.v` — 嵌套 module, 跨模块连接
- `t_xdd_uart_*.sv` — UART 16550 设计 (6个文件, ~200+ 信号)

### 4.5 待扩展功能 (Phase 3)

1. **Port 连接信息** — `trace.x_origin` 穿越 module 边界需要
2. **Direction 属性** — `xdd_signal_direction()` (当前只有 type)
3. **完整语句类型** — kind 字段需扩展: "if", "case", "force" 等控制流标记
4. **信号位选择/部分选择** — 当前只有 whole-signal 级别的 driver

---

## 5. xdebug 代码分析

### 5.1 NPI 耦合深度

对 `${XDEBUG_ORIGINAL_ROOT}/xdebug/src/` 的全面审计：

| 指标 | 数量 |
|------|------|
| NPI FSDB API 调用点总数 | ~930 处 |
| NPI 类型引用 (npiFsdbTime, npiFsdbSigHandle 等) | 677 处 |
| 涉及源文件 | 77 个 |
| 全局 NPI 状态 | `g_fsdb_file` 在两个 namespace 中 |
| 抽象层 | **无** — 所有 handler 直接调用 NPI API |

**关键接口文件**:

| 文件 | 作用 |
|------|------|
| `src/engine/service/engine_globals.h` | `g_fsdb_file`, `g_fsdb_path`, `g_daidir_path` |
| `src/engine/server.cpp` | `npi_fsdb_open()`, 启动 transport loop |
| `src/engine/service/engine_action_handler.h` | `action_name()`, `needs_design()`, `needs_waveform()`, `run()` |
| `src/waveform/server/fsdb_value_reader.h` | `read_sig_value_at()`, `fsdb_signal_width()` |
| `src/waveform/server/fsdb_scan_utils.h` | `SignalChangeCursor`, `ClockEdgeCursor`, `TimeBasedVcIterGuard` |
| `src/waveform/common/clock_sampling.h` | `ClockSampleScanner`, `ClockPointSampler` |
| `src/combined/active_trace_service.h` | `build_active_driver_payload()` |
| `src/combined/active_trace_chain.h` | `build_active_driver_chain_payload()` |

### 5.2 Action 分类

xdebug 共约 60+ 个 action handler，分 5 大类:

| 类别 | 数量 | needs_design | needs_waveform | NPI 依赖 |
|------|------|:---:|:---:|:---:|
| waveform | 37 | ❌ | ✅ | npi_fsdb_* |
| design | 5 | ✅ | ❌ | npi_hdl_* |
| combined | 3 | ✅ | ✅ | npi_fsdb_* + npi_hdl_* |
| protocol | ~10 | ❌ | ✅ | npi_fsdb_* |
| stream | ~7 | ❌ | ✅ | npi_fsdb_* |

**最复杂的 3 个 action:**
1. `trace.x_origin` (830行) — X 传播溯源，需要波形逆序查找 + 设计 driver 追踪 + port 穿越
2. `trace.active_driver` — 从给定时间点出发，沿 driver 链向前追踪
3. `window.verify` (~900行) — 时钟采样 + 表达式求值 + 全局/最终/永不断言

### 5.3 xverif MCP 架构

**MCP Server** (`${XDEBUG_ORIGINAL_ROOT}/xverif_mcp/`):
- `server.py` — FastMCP stdio server, `@xverif_tool` 装饰器注册
- `adapters/xdebug.py` — `XverifDebugAdapter` → `McpSessionManager`
- `xverif_loop/sessions/session_manager.py` — `McpSessionManager`
- `xverif_loop/sessions/launchers.py` — `_loop_cmd()` → `[xdebug_bin, "--stdio-loop", "--json"]`
- `xverif_loop/config.py` — `default_xdebug_bin()` → `tools/xdebug`

**Session 生命周期**:
1. `session.open` → `McpSessionManager.open_session()`
2. 创建 `XdebugLoopSession` → `launcher.start(cfg)` 启动原生二进制
3. 原生二进制通过 `--stdio-loop --json` 模式运行
4. 发送 `{"action":"session.open","target":{"session_id":"t","fsdb":"/path/to/waves.fsdb"}}`
5. 后续所有 JSONL query 通过 stdio 发送

**关键**: xdebug-fst 只需实现相同的 `--stdio-loop --json` 协议 + 接受 `"fsdb"` key (值指向 `.fst` 文件) 即可无缝替换。

---

## 6. 实施计划

### Phase 1: wellen-capi — Rust C FFI 层 ✅ 已完成

- [x] 创建 `wellen_capi` crate (workspace 成员)
- [x] 实现 17 个 `extern "C"` 函数
- [x] C header 文件
- [x] C 测试程序 (picorv32.vcd.fst 验证通过)
- [x] Release build (`libwellen_capi.so`)

### Phase 2: xdebug-fst C++ 引擎 ✅ 已完成

**2.1 骨架** ✅:
- [x] `CMakeLists.txt` 构建系统（GLOB 自动收集源码）
- [x] `IWaveformBackend` 抽象接口（含 find_signal / time_indices_of）
- [x] `IDesignBackend` 抽象接口
- [x] `WellenFstBackend` — 封装 wellen_capi + wellenx_capi 扩展
- [x] `XddDesignBackend` — dlopen/dlsym 封装 Verilator DesignDB SO
- [x] `EngineGlobals` / `EngineActionHandler` / `ActionRegistry`
- [x] `main.cpp` — 三种 CLI 模式；`server.cpp` — dispatch + session 管理
- [x] 编译通过、link 成功

**2.2 First actions** ✅:
- [x] `scope.list` + `scope.roots`、`value.at`、`signal.changes`
- [x] `signal.resolve`、`trace.driver`、`trace.load`
- [x] 编译调试完成（const 签名、scope name 缓存、ref=0 哨兵等全部修复）

**2.3-2.8 全部 actions** ✅（共 74 个，与 xdebug 73 个对齐）:
- [x] signal.statistics / stability / xz_verify / anomaly.inspect
- [x] clock_point_query / expr.eval_at / counter.statistics /
      signal.sampled_pulse.inspect / protocol.handshake.inspect
- [x] list.* (8) / event.* (4) / waveform.cursor.* (5) / nwave.rc.generate
- [x] apb.* (6) / axi.* (11)
- [x] stream.* (7)
- [x] signal.canonicalize / expr.normalize
- [x] trace.active_driver / active_driver_chain / x_origin
- [x] window.verify / verify.conditions
- [x] batch / session.open/close/list/doctor/gc/kill

### Phase 3: design-db 扩展 ✅ 已完成（xdebug-fst 侧实现）

- [x] Direction 推断：从 driver/load 表推断 port input/output（nba/proc 驱动
      → output；cont_assign src → input）；原生 xdd_signal_direction 符号优先
- [x] Port 连接推断：cont_assign 记录提取 port_boundary 连接
- [x] trace.x_origin port 穿越：driver 链 + port_connections fallback
- [x] 验证：xprop fixture（VCD 手写 X 传播）→ out → y → a 完整 X 链

### Phase 4: xverif MCP 兼容性验证 ✅ 已完成（xverif 零修改）

- [x] stdio-loop wire 协议：ready envelope、request_id 回显、payload_format
      json/xout envelope（与参考 xdebug 对齐）
- [x] session.open 兼容 args.name（xverif 约定）与 target.session_id
- [x] DesignDB SO 自动探测（fsdb 旁或 obj_dir/）
- [x] E2E 通过 xverif 真实 McpSessionManager：open/query/close 全通
- [x] 仅换 `XDEBUG_BIN` 环境变量即可从 FSDB 后端切换到 FST 后端

### Phase 5: 测试 Fixture 全开源化 ✅ 已完成

- [x] Verilator `--trace-fst --design-db` 生成 FST + DesignDB SO（无 VIP）
      - counter（基础波形）、apb（APB 主从传输）、axi（AXI4 读写突发）、
        stream（valid/ready FIFO）、xprop（X 传播，手写 VCD）
- [x] pytest 套件 120 个测试，覆盖全部 74 个 action（参考 xverif runner 风格）
- [x] 测试运行环境：xverif 的 conda env（`${XDEBUG_ORIGINAL_ROOT}/.conda-xverif`）

---

## 7. 当前进度

### 已完成（全部 Phase 1-5）

| 组件 | 状态 | 说明 |
|------|------|------|
| wellen_capi (Rust) | ✅ | 17 个 extern "C" 函数，C 测试通过 |
| wellenx_capi (Rust 扩展) | ✅ | 最小扩展 FFI：ASCII 位串值（2/4/9-state）+ time_indices |
| LogicValue 渲染 | ✅ | SV literal hex/bin/dec，4-state X/Z，parse_sv_literal |
| IWaveformBackend / IDesignBackend | ✅ | 抽象接口 + WellenFst/Xdd 实现 |
| 74 个 action handlers | ✅ | 与 xdebug 73 个对齐，全部响应无 crash |
| Direction/Port 推断 | ✅ | 从 driver/load 表推断，支撑 trace.x_origin |
| stdio-loop 协议 | ✅ | ready + envelope，xverif MCP 兼容 |
| FST fixtures | ✅ | counter/apb/axi/stream (Verilator) + xprop (VCD) |
| pytest 套件 | ✅ | 120 测试全绿，覆盖全部 action |
| E2E (xverif McpSessionManager) | ✅ | open/query/close 全通，xverif 零修改 |

### 构建与测试

```bash
# 构建（Rust 扩展 + C++ 引擎）
cd ${REPO_ROOT}/wellenx_capi && cargo build --release
cd ${REPO_ROOT} && cmake -S . -B build && cmake --build build -j4

# 运行
LD_LIBRARY_PATH=build:../wellen/target/release:wellenx_capi/target/release \
  ./build/xdebug-fst --stdio-loop --json < req.jsonl

# 测试（xverif 的 conda env）
${XDEBUG_ORIGINAL_ROOT}/.conda-xverif/bin/python -m pytest tests/ -p no:xverif
```

### Git 提交历史

```
ccbccc0 Phase 4: xverif MCP end-to-end compatibility
773c204 Phase 5b: comprehensive pytest suite — 120 tests covering all 74 actions
e6c19f7 Phase 5: APB/AXI/stream FST fixtures + protocol analyzer fixes
20fd4e2 Phase 3: design-db direction/port-connection inference + trace.x_origin port crossing
e26507f Phase 2.4-2.6: implement 60+ action handlers (parallel subagents)
ed4f69b Phase 2.3: shared waveform infrastructure
d4416b6 Phase 2.1/2.2: fix scope-name cache, add wellenx_capi extension, wire FST+DesignDB pipeline
43890e2 Phase 2: xdebug-fst C++ engine skeleton
fe6a083 Initial commit: xdebug-fst migration plan
```

---

## 8. 关键实现细节

### 8.1 wellen_capi FFI 设计要点

**内存模型**:
```
C++ 侧                    Rust 侧 (wellen_capi)
───────                    ─────────────────────
wellen_open()         →   Box::new(WellenDb).into_raw()
wellen_close(ptr)     →   Box::from_raw(ptr).drop()

wellen_get_times()    →   拷贝 Vec<u64> 到 C 预分配 buffer
wellen_scope_name()   →   CString 缓存 (HashMap<usize, CString>)
wellen_load_signals() →   wave.load_signals(refs)  // 触发 FST I/O
wellen_signal_offset_at() → signal.get_offset(time_idx) // 二分查找
```

**Time → TimeTableIdx 转换**: C++ 侧缓存时间表 (`std::vector<uint64_t>`)，用 `std::upper_bound` 做二分，避免每次 FFI 调用的开销。

**ReF 编码**: ScopeRef/VarRef: 1-indexed (0 = invalid)。SignalRef: 0-indexed (所有从 wellen 返回的 index 直接使用)。

### 8.2 xdebug-fst dispatch 流程

```
stdin JSON → Json::parse() → dispatch(request)
  ├─ action == "actions"        → 返回 handler 列表
  ├─ action == "schema"         → 返回 schema stub
  ├─ action == "session.open"   → 打开 FST 文件, 初始化 EngineGlobals
  ├─ action == "session.close"  → 关闭 backend
  └─ 其他                        → ActionRegistry::find(action) → handler->run(request)
       ├─ needs_design() 检查
       └─ needs_waveform() 检查
```

### 8.3 信号查找实现

当前实现: O(N×M) 遍历所有 scope × var，匹配 `var_full_name()`。对于大型设计 (>10000 signals) 需要优化为 HashMap。

优化方案 (后续): 在 `WellenFstBackend::open()` 时构建 `std::unordered_map<std::string, uint32_t>` (name → signal_ref)。

### 8.4 许可证合规

xdebug-fst 的动态链接架构天然满足 LGPL 要求:
- `libwellen_capi.so` — BSD-3, 直接 link
- `libVtop__DesignDb.so` — LGPL, 通过 `dlopen()` 动态加载 (用户可替换)
- `nlohmann/json` — MIT, header-only (无 link)

---

## 9. 待解决事项

### 优先级 P0 (阻塞当前进度)

1. **修复 action 文件的 const 签名** — 三个文件需修正 `override` 签名
2. **重建 CMake 构建** — 修复后完整编译+link

### 优先级 P1 (Phase 2 继续)

3. **实现更多 waveform actions**: signal_statistics, clock_point_query, etc.
4. **优化信号查找**: HashMap 替代 O(N²) 遍历
5. **实现时间格式化**: 从 wellen timescale 计算 human-readable 字符串

### 优先级 P2 (Phase 3)

6. **Verilator --design-db 扩展**: port 连接, direction
7. **trace.x_origin 实现**: find_x_onset + collect_dependencies 迁移

### 优先级 P3 (Phase 5)

8. **APB/AXI BFM**: C++ Verilator testbench + scoreboard
9. **测试 fixture 生成**: Makefile 自动化
10. **MCP 兼容性端到端验证**

---

## 10. 附录：文件清单

### wellen_capi (17 函数)

```
wellen_open, wellen_open_error, wellen_close
wellen_time_count, wellen_get_times
wellen_root_scope_count, wellen_scope_at, wellen_scope_child_count,
  wellen_scope_child_at, wellen_scope_var_count, wellen_scope_var_at
wellen_scope_name, wellen_var_name, wellen_var_full_name
wellen_var_signal_ref, wellen_var_encoding
wellen_load_signals, wellen_unload_signals, wellen_signal_info
wellen_signal_offset_at, wellen_signal_value_at_offset, wellen_values_at
```

### xdebug-fst 源码文件

| 文件 | 行数 | 状态 |
|------|------|------|
| `src/main.cpp` | 40 | ✅ |
| `src/server.cpp` | 170 | ✅ |
| `src/api/json_types.h` | 4 | ✅ |
| `src/backend/waveform_backend.h` | 144 | ✅ |
| `src/backend/design_backend.h` | 97 | ✅ |
| `src/backend/wellen_fst_backend.h` | 67 | ✅ |
| `src/backend/wellen_fst_backend.cpp` | 280 | ✅ |
| `src/backend/xdd_design_backend.h` | 46 | ✅ |
| `src/backend/xdd_design_backend.cpp` | 185 | ✅ |
| `src/engine/engine_action_handler.h` | 22 | ✅ |
| `src/engine/engine_globals.h` | 31 | ✅ |
| `src/engine/engine_globals.cpp` | 78 | ✅ |
| `src/engine/action_registry.h` | 27 | ✅ |
| `src/engine/action_registry.cpp` | 30 | ✅ |
| `src/engine/actions/register_all.cpp` | 24 | ✅ |
| `src/engine/actions/waveform/scope_list.cpp` | 52 | 🔧 |
| `src/engine/actions/waveform/value_at.cpp` | 75 | 🔧 |
| `src/engine/actions/design/signal_resolve.cpp` | 84 | 🔧 |
| `CMakeLists.txt` | 31 | ✅ |
| **合计** | **~1487** | |

### Wellen 实验文件

| 文件 | 说明 |
|------|------|
| `wellen/examples/clock_edge_experiment.rs` | 时钟沿 before/middle/after 检测 |
| `wellen/examples/clock_coincidence.rs` | 全局信号同步性分析 |
| `wellen/wellen_capi/test/test_capi.c` | C FFI 端到端验证 |

### Verilator --design-db 文件

| 文件 | 说明 |
|------|------|
| `include/xdd_api.h` | 公开 C API |
| `src/V3EmitDesignDb.cpp` | 代码生成 (~350行) |
| `src/V3EmitDesignDb.h` | 头文件 |
| `test_regress/t/validate_trace.c` | 测试 (413行) |
| `test_regress/t/t_xdd_*.v` | 测试 Verilog 文件 (10个) |
