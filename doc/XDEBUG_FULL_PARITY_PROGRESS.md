# xdebug_oc 全能力兼容实施进度

## 当前状态

- Goal：active（thread `019fe602-0198-7f23-a9a1-bb3c6a539dec`）
- 当前阶段：P3 已完成（Wellen FST 波形语义）
- 当前任务：进入 P4，先以失败差分判断是否确有必要修改 Verilator
- 全局硬门禁：生产、回归和最终验收只打开 FST 波形；VCD 仅可作为可重复生成 FST 的源文件，禁止作为输入或 fallback
- xdebug-fst 当前功能提交：`f61670a`；当前测试提交：`e1779c9`
- Wellen 分支：`feature/xdebug-fst-capi`，冻结 revision `066d86ad26e82ae02407ad2a64c5a226b8ebe212`
- Verilator 分支：`feature/design-db-for-xdebug`，冻结 revision `e04eb0ea8203028490400172396add8ec458932b`
- 原版 xdebug runtime revision：`8eecf71271cc523d93bf03f6b9f9b6fa04ed3ee8`
- 原版 xdebug runtime build ID：`8eecf71271cc-c45099040abf3dbe194d3ba27c207d7637b39ba9f9d662fad3d9d50dda99fb2c`
- 原版 schema revision：`c45099040abf3dbe194d3ba27c207d7637b39ba9f9d662fad3d9d50dda99fb2c`
- 冻结 action 数量：严格 73 个；schema 文件：282 个，其中 public action schema：146 个

## 阶段状态

| 阶段 | 状态 | Tag | 验收摘要 |
| --- | --- | --- | --- |
| P0 | 已完成 | `parity-p0` | 基线、依赖锁、Wellen/Verilator 独立回归和漂移检查均通过 |
| P1 | 已完成 | `parity-p1` | 73 action、146 schema、请求/响应校验、canonical JSON/XOUT 与 stdio-loop 全部对齐 |
| P2 | 已完成 | `parity-p2` | registry、真实 UDS engine、来源清单、批量生命周期、异常补偿及 MCP direct/fake-LSF 全部通过；TCP/file 按用户要求裁剪且无 fallback |
| P3 | 已完成 | `parity-p3` | FST-only Wellen 层级、时间、类型、delta、采样、批量与完整性已实现；Rust/C/C++、7 CTest、基线与双 C ABI 门禁全部通过 |
| P4 | 未开始 | `parity-p4` | 克制扩展 DesignDB |
| P5 | 未开始 | `parity-p5` | 全部公共 Action |
| P6 | 未开始 | `parity-p6` | Active Driver 与 X Origin |
| P7 | 未开始 | `parity-p7` | 全量差分与最终交付 |

## Commit 记录

- `00b5d9f`：建立完整任务书、阶段边界和分批提交规则。
- `630c436`：冻结原版 xdebug v1 runtime、engine、schema、73 action catalog 与许可证边界。
- Wellen `dba5242`：建立可测试的 Wellen C 波形访问接口。
- Wellen `1d66a9e`：修复 1 基 signal reference 并增加 Rust/C ABI 端到端验证。
- Verilator `80c4226ae`：增加显式、只读、最小化 DesignDB 生成接口。
- Verilator `e04eb0ea8`：修复 DesignDB 全量回归隔离并覆盖七类设计。
- `9a529cc`：统一 `wellenx_capi` 与 Wellen C ABI 的 1 基信号句柄编码。
- `5b2595a`：锁定 Wellen、Verilator revision、ABI header 和 release library。
- `fad3309`：说明 Verilator DesignDB、Wellen 波形后端和 xdebug-fst 组合架构。
- `2f04051`：建立冻结文件、依赖、catalog、schema 和现场原版的自动漂移检查。
- `ce1a300`：迁移 MIT JSON Schema validator、runtime validator 和诊断错误核心。
- `89f740b`：迁移严格 73 action 公共注册表、完整 metadata、过滤和 modes。
- `a97a01b`：统一 canonical response/error envelope，并冻结 215 个 schema 引用示例。
- `eb00b49`：在 handler 前执行 action-specific 严格请求校验，batch 子请求同样 fail closed。
- `bf5c7fc`：在输出前执行响应合同校验，并遍历验证全部冻结请求/响应示例。
- `8994fc6`：对齐 one-shot JSON、默认 XOUT、退出码和 stdio-loop wire protocol。
- `f67e6f9`：迁移多会话注册表、严格 endpoint 合同与 generation 生命周期状态机。
- `42876c0`：实现真实 UDS engine 子进程、公开 session 路由与 generation-safe 资源释放。
- `d5d77ea`：按用户范围裁剪 TCP/file transport，并登记明确拒绝且无 fallback 的门禁。
- `cb024c9`：对齐 session doctor、idle 回收、崩溃清理、fingerprint 复检与 cleanup_failed 补偿。
- `b4a579e`：覆盖 UDS 会话启动、并发、崩溃、资源变化、超时与 stdio 父子进程生命周期。
- `0d7a3ed`：建立真实 xverif MCP direct 与 fake-LSF 生命周期回归。
- `f4a0713`：实现 run manifest 来源校验、同资源 advisory 与 close/kill all 批量语义。
- `a7af5e2`：覆盖 waveform/combined manifest、来源错配和批量会话清理。
- Wellen `c5132ef`：区分根层级和递归层级，发布 scope full name。
- Wellen `76e5077`：通过 C ABI 发布 FST timescale。
- Wellen `066d86a`：保真发布 bit-vector、real、UTF-8 string 与 event 类型和值。
- `be9b36b`、`5edeafc`：统一 Wellen 信号句柄、递归层级、alias 消歧及其回归。
- `0507cb1`、`5061ef9`：实现并验证严格 timescale 时间解析与渲染。
- `974bad8`、`e45eb6a`：实现并验证四态及非位值类型。
- `f61670a`：实现 FST delta、raw/before/after、时钟采样、类型化批量值、变化游标和范围扫描。
- `e1779c9`：建立生产与测试 FST-only 门禁并覆盖真实 FST 边界。

## 测试记录

- Wellen：`cargo test --workspace --all-targets` 通过；核心单元、diff、stream 和新增 C API 测试均无失败。
- Wellen C ABI：`make -C wellen_capi/test` 通过，真实覆盖首个原生 `SignalRef(0)` 对应 C reference 1。
- `wellenx_capi`：`cargo test --manifest-path wellenx_capi/Cargo.toml --all-targets`，2 个测试通过。
- Verilator：7 个相互隔离的 DesignDB 回归全部真实执行通过，未发生共享 prefix/obj_dir 或 skip 冒充通过。
- Verilator：distribution copyright/license 检查通过，`make -C src -j2` 通过。
- xdebug-fst：`cmake -S . -B build && cmake --build build -j2` 通过，配置阶段成功校验全部依赖锁。
- xdebug-fst：`python3 tools/check_compat_baseline.py --original-root ${XDEBUG_ORIGINAL_ROOT}` 通过，确认冻结文件、catalog、schema、依赖和现场原版均未漂移。
- xdebug-fst：`${XFST_CONDA_ENV} -m pytest tests/ -p no:xverif -q`，122 个测试通过。
- P1 CTest：真实加载 105 个 request 示例和 110 个 response 示例，215 个示例逐一通过对应 runtime schema；同时验证冻结原版 actions response。
- P1 protocol：`python3 tools/check_p1_protocol_parity.py --original-root ${XDEBUG_ORIGINAL_ROOT}` 通过，原版与 xdebug-fst 的 73 action catalog、73×2=146 个 schema action 完整 JSON 响应完全一致。
- P1 CLI：one-shot JSON、actions/schema/error XOUT、INVALID_JSON、退出码、stdio-loop ready、payload override、错误双载荷和 quit envelope 与原版比较一致；PID 是唯一易变字段。
- P1 pytest：13 项 CLI/request/catalog 专项测试通过；common/waveform 中已改用正式公共合同的 21 项基础测试在启用 response gate 前通过。
- P2 registry CTest：覆盖 opening→active CAS、重复名称、双 registry 实例、单调 touch、generation mismatch、条件删除和严格 endpoint round-trip。
- P2 UDS CTest：真实 fork server/client 往返通过，覆盖 `0600` socket、换行 JSON framing、非法 JSON 拒绝、listener 隔离、非正 timeout 拒绝和断连写入不触发 SIGPIPE。
- P2 lifecycle CTest：真实 `xdebug-fst --server` 子进程覆盖 TCP/file 明确拒绝且无 fallback、非法 timeout 环境、严格私有控制合同、waveform 与 combined run manifest、来源摘要不一致证据、同资源 advisory、重复名称、双前端并发 open、close/kill all、启动提前退出补偿、公开 action 经 UDS 路由、ownership token mismatch 保活、正确 token kill、engine SIGKILL 后 doctor/gc、FST fingerprint 变化、idle list 回收，以及同一 stdio-loop 内 open/close 的 child reap；最终 socket 与 active registry 均清空。
- P2 MCP direct CTest：真实加载 xverif MCP adapter，使用当前 `xdebug-fst` 完成 73 action one-shot catalog、managed stdio `open/doctor/list`、公开 action UDS 路由和 `close`，wrapper 与 native registry 均清空。
- P2 fake-LSF CTest：使用 xverif 自带 fake bsub/bkill 和当前 `xdebug-fst`，真实覆盖 stdout scheduler noise、job id 识别、managed `open/doctor/close`、bkill 日志与 native registry 清空；未调用真实 LSF，也未切换 backend。
- P3 Wellen：`cargo test -p wellen-capi` 3 项通过，C ABI `make` 通过；根/递归层级、1ns timescale、bit/string/real/event 类型化值均有真实断言。
- P3 xdebug-fst：7/7 CTest 通过；`wellen-fst-backend` 的所有实际波形参数均以 `.fst` 结尾，真实覆盖 1ns/1ps、深层 leaf、alias、64 位 X、Z、UTF-8 string delta、real、event、raw/before/after、时钟采样、批量 load/unload/value、变化游标、范围扫描和完整性诊断。
- P3 阶段总验收：Wellen Rust 3/3、Wellen C ABI、wellenx 2/2、xdebug-fst 7/7 CTest 和 `check_compat_baseline.py` 全部通过；xdebug-fst、Wellen、Verilator 三仓状态均干净，Verilator 仍停留在 `e04eb0ea8`，P3 未对其增加任何修改。
- 旧 action 测试现状：P1 的严格 request/response gate 已按计划启用，仍使用 `render_format`、平铺 `begin/end`、旧 config shape 或旧成功响应 shape 的测试会 fail closed；这些不是 P1 协议回退点，将在 P3/P5 对应 action 实现迁移时逐组改正并恢复全量绿色。
- 环境记录：系统 `pytest`/`python3 -m pytest` 缺少 pytest；按仓库 `HANDOFF.md` 使用已记录的 xverif Python 环境运行同一测试层，没有更换 backend、数据或测试内容，也未进行沙箱外重试。

## 剩余差异

P0、P1、P2、P3 已关闭。P3 的 FST 后端事实层已实现递归层级、alias、timescale、严格时间、四态/real/string/event、delta、raw/before/after、时钟采样、批量访问和完整性诊断，且生产与测试均禁止直接读取 VCD/FSDB 或 fallback。下一步进入 P4：必须先用公开 action 的失败差分证明现有 XDD ABI 无法提供必要事实；如果 xdebug-fst 组合现有 FST 与 XDD 即可完成，则不修改 Verilator。随后 P5 必须把全部公开 action 迁移到这些统一语义。2026-08-09 用户明确裁剪 TCP 与 file transport，因此二者不再开发或作为验收门禁；schema enum 保留，实际选择必须 fail closed 且不得 fallback。严格 validator 和 response gate 保持开启，不为旧测试放宽 schema。
