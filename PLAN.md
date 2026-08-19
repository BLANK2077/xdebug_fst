# xdebug FST 迁移方案

## Context

当前 xdebug 的波形后端深度绑定 Synopsys NPI FSDB C API：77 个源文件中 ~930 处 NPI API 调用、677 处 NPI 类型引用、全局状态 `g_fsdb_file` 散落各处，无抽象层。FSDB 需要商业许可，FST 是开源格式（Verilator/GHDL/nvc 均可输出）。

我们已有两块开源积木：
- **Verilator `--design-db`**（`${VERILATOR_HOME}` 的 `feature/design-db-for-xdebug` 分支）：编译时生成 `lib<top>__DesignDb.so`，提供 `xdd_*` C API（signal 解析、driver/load 追踪）
- **Wellen**（BSD-3）：Rust FST/VCD/GHW 解析库，支持 mmap 懒加载、O(log N) 二分查询、`time_match` 时钟沿检测

## 核心原则

1. **不修改 `${XDEBUG_ORIGINAL_ROOT}` 的任何文件。** xverif MCP server 原封不动。
2. **`xdebug-fst` 是 xdebug 的二进制级兼容替换**——相同 CLI 接口、相同 JSON 协议、相同 session 生命周期。
3. **通过环境变量切换**: `XDEBUG_BIN=${REPO_ROOT}/build/xdebug-fst` 即可从 FSDB 后端切换到 FST 后端。

### 兼容性要求

xdebug-fst 必须实现以下 CLI 接口（与 xdebug 完全一致）：

```
# 无状态 one-shot
xdebug-fst --json -              # 接受 stdin JSON，输出 stdout JSON
xdebug-fst -                     # 接受 stdin JSON，输出 stdout XOUT

# 有状态 session (stdio-loop)
xdebug-fst --stdio-loop --json   # 持久进程，JSONL 协议

# session 管理
xdebug-fst --server <session_id> -fsdb <path> --transport uds ...
#                               ↑ 保持参数名 -fsdb 兼容，实际接受 .fst 文件
```

`session.open` 的 target 字段保持兼容：
```json
{"action": "session.open", "target": {"session_id": "test", "fsdb": "/path/to/waves.fst"}}
```
`"fsdb"` key 名不变，值指向 FST 文件。MCP server 现有的 `target_key="fsdb"` 无需改动。

## 总体架构

```
┌─────────────────────────────────────────────────────────┐
│              xverif MCP Server (不变，不修改)              │
│  adapters/xdebug.py                                      │
│    → 通过 XDEBUG_BIN 环境变量指向 xdebug 或 xdebug-fst    │
│    → target_key="fsdb" 不变，值指向 .fst 文件             │
└──────────────────────┬──────────────────────────────────┘
                       │ stdio-loop JSONL 协议
           ┌───────────┴───────────┐
           │                       │
    ┌──────┴──────┐        ┌──────┴──────────┐
    │   xdebug    │        │   xdebug-fst     │
    │ (C++, NPI)  │        │ (C++, wellen-FFI)│
    │ 不变         │        │ ${REPO_ROOT}/../     │
    │             │        │   xdebug_fst/     │
    └──────┬──────┘        └──┬──────┬────────┘
           │                  │      │
    npi_fsdb_*          wellen_capi  xdd_api
                        (Rust C FFI) (Verilator SO)
                             │
                        wellen (Rust)
                        fst-reader
```

MCP server 完全不需要修改。切换方式：
```bash
# FSDB 模式 (现有)
XDEBUG_BIN=${XDEBUG_ORIGINAL_ROOT}/tools/xdebug

# FST 模式 (新增)
XDEBUG_BIN=${REPO_ROOT}/build/xdebug-fst
```

## 工作目录

```
${REPO_ROOT}/../
├── wellen/           ← Wellen (Rust, BSD-3, 已有)
├── verilator/        ← Verilator + --design-db (已有)
├── lz4/              ← LZ4 (已有)
└── xdebug_fst/       ← 【新建】xdebug-fst 源码 + testdata
    ├── src/           ← C++ 引擎
    ├── wellen_capi/   ← Rust C FFI (wellen workspace 内)
    ├── testdata/      ← Verilator 生成的测试 FST fixtures
    └── tests/         ← Python 测试（pytest）
```

## Phase 1: wellen-capi — Rust C FFI 层

**仓库位置**: `${WELLEN_HOME}/wellen_capi/`（wellen workspace 内新增 crate）
**产物输出**: `libwellen_capi.so`, `wellen_capi.h` → 被 `${REPO_ROOT}/` 引用

**目标**: 约 500 行 Rust，暴露 ~16 个 `extern "C"` 函数。

### 1.1 API 清单

```
// 生命周期
wellen_open(path) → handle
wellen_close(handle)
wellen_last_error(handle) → const char*

// 时间表（C++ 侧本地拷贝后二分查询）
wellen_time_count(handle) → uint32_t
wellen_get_times(handle, out[], offset, count) → void

// 层级遍历（返回 u32 handle，零拷贝）
wellen_root_scope_count(handle) → uint32_t
wellen_root_scope_at(handle, idx) → ScopeRef
wellen_scope_child_count(handle, scope) → uint32_t
wellen_scope_child_at(handle, scope, idx) → ScopeRef
wellen_scope_var_count(handle, scope) → uint32_t
wellen_scope_var_at(handle, scope, idx) → VarRef
wellen_scope_name(handle, scope) → const char*
wellen_var_name/full_name(handle, var) → const char*
wellen_var_signal_ref(handle, var) → SignalRef
wellen_var_signal_encoding(handle, var) → encoding enum

// 便捷：路径→SignalRef（C 侧二分查 interned string table）
wellen_find_signal(handle, "top.u.clk") → SignalRef

// 信号加载/卸载
wellen_load_signals(handle, refs[], count) → int
wellen_unload_signals(handle, refs[], count) → void
wellen_signal_info(handle, ref, &info) → int

// 核心查询
wellen_signal_offset_at(handle, ref, time_idx,
    &start, &elements, &time_match, &next_idx, &has_next) → int
wellen_signal_value_at_offset(handle, ref, start, element,
    out_bytes, &out_len, &out_str, &out_str_len) → int

// 批量读（多信号同一时间）
wellen_values_at(handle, refs[], count, time_idx,
    out_values[], out_found[]) → int
```

### 1.2 数据类型

```c
typedef void* wellen_handle_t;
typedef uint32_t wellen_scope_ref_t;   // NonZeroU32
typedef uint32_t wellen_var_ref_t;
typedef uint32_t wellen_signal_ref_t;
typedef uint32_t wellen_time_idx_t;

typedef struct {
    uint32_t signal_ref;
    uint32_t num_changes;
    uint32_t max_states;        // 2, 4, or 9
    uint32_t width;
    uint32_t bytes_per_entry;
    int32_t  has_meta_byte;
} wellen_signal_info_t;
```

### 1.3 构建产物

```makefile
wellen_capi/target/release/libwellen_capi.so  (BSD-3)
wellen_capi/include/wellen_capi.h             (BSD-3)
```

---

## Phase 2: xdebug-fst — C++ 波形引擎

**仓库位置**: `${REPO_ROOT}/`（新建目录）

**目标**: 约 5000-8000 行 C++，实现与 xdebug 等价的波形 action 全集。

### 2.1 架构

```
xdebug_fst/src/
├── main.cpp                  # --server 入口，与 xdebug 等价
├── server.cpp                # 打开 FST + DesignDB SO，启动 transport loop
├── backend/
│   ├── waveform_backend.h    # IWaveformBackend 抽象接口
│   ├── wellen_fst_backend.h  # Wellen C FFI 实现
│   └── design_backend.h      # IDesignBackend (xdd_api)
├── waveform/                 # 从 xdebug 移植，去掉 NPI 依赖
│   ├── clock_sampling.h/cpp  # 基于 Wellen time_indices 重写
│   ├── signal_analysis.cpp   # signal.changes, statistics, xz_verify...
│   ├── signal_inspect.cpp    # pulse_inspect, handshake_inspect, anomaly_inspect
│   ├── event_analyzer.h/cpp  # 事件（expression + waveform）分析
│   ├── expression.h/cpp      # 复用 xdebug 的表达式引擎（无 NPI 依赖）
│   ├── value_support.h/cpp
│   └── time_contract.h/cpp
├── protocol/                 # APB/AXI 协议分析（复用，替换内部 NPI 调用）
│   ├── apb_analyzer.h/cpp
│   └── axi_analyzer.h/cpp
├── engine/
│   ├── engine_action_handler.h  # 复用 xdebug 接口定义
│   ├── engine_action_registry.h/cpp
│   ├── engine_globals.h         # 替换 g_fsdb_file → g_waveform_backend
│   ├── actions/
│   │   ├── waveform/            # 37 个 waveform action handler
│   │   │   ├── value_at.cpp
│   │   │   ├── signal_changes.cpp
│   │   │   ├── signal_statistics.cpp
│   │   │   ├── signal_stability.cpp
│   │   │   ├── signal_xz_verify.cpp
│   │   │   ├── signal_sampled_pulse_inspect.cpp
│   │   │   ├── signal_anomaly_inspect.cpp
│   │   │   ├── protocol_handshake_inspect.cpp
│   │   │   ├── clock_point_query.cpp
│   │   │   ├── counter_statistics.cpp
│   │   │   ├── expr_eval_at.cpp
│   │   │   ├── window_verify.cpp
│   │   │   ├── scope_list.cpp / scope_roots.cpp
│   │   │   ├── list_add.cpp / list_create.cpp / list_load.cpp / list_show.cpp
│   │   │   ├── list_first_change.cpp
│   │   │   ├── list_export.cpp
│   │   │   ├── event_*.cpp
│   │   │   ├── waveform_cursor_*.cpp
│   │   │   └── nwave_rc_generate.cpp / verify_conditions.cpp
│   │   ├── design/             # 5 个 design action handler
│   │   │   ├── signal_resolve.cpp
│   │   │   ├── signal_canonicalize.cpp
│   │   │   ├── trace_driver.cpp     # ← Verilator xdd_trace_driver()
│   │   │   ├── trace_load.cpp       # ← Verilator xdd_trace_load()
│   │   │   └── expr_normalize.cpp
│   │   ├── combined/           # active trace / trace x
│   │   │   ├── trace_active_driver.cpp
│   │   │   ├── trace_active_driver_chain.cpp
│   │   │   └── trace_x_origin.cpp
│   │   ├── protocol/           # APB/AXI queries
│   │   │   ├── apb_query.cpp / apb_statistics.cpp
│   │   │   ├── axi_query.cpp / axi_analysis.cpp / axi_export.cpp
│   │   │   └── ...
│   │   └── stream/             # stream actions
│   │       ├── stream_query.cpp / stream_describe.cpp / stream_export.cpp
│   │       └── ...
│   └── service/                # 从 xdebug 复用
│       ├── engine_action_handler.h  (同接口)
│       └── trace_source_path_formatter.h/cpp
└── api/                        # 复用 xdebug api/ 层
    ├── action_catalog.cpp/h
    ├── action_context.h
    ├── action_registry.cpp/h
    ├── dispatcher.cpp/h
    ├── json_types.h
    ├── request_envelope.cpp/h
    ├── response.cpp/h
    └── stdio_loop.cpp/h
```

### 2.2 关键接口抽象

```cpp
// backend/waveform_backend.h
class IWaveformBackend {
public:
    virtual ~IWaveformBackend() = default;

    // Lifecycle
    virtual bool open(const std::string& path) = 0;
    virtual void close() = 0;

    // Time
    virtual uint64_t min_time() const = 0;
    virtual uint64_t max_time() const = 0;
    virtual uint32_t time_count() const = 0;
    virtual uint64_t time_at(uint32_t idx) const = 0;
    virtual uint32_t time_idx_of(uint64_t time) const = 0;  // binary search
    virtual std::string format_time(uint64_t time) const = 0;

    // Hierarchy
    virtual uint32_t root_scope_count() const = 0;
    virtual uint32_t scope_child_count(uint32_t scope) const = 0;
    virtual uint32_t scope_var_count(uint32_t scope) const = 0;
    // ... accessors
    virtual uint32_t find_signal(const char* path) const = 0;  // → SignalRef or 0

    // Signal loading
    virtual bool load_signals(const std::vector<uint32_t>& refs) = 0;
    virtual void unload_signals(const std::vector<uint32_t>& refs) = 0;

    // Signal info
    struct SignalInfo { uint32_t width, num_changes, max_states, bytes_per_entry; };
    virtual bool signal_info(uint32_t ref, SignalInfo&) const = 0;

    // Query
    struct SignalOffset {
        uint32_t start; uint16_t elements;
        bool time_match; uint32_t next_idx; bool has_next;
    };
    virtual bool signal_offset_at(uint32_t ref, uint32_t time_idx,
                                   SignalOffset&) const = 0;
    virtual std::string signal_value_at_offset(uint32_t ref,
                                                uint32_t start, uint16_t element) const = 0;

    // Bulk
    virtual bool values_at(const std::vector<uint32_t>& refs, uint32_t time_idx,
                           std::vector<std::string>& out,
                           std::vector<bool>& out_found) const = 0;
};

// backend/design_backend.h
class IDesignBackend {
public:
    virtual ~IDesignBackend() = default;
    virtual bool open(const std::string& so_path) = 0;  // dlopen()
    virtual void close() = 0;

    virtual int signal_count() const = 0;
    virtual int resolve(const char* name) const = 0;
    virtual const char* signal_name(int idx) const = 0;
    virtual const char* signal_type(int idx) const = 0;
    virtual int signal_width(int idx) const = 0;

    struct Driver { int src_signal; std::string kind, file; int line; };
    virtual std::vector<Driver> trace_driver(int idx) const = 0;

    struct Load { int consumer; std::string kind, file; int line; };
    virtual std::vector<Load> trace_load(int idx) const = 0;
};
```

### 2.3 Action Handler 等价清单

每个 action 需要与 xdebug (FSDB) 产生**字节等价**的 JSON 输出。

#### waveform actions (37个) — 全部需要 New Backend
| action | 关键依赖 | 迁移难度 |
|--------|---------|---------|
| `value.at` | `signal_by_name` + `value_at(time)` | 低 |
| `signal.changes` | `SignalChangeCursor` | 中 |
| `signal.statistics` | `ClockSampleScanner` | 高 |
| `signal.stability` | `SignalChangeCursor` | 低 |
| `signal.xz_verify` | `SignalChangeCursor` | 低 |
| `signal.sampled_pulse.inspect` | `ClockSampleScanner` + `SignalChangeCursor` | 高 |
| `signal.anomaly.inspect` | `SignalChangeCursor` | 低 |
| `protocol.handshake.inspect` | `ClockSampleScanner` | 高 |
| `counter.statistics` | `ClockSampleScanner` | 高 |
| `expr.eval_at` | `ClockPointSampler` | 中 |
| `window.verify` | `ClockSampleScanner` + expression | 高 |
| `clock_point_query` | `ClockPointSampler` | 中 |
| `scope.list` / `scope.roots` | hierarchy iteration | 低 |
| `list.*` (add/create/load/show/validate) | `signal_by_name` | 低 |
| `list.first_change` | `find_list_first_change` | 中 |
| `list.export` | full signal export | 高 |
| `event.*` | `EventAnalyzer` | 中-高 |
| `waveform.cursor.*` | `CursorManager` | 低 |
| `apb.*` / `axi.*` | `ApbAnalyzer` / `AxiAnalyzer` | 高 |
| `nwave_rc_generate` | `RcGenerator` | 低 |

#### design actions (5个) — 全部用 Verilator XddDb
| action | 迁移 |
|--------|------|
| `signal.resolve` | `xdd_resolve()` |
| `signal.canonicalize` | `xdd_signal_name/type()` |
| `trace.driver` | `xdd_trace_driver()` |
| `trace.load` | `xdd_trace_load()` |
| `expr.normalize` | 纯计算，不依赖 NPI |

#### combined actions (3个) — 需要 Both Backends
| action | 依赖 | 迁移难度 |
|--------|-----|---------|
| `trace.active_driver` | IWaveformBackend + IDesignBackend | 高 |
| `trace.active_driver_chain` | 同上 | 高 |
| `trace.x_origin` | IWaveformBackend (SignalChangeCursor + find_x_onset) + IDesignBackend (driver追踪 + PortConnection) | **最高** |

#### protocol actions (~10个)
| action | 迁移难度 |
|--------|---------|
| `apb.query/wr/rd/transaction_cursor/statistics/transfer_window` | 高 (需移植 ApbAnalyzer) |
| `axi.query/analysis/export/transaction_cursor/statistics` | 高 (需移植 AxiAnalyzer) |
| `axi.latency_outlier/outstanding_timeline/channel_stall/request_response_pair` | 高 |

#### stream actions
| action | 迁移难度 |
|--------|---------|
| `stream.config.list/load/describe` | 中 |
| `stream.validate` | 中 |
| `stream.query/export` | 高 |

### 2.4 trace.x_origin 迁移方案（最复杂 action）

`trace.x_origin` 的核心逻辑：
1. 从信号 S 在时间 T 处有 X 值出发
2. 用 `find_x_onset`（`SignalChangeCursor` 逆序查找 X 首次出现的时间）
3. 用 `collect_dependencies`（NPI `npi_trace_driver_by_hdl2` + port 穿越）找上游驱动信号
4. 对每个驱动信号读取波形值，若也是 X，递归
5. 终止条件：找到 force X / 主输入 / 未 dump / 不可观测 / 循环 / 超深度

FST 版本替换：
- `SignalChangeCursor` → Wellen `signal.time_indices` + `get_offset(time_idx)`（正向二分） + 手动逆序（`offset.start - 1` 取上一个值）
- `find_x_onset` → 从 anchor_time_idx 开始，用 `signal.data().get_value_at(idx)` 读值，反向遍历 time_indices
- `npi_trace_driver_by_hdl2` → `xdd_trace_driver()` (Verilator)
- `PortConnection`（穿越 module 边界）→ 需要 Verilator 提供 port 连接信息（当前 `xdd_api.h` 不包含这个——**需要扩展或后期迭代**）
- `npi_string(npiFullName)`, `npi_get(npiType)`, `npiHandle` → `xdd_signal_name/type()` 

**`trace.x_origin` 是 Phase 3 的目标，Phase 2 先确保 simpler actions 全部通过。**

### 2.5 关键设计决策

1. **不与 xdebug 共享编译单元。** 全部新建文件——xdebug 的 NPI 渗透太深，共享一个 .h 文件都会引入 `#include "npi_fsdb.h"` 的传播链。

2. **API 层透明复用。** `api/` 下的 JSON schema、action registry、dispatcher、stdio_loop 与后端无关——直接拷贝，只改 `#include` 路径。

3. **`trace.x_origin` 暂时降级为 Phase 2 暂不支持（返回 "not supported in fst backend yet"），Phase 3 补全。** Port 穿越需要扩展现有的 Verilator `--design-db` 生成逻辑或增加额外的 port 映射表。

---

## Phase 3: Verilator --design-db 扩展

**仓库位置**: `${VERILATOR_HOME}` 的 `feature/design-db-for-xdebug` 分支
**测试 fixture 输出**: 生成的 `libVtop__DesignDb.so` + `waves.fst` → 放在 `${REPO_ROOT}/testdata/`

### 需要补充的功能

1. **Port 连接信息** (`PortConnection`) — 用于 `trace.x_origin` 和 `trace.active_driver` 的 `resolve_input_port_connection()`。Verilator 在 flatten 过程中知道 port 的 high/low connection，但目前没有导出到 `xdd_api.h`。
   - 新增结构：`typedef struct { int port_signal; int connected_signal; const char* kind; } XddPortConn;`
   - 新增 API：`int xdd_port_conn_count(db, signal_idx)` 和 `void xdd_port_conn(db, signal_idx, int i, ...)`

2. **完整语句信息** — 当前 `xdd_trace_driver()` 返回 `(src_signal, kind, file, line)`。与 NPI `drvLoadStmtVec_t` 等价需要额外返回 `useHdl` 类型标签和表达式文本。Phase 2 最低需要 `kind` 区分 "assignment" / "force" / "if" / "case"。

3. **Direction 属性** — `xdd_signal_type()` 目前返回 "port" / "reg" / "wire"。`trace.x_origin` 需要 `direction`（input/output/inout）。新增 `xdd_signal_direction()`。

---

## Phase 4: xverif MCP 兼容性验证

**不修改 `${XDEBUG_ORIGINAL_ROOT}` 的任何文件。**

xverif MCP server 通过以下机制启动 xdebug：

```python
# xverif_loop/sessions/launchers.py:147
def _loop_cmd(cfg):
    cmd = [cfg.tool_bin or cfg.xdebug_bin, "--stdio-loop", "--json"]
    return cmd
```

```python
# xverif_loop/sessions/loop_session.py — session.open 的 target
open_req["target"][self.target_key] = self.fsdb  # target_key = "fsdb"
```

xdebug-fst 需要支持完全相同的协议：

### 4.1 CLI 兼容性

| 功能 | xdebug CLI | xdebug-fst CLI |
|------|-----------|---------------|
| One-shot JSON | `xdebug --json -` | `xdebug-fst --json -` |
| One-shot XOUT | `xdebug -` | `xdebug-fst -` |
| Session loop | `xdebug --stdio-loop --json` | `xdebug-fst --stdio-loop --json` |
| Session server | `xdebug --server <id> -fsdb <path> --transport uds` | `xdebug-fst --server <id> -fsdb <path> --transport uds` |

### 4.2 Session 协议兼容性

|| 请求 | xdebug 响应 | xdebug-fst 响应 |
|------|-----------|-------------|------|
| `session.open` | `{"ok":true,"session":{"session_id":"test",...}}` | 完全一致 |
| `actions` | `{"ok":true,"actions":[...]}` | 完全一致（相同 action 列表） |
| `schema` | `{"ok":true,"schema":{...}}` | JSON schema 完全一致 |
| 任意 waveform action | `{"ok":true,"data":{...}}` | **语义等价**（数值相同、time 格式相同、字段顺序相同） |

### 4.3 切换方式

```bash
# 方式 1: 环境变量
export XDEBUG_BIN=${REPO_ROOT}/build/xdebug-fst

# 方式 2: MCP adapter 构造时传参
debug = XverifDebugAdapter(runtime=runtime.with_overrides(
    xdebug_bin="${REPO_ROOT}/build/xdebug-fst"
))

# 方式 3: pytest --xdebug-bin
pytest --xdebug-bin=${REPO_ROOT}/build/xdebug-fst
```

### 4.4 验证清单

- [ ] `xdebug-fst --json -` 接受 `{"action":"actions"}` 返回完整 action 列表（与 xdebug 一致）
- [ ] `xdebug-fst --json -` 接受 `{"action":"schema","args":{"action":"value.at","kind":"request"}}` 返回一致 JSON schema
- [ ] `xdebug-fst --stdio-loop --json` 启动成功，ready 协议一致
- [ ] Session open 成功 (`{"action":"session.open","target":{"session_id":"t","fsdb":"waves.fst"}}`)
- [ ] 所有 action query 返回语义等价结果
- [ ] xverif MCP 不改任何代码，仅换 `XDEBUG_BIN` 即可从 FSDB 模式切换到 FST 模式

---

## Phase 5: 测试 Fixture 全开源化

**工作目录**: `${REPO_ROOT}/testdata/` — 所有 fixture 在此构建

**目标**: 所有测试波形用 **Verilator + FST** 生成，无 Synopsys 依赖。APB/AXI VIP 替换为开源 BFM。

### 5.1 现有 Fixture 分析

| Fixture | 当前方案 | 能否用 Verilator? | 替换策略 |
|---------|---------|------------------|---------|
| `ai_complex_wave` | VCS + SV testbench | ✅ 直接 | Verilator `--trace-fst`，无 VIP 依赖 |
| `stream_v1` | VCS + SV testbench | ✅ 直接 | 同上 |
| `axi_vip_real` | VCS + Synopsys SVT AXI VIP + UVM | ❌ 需替换 | 重写为开源 AXI BFM + Verilator |
| `apb_vip_real` | VCS + Synopsys SVT APB VIP + UVM | ❌ 需替换 | 重写为开源 APB BFM + Verilator |
| `xif_agent_event` | VCS + custom agent | ✅ 可能 | 评估是否依赖 Synopsys 特有 feature |
| `sva_fsdb_npi` | VCS + SVA | ❌ | 开源 SVA 替代方案或用简单 checkers |
| `counter_statistics` | VCS | ✅ 直接 | Verilator |

### 5.2 APB/AXI VIP 替换方案

#### 方案：C++ BFM via Verilator C++ Testbench

**核心理念**: 不用 UVM。写轻量 C++ BFM，直接调用 Verilated DUT 的 pin 级接口，生成 `--trace-fst` 波形。

```
apb_fst_fixture/
├── rtl/
│   └── apb_slave_dut.sv         # 从 xdebug 现有 DUT 移植（无 VIP 依赖）
├── cpp/
│   ├── apb_bfm.h                # APB master BFM (C++)
│   ├── axi_bfm.h                # AXI master/slave BFM (C++)
│   ├── scoreboard.h             # 简单的地址/数据记录+比对
│   └── test_main.cpp            # Verilator testbench 入口
├── Makefile                     # verilator --trace-fst --design-db
└── expected/
    ├── apb_expected_writes.json  # 已知的期望值（用于测试断言）
    └── apb_expected_transfers.json
```

**APB BFM 接口** (C++):

```cpp
class ApbMasterBfm {
    // Drive a write transaction
    void write(uint32_t addr, uint32_t data, int delay_cycles = 0);
    // Drive a read transaction, return data
    uint32_t read(uint32_t addr, int delay_cycles = 0);
    // Generate random traffic with known seed for reproducibility
    void random_traffic(int num_writes, int num_reads, int seed);
};
```

**AXI BFM 接口** (C++):

```cpp
class AxiMasterBfm {
    struct WriteTransaction { uint64_t addr; std::vector<uint32_t> data; uint8_t id; uint8_t len; };
    struct ReadTransaction  { uint64_t addr; uint8_t id; uint8_t len; };

    void write(const WriteTransaction& txn);
    std::vector<uint32_t> read(const ReadTransaction& txn);
    void random_traffic(int num_txns, int num_ids, int seed);
};

class AxiSlaveBfm {
    // Responds to reads/writes with configurable delay behavior
    void set_response_delay(int min_cycles, int max_cycles);
};
```

**优点**:
- C++ BFM 的 transaction 记录可以直接输出为 JSON (axi_handshake.jsonl)
- Verilator `--trace-fst` 直接输出 FST
- `--design-db` 同时输出 DesignDB SO
- 所有输出完全可重现（固定 seed）
- 编译和运行：`verilator --binary --trace-fst --design-db top.sv && ./obj_dir/Vtop`

**关键设计要求**:
1. BFM 的行为必须**确定性可重现**（seed 控制随机延迟）
2. 生成的 FST + DesignDB SO + expected JSON 构成完整的测试 fixture
3. BFM 同时输出 axi_handshake.jsonl（当前 VIP 版输出此文件，用于协议分析测试验证）
4. 协议 corner case 覆盖：outstanding transactions、乱序响应、stall、back-pressure

### 5.3 Fixture 构建流程

```
xdebug_fst/testdata/
├── fixtures/
│   ├── complex_wave/
│   │   ├── rtl/ai_complex_top.sv
│   │   ├── cpp/test_main.cpp
│   │   └── Makefile            # verilator --trace-fst --design-db
│   ├── apb/
│   │   ├── rtl/apb_slave_dut.sv
│   │   ├── cpp/apb_bfm.h, test_main.cpp
│   │   ├── cpp/scoreboard.h
│   │   └── Makefile
│   ├── axi/
│   │   ├── rtl/axi_dut_wrapper.sv
│   │   ├── cpp/axi_bfm.h, test_main.cpp
│   │   ├── cpp/axi_slave_bfm.h, scoreboard.h
│   │   └── Makefile
│   ├── stream_v1/
│   │   ├── rtl/stream_v1_top.sv
│   │   └── Makefile
│   └── counter/
│       ├── rtl/counter_top.sv
│       └── Makefile
└── expected/                    # 每个 fixture 的已知正确输出
    ├── complex_wave_expected.json
    ├── apb_expected.json
    └── axi_expected.json
```

**Makefile 模板** (以 APB 为例):

```makefile
TOP = apb_fixture_top
FST = out/waves.fst
DESIGN_DB = out/libV$(TOP)__DesignDb.so
EXPECTED = out/axi_handshake.jsonl

all: $(FST) $(DESIGN_DB)

$(FST) $(DESIGN_DB): rtl/*.sv cpp/*.cpp cpp/*.h
	verilator --binary --trace-fst --design-db \
	  --top-module $(TOP) \
	  -CFLAGS "-std=c++17" \
	  rtl/apb_slave_dut.sv \
	  rtl/apb_fixture_top.sv \
	  cpp/test_main.cpp \
	  -o $(TOP)
	./obj_dir/$(TOP) +trace
	# Post-check: verify outputs exist and match expectations
	diff <(python3 check_results.py out/) expected/apb_expected.json
```

### 5.4 测试分类与迁移策略

| 类别 | 数量 | 原依赖 | FST 迁移策略 |
|------|------|--------|-------------|
| `contract/` | 8 | FSDB (部分) | 用 FST fixture 替换 FSDB fixture |
| `active_trace_chain/` | 5 | design+waveform | FST + DesignDB SO fixture |
| `combined/` | 3 | 同上 | 同上 |
| `design/` | 1 | NPI design | Verilator DesignDB SO fixture |
| `waveform/` | 2 | FSDB | FST fixture |
| `synthetic/` | 7 | FSDB (VIP 生成) | 重写为开源 BFM 生成的 FST fixture |
| `session/` | 2 | 无 | 直接適用 |
| `native_xout/` | 3 | FSDB (部分) | FST fixture |
| `static/` | 6 | 无 | 直接复用 |
| `mcp/` | 1 | FSDB | 新增 xfst adapter fixture |
| `benchmark/` | 1 | FSDB | 等价 FST |
| `unit/` | 1 | 无 | 直接复用 |
| `runner/` | 7 | N/A | 直接复用 |

### 5.5 conftest.py 扩展

```python
# 新增 FST 二进制 fixture
@pytest.fixture(scope="session")
def xfst_bin(pytestconfig) -> Path:
    return Path(pytestconfig.getoption("--xfst-bin", 
                default=str(REPO_ROOT / "tools" / "xdebug-fst")))

# 新增 FST runner
@pytest.fixture
def fst_cli_runner(xfst_bin, repo_root, isolated_home) -> CliRunner:
    return CliRunner(xfst_bin, cwd=repo_root,
                     base_env={"HOME": str(isolated_home),
                               "XVERIF_HOME": str(repo_root)})

# FST fixtures (复用 xverif_fixture 机制，但产出 FST)
@pytest.fixture
def complex_wave_fst(xverif_fixture) -> Path:
    return xverif_fixture("xdebug_fst.complex_wave") / "out/waves.fst"

@pytest.fixture
def apb_fst(xverif_fixture) -> Path:
    return xverif_fixture("xdebug_fst.apb") / "out/waves.fst"

@pytest.fixture
def axi_fst(xverif_fixture) -> Path:
    return xverif_fixture("xdebug_fst.axi") / "out/waves.fst"

@pytest.fixture
def axi_handshake_jsonl(axi_fst) -> Path:
    return axi_fst.parent / "axi_handshake.jsonl"
```

### 5.6 测试等价性验证

对于每个 action，验证以下等价性：

1. **JSON schema 等价**: `action.schema` 返回一致的结构
2. **JSON 响应等价**: 相同逻辑输入产生语义等价的 JSON output（数值相同、时间格式一致、字段顺序稳定）
3. **XOUT 输出等价**: CLI `xout` 格式文本输出一致
4. **错误处理等价**: 不存在的信号/时间/格式返回相同 error code
5. **确定性可重现**: 相同 fixture + 相同 seed → 相同输出

---

## 实施顺序

```
Week 1: Phase 1 wellen-capi          ───  Rust C FFI 薄层, ~500 行
Week 2: Phase 2 骨架                 ───  main, server, backend 接口,
                                         waveform_backend.h + WellenFstBackend,
                                         design_backend.h + XddDesignBackend
Week 3: Phase 2 波形 actions (简单)   ───  value_at, signal_changes,
                                         scope_list, signal.resolve,
                                         trace.driver, trace.load
Week 4: Phase 2 波形 actions (中等)   ───  signal_statistics, signal_stability,
                                         signal_xz_verify, signal_anomaly_inspect,
                                         clock_point_query, expr_eval_at
Week 5: Phase 2 波形 actions (复杂)   ───  window_verify, counter_statistics,
                                         protocol_handshake_inspect,
                                         signal_sampled_pulse_inspect,
                                         list_first_change, event_find
Week 6: Phase 3 Verilator扩展        ───  port 连接, direction, 语句类型
Week 7: Phase 2 combined actions     ───  trace_active_driver,
                                         trace_active_driver_chain,
                                         trace_x_origin (需要 Phase 3)
Week 8: Phase 2 protocol actions     ───  apb_query, axi_analysis, 等
Week 9: Phase 4 MCP 集成             ───  adapter, capabilities, tools
Week 10: Phase 5 测试迁移            ───  fixtures, conftest, 等价性验证

总计: ~10 周
```

## 验证 Checklist

- [ ] `wellen_capi` 编译通过 (Rust + C header)
- [ ] `xdebug-fst` 编译通过，`--json -` 接受 `actions`/`schema` 请求
- [ ] 打开 FST 文件 + DesignDB SO 创建 session 成功
- [ ] 37 个 waveform actions 全部响应无 crash
- [ ] 5 个 design actions 用 Verilator XddDb 通过
- [ ] `trace.active_driver` 在 FST + DesignDB 上返回正确 driver chain
- [ ] `trace.x_origin` 在 FST + DesignDB 上返回正确 X propagation chain
- [ ] xdebug 全部 contract tests 在 xdebug-fst 上通过
- [ ] xdebug 全部 active_trace_chain tests 通过
- [ ] xdebug 全部 combined tests 通过
- [ ] xdebug 全部 synthetic waveform tests 通过
- [ ] MCP server 可注册 xfst adapter
- [ ] MCP session_open(session_id, fst=path) 成功
- [ ] MCP query 通过 xfst adapter 返回与 xdebug adapter 等价结果
