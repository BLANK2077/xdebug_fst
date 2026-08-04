# xdebug-fst Action Handler 开发指南

> 面向并行子 agent 的接口速查。所有 action 均为 C++17，位于本仓库
> `src/engine/actions/` 下。**禁止修改 `${XDEBUG_ORIGINAL_ROOT}` 任何文件**（只读参考）。

## 1. Handler 基本模式

每个文件实现一个或多个 handler，导出 `make_<name>_handler()` 工厂函数：

```cpp
// src/engine/actions/waveform/foo.cpp
#include "engine/engine_action_handler.h"
#include "engine/engine_globals.h"
#include "api/json_types.h"

namespace xdebug_fst {

struct FooHandler : public EngineActionHandler {
    const char* action_name() const override { return "foo.bar"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }
    Json run(const Json& req) override {
        auto& g = engine_globals();
        if (!g.has_waveform || !g.waveform) {
            return Json{{"ok", false},
                        {"error", {{"code", "WAVEFORM_NOT_LOADED"},
                                   {"message", "action requires waveform file: foo.bar"}}}};
        }
        auto args = req.value("args", Json::object());
        // ... 实现逻辑 ...
        return Json{{"ok", true},
                    {"summary", {{"signal", sig}}},
                    {"data", {{"items", Json::array()}}}};
    }
};

std::unique_ptr<EngineActionHandler> make_foo_handler() {
    return std::make_unique<FooHandler>();
}

} // namespace xdebug_fst
```

注册：在 `src/engine/actions/register_all.cpp` 的 `register_all_actions()`
中调用 `r.add(make_foo_handler());`，并在文件顶部添加
`std::unique_ptr<EngineActionHandler> make_foo_handler();` 前向声明。

`EngineActionHandler` 接口（`src/engine/engine_action_handler.h`）：
- `virtual const char* action_name() const = 0;`
- `virtual bool needs_design() const = 0;`
- `virtual bool needs_waveform() const = 0;`
- `virtual Json run(const Json& request) = 0;`  （`Json = nlohmann::json`）

## 2. 后端接口（只读，不得修改签名）

### IWaveformBackend（`src/backend/waveform_backend.h`）

```cpp
class IWaveformBackend {
    virtual bool open(const std::string& path) = 0;
    virtual void close() = 0;
    virtual bool is_open() const = 0;
    // 时间表
    virtual uint32_t time_count() const = 0;         // 时间点数量
    virtual uint64_t time_at(uint32_t idx) const = 0; // idx → 绝对时间
    virtual uint64_t min_time() const = 0;
    virtual uint64_t max_time() const = 0;
    virtual uint32_t time_idx_of(uint64_t t) const = 0; // 二分：最大 idx 使 time<=t
    virtual std::string format_time(uint64_t t) const = 0;
    // 层级
    virtual uint32_t scope_count() const = 0;
    virtual uint32_t scope_at(uint32_t idx) const = 0;
    virtual uint32_t scope_child_count(uint32_t scope_ref) const = 0;
    virtual uint32_t scope_child_at(uint32_t scope_ref, uint32_t idx) const = 0;
    virtual uint32_t scope_var_count(uint32_t scope_ref) const = 0;
    virtual uint32_t scope_var_at(uint32_t scope_ref, uint32_t idx) const = 0;
    virtual const char* scope_name(uint32_t scope_ref) = 0;
    virtual const char* var_name(uint32_t var_ref) = 0;
    virtual const char* var_full_name(uint32_t var_ref) = 0;
    virtual uint32_t var_signal_ref(uint32_t var_ref) const = 0;
    virtual int var_encoding(uint32_t var_ref, uint32_t* out_width) const = 0;
    // 信号
    virtual uint32_t find_signal(const std::string& path) const = 0; // 0=未找到；大小写不敏感、容忍 TOP. 前缀
    virtual int load_signals(const std::vector<uint32_t>& refs) = 0;
    virtual void unload_signals(const std::vector<uint32_t>& refs) = 0;
    virtual bool is_loaded(uint32_t signal_ref) const = 0;
    struct SignalInfo { uint32_t num_changes, max_states, width, bytes_per_entry; bool has_meta_byte; };
    virtual bool signal_info(uint32_t signal_ref, SignalInfo& out) const = 0;
    struct SignalOffset { uint32_t start; uint16_t elements; bool time_match; uint32_t next_idx; bool has_next; };
    virtual bool signal_offset_at(uint32_t signal_ref, uint32_t time_idx, SignalOffset& out) const = 0;
    virtual std::string signal_value_at(uint32_t signal_ref, uint32_t start, uint16_t element) const = 0;
    virtual std::string signal_value_str(uint32_t signal_ref, uint32_t start, uint16_t element) const = 0;
    // 返回 ASCII 位串（MSB-first，'0'/'1'/'x'/'z'），字符串信号返回原始文本
    virtual void values_at(const std::vector<uint32_t>& refs, uint32_t time_idx,
                           std::vector<std::string>& out_values, std::vector<bool>& out_found) const = 0;
    virtual std::vector<uint32_t> time_indices_of(uint32_t signal_ref) const = 0; // 变化时刻 idx 升序
};
```

**信号值语义**：`signal_value_str()` 返回位串（例如 8 位信号 → "00001011"）。
取某时间点的值流程：
1. `ref = wf.find_signal(name)`
2. `wf.load_signals({ref})`（若 `!wf.is_loaded(ref)`）
3. `ti = wf.time_idx_of(t)`
4. `wf.signal_offset_at(ref, ti, off)` → `off.start`/`off.time_match`/`off.next_idx`
5. `bits = wf.signal_value_str(ref, off.start, 0)`

### IDesignBackend（`src/backend/design_backend.h`）

```cpp
class IDesignBackend {
    virtual bool open(const std::string& so_path) = 0;  // dlopen DesignDB .so
    virtual void close() = 0;
    virtual int signal_count() const = 0;
    virtual int resolve(const char* name) const = 0;      // 索引或 -1
    virtual const char* signal_name(int idx) const = 0;
    virtual const char* signal_type(int idx) const = 0;   // "port"/"reg"/"wire"
    virtual int signal_width(int idx) const = 0;
    virtual const char* signal_file(int idx) const = 0;
    virtual int signal_line(int idx) const = 0;
    virtual int signal_direction(int idx) const = 0;      // 0=unknown,1=input,2=output,3=inout
    struct DriverRecord { int src_signal; std::string kind, file; int line; };
    virtual int trace_driver_count(int signal_idx) const = 0;
    virtual int trace_driver(int signal_idx, std::vector<DriverRecord>& out) const = 0;
    struct LoadRecord { int consumer; std::string kind, file; int line; };
    virtual int trace_load_count(int signal_idx) const = 0;
    virtual int trace_load(int signal_idx, std::vector<LoadRecord>& out) const = 0;
};
```

## 3. 共享工具

### LogicValue（`src/core/value/logic_value.h`）

```cpp
struct LogicValue { std::string bits; int width; bool known, has_x, has_z; };
LogicValue logic_value_from_bits(const std::string& bits, int width);
LogicValue logic_value_from_u64(uint64_t value, int width);
std::string sv_literal(const LogicValue& v, char radix);       // "8'h0b"
std::string render_logic_value(const LogicValue& v, ValueRenderFormat fmt);
Json logic_value_json(const LogicValue& v, ValueRenderFormat fmt = Hex);
// → {"value":"8'h0b","known":true,"width":8,"bits":"00001011"}，unknown 时含 has_x/has_z
enum class ValueRenderFormat { Hex, Bin, Dec };
bool parse_value_render_format(const std::string& text, ValueRenderFormat& out);
```

### ClockSampleScanner（`src/waveform/clock_sampling.h`）

```cpp
struct ClockSample { uint32_t time_idx; uint64_t time; std::string before, middle, after; bool changed_at_edge; };
class ClockSampleScanner {
    ClockSampleScanner(const IWaveformBackend& wf, uint32_t clk_ref, uint32_t target_ref,
                       bool rising = true, bool falling = false);
    size_t scan(uint32_t begin_ti, uint32_t end_ti, std::vector<ClockSample>& out) const;
    const std::vector<uint32_t>& edge_indices() const;
};
PointValues read_point_values(const IWaveformBackend& wf, uint32_t signal_ref, uint32_t time_idx);
```

### ListManager / CursorManager（单例）

```cpp
// src/waveform/list/list_manager.h
ListManager& ListManager::instance();
bool create(name, error); bool add(name, signals, error); bool remove(name, signals, error);
bool load(name, file_path, error); bool validate(name, error);
const SignalList* get(name) const; bool exists(name) const;
std::vector<std::string> names() const; void clear();

// src/waveform/cursor/cursor_manager.h
CursorManager& CursorManager::instance();
bool set(name, time); bool get(name, WaveformCursor&) const;
bool remove(name); bool use(name, uint64_t&) const;
std::vector<WaveformCursor> all() const; void clear();
```

### 表达式（`src/waveform/expr/expr_eval.h`）

```cpp
ExprNode* parse_expression(const std::string& text, std::string& error); // delete 归还
LogicValue eval_expression(const ExprNode* root, const IWaveformBackend& wf,
                           uint32_t time_idx, std::map<std::string, LogicValue>* samples = nullptr);
bool expr_eval_at(const std::string& text, const IWaveformBackend& wf,
                  uint32_t time_idx, LogicValue& out, std::string& error);
std::vector<std::string> expression_signals(const ExprNode* root);
```

支持：信号名、`sig[msb:lsb]`、常量 `8'h05`/`4'b1010`/`42`、一元 `~ ! -`、
二元 `& | ^ && || == != === !== < <= > >= + - * / % << >>`、括号。

## 4. 输出结构约定（与 xdebug 语义对齐）

- 成功：`{"ok":true, "summary":{...}, "data":{...}}`
- 失败：`{"ok":false, "error":{"code":"<CODE>","message":"..."}}`
- 常见错误码：`WAVEFORM_NOT_LOADED`、`DESIGN_NOT_LOADED`、`SIGNAL_NOT_FOUND`、
  `MISSING_FIELD`、`INVALID_TIME`、`LIST_NOT_FOUND`、`UNKNOWN_ACTION`
- 时间统一为整数（FST 原始时间戳），time 字段在 summary/data 中为数字
- 值统一用 `logic_value_json` 渲染

## 5. 参考实现（只读）

`${XDEBUG_ORIGINAL_ROOT}/xdebug/src/engine/service/actions/` 下有 xdebug 的原始
handler（NPI 实现）。**只可阅读参考语义，不得复制其 NPI 调用，不得修改
该目录。** 输出结构可参考其 JSON 字段组织方式。

## 6. 构建与测试

```bash
cd ${REPO_ROOT}
cmake -S . -B build          # GLOB 变更后必须重新 configure
cmake --build build -j4
# 运行（需要 LD_LIBRARY_PATH 指向两个 Rust .so）
LD_LIBRARY_PATH=build:../wellen/target/release:wellenx_capi/target/release \
  ./build/xdebug-fst --stdio-loop --json < req.jsonl
```

测试 fixture：
- FST: `testdata/fixtures/counter/waves.fst`（信号名 `TOP.counter_top.*`、`TOP.*`）
- DesignDB: `testdata/fixtures/counter/obj_dir/libVcounter_top__DesignDb.so`
