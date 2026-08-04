# 子 Agent 任务书（xdebug-fst action 实现）

你是 xdebug-fst 项目的实现子 agent。请严格按本任务书 + `docs/DEV_GUIDE.md`
完成分配给你的 action handlers，**只读参考** `${XDEBUG_ORIGINAL_ROOT}/xdebug/src/` 下的
xdebug 源码（NPI 实现），**绝不修改 xverif 任何文件**。

## 工作目录

```
${REPO_ROOT}
```

## 必读文件（按顺序）

1. `docs/DEV_GUIDE.md` — handler 模式、后端接口、共享工具、输出约定
2. `src/engine/actions/waveform/value_at.cpp` — 已实现的 handler 范例
3. `src/engine/actions/design/signal_resolve.cpp` — design handler 范例
4. `src/engine/actions/register_all.cpp` — 注册机制（不要改它，用你自己的注册函数）
5. `src/backend/waveform_backend.h`、`src/backend/design_backend.h` — 接口
6. `src/core/value/logic_value.h`、`src/waveform/clock_sampling.h`、
   `src/waveform/list/list_manager.h`、`src/waveform/cursor/cursor_manager.h`、
   `src/waveform/expr/expr_eval.h` — 共享工具

## 参考实现（只读）

xdebug 参考源码在 `${XDEBUG_ORIGINAL_ROOT}/xdebug/src/engine/service/actions/`（各 action
的 `run()` 入口）和 `${XDEBUG_ORIGINAL_ROOT}/xdebug/src/waveform/server/service/`
（`ai_*` 语义实现，如 `signal_analysis.cpp`、`query_actions.cpp`）。用 grep/sed
提取每个 action 的：入参、输出 JSON 字段、错误码、边界语义。**只借鉴语义与
JSON 结构，不要复制 NPI 代码。**

## 交付要求

1. 在 `src/engine/actions/<你的组目录>/` 下新建实现文件（建议按 action 分组，
   一个文件可含多个 handler）。
2. 每个文件底部导出 `make_<name>_handler()` 工厂函数（与 value_at.cpp 一致）。
3. 新建一个 `src/engine/actions/<组名>_register.cpp`，内容：
   ```cpp
   // <组名>_register.cpp — 注册本组所有 handler
   #include "engine/action_registry.h"
   namespace xdebug_fst {
   // 前向声明 ...
   void register_<组名>_actions(ActionRegistry& r) {
       r.add(make_xxx_handler());
       ...
   }
   } // namespace xdebug_fst
   ```
   **不要修改 register_all.cpp 和 CMakeLists.txt**（主 agent 负责集成）。
4. 编译检查（每个文件）：
   ```bash
   cd ${REPO_ROOT}
   g++ -std=c++17 -fsyntax-only -Isrc -Ithird_party -I../wellen/wellen_capi/include -Iwellenx_capi/include src/engine/actions/你的文件.cpp
   ```
   必须零错误通过。若有报错，修复后再交付。
5. 不允许修改：`src/backend/*.h`、`src/core/value/logic_value.h`、
   `src/waveform/clock_sampling.h`、`src/waveform/list/list_manager.h`、
   `src/waveform/cursor/cursor_manager.h`、`src/waveform/expr/expr_eval.h`、
   `src/server.cpp`、`src/main.cpp`、`CMakeLists.txt`。若确需新共享设施，
   在自己的文件内实现（static 函数或局部类）。

## 通用语义要点（所有 action 共有）

- 时间入参 `begin`/`end`/`time` 均为整数（FST 原始时间戳），缺省 begin=0、
  end=max_time。用 `wf.time_idx_of(t)` 换算 time_idx。
- 信号名查找一律 `wf.find_signal(name)`（大小写不敏感、容忍 `TOP.` 前缀）。
- 值渲染用 `logic_value_json(LogicValue, fmt)`，fmt 来自 `args.render_format`
  （hex/bin/dec，默认 hex）。
- 输出结构：成功 `{"ok":true,"summary":{...},"data":{...}}`；
  失败 `{"ok":false,"error":{"code":"...","message":"..."}}`。
- 每个 action 都要检查 `needs_waveform()`/`needs_design()` 对应后端是否加载
  （`g.has_waveform`/`g.has_design`），未加载返回对应错误码。
- 变化迭代用 `wf.time_indices_of(ref)`（升序 time_idx），逐点
  `wf.signal_offset_at(ref, ti, off)` + `wf.signal_value_str(ref, off.start, 0)`。

## 完成后报告（返回 JSON）

```json
{
  "group": "<组名>",
  "implemented": ["action1", "action2", ...],
  "files": ["src/engine/actions/...", ...],
  "register_function": "register_<组名>_actions",
  "compile_check": "all pass" ,
  "notes": "实现要点/与参考的差异"
}
```
