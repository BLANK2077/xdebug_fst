# xdebug_oc 全能力兼容实施进度

## 当前状态

- Goal：active（thread `019fe602-0198-7f23-a9a1-bb3c6a539dec`）
- 当前阶段：P2 进行中（registry、真实 engine 与 UDS 已完成首轮集成）
- 当前任务：补齐 UDS idle timeout、失败补偿与 managed lifecycle 对齐
- xdebug-fst 基线：`1009e5c`
- Wellen 分支：`feature/xdebug-fst-capi`，冻结 revision `1d66a9ea5111d1e80d16273a604f92e8c6a51cbd`
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
| P2 | 进行中 | `parity-p2` | registry/generation 与真实 UDS engine 已落地；异常矩阵待完成，TCP/file 已按用户要求裁剪 |
| P3 | 未开始 | `parity-p3` | Wellen 波形语义 |
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
- P2 UDS CTest：真实 fork server/client 往返通过，覆盖 `0600` socket、换行 JSON framing、非法 JSON 拒绝和 listener 隔离。
- P2 lifecycle CTest：真实 `xdebug-fst --server` 子进程完成 `open/list/doctor/kill/list`；覆盖重复名称、公开 action 经 UDS 路由、ownership token mismatch 保活、正确 token 强制清理和 socket 消失。
- 旧 action 测试现状：P1 的严格 request/response gate 已按计划启用，仍使用 `render_format`、平铺 `begin/end`、旧 config shape 或旧成功响应 shape 的测试会 fail closed；这些不是 P1 协议回退点，将在 P3/P5 对应 action 实现迁移时逐组改正并恢复全量绿色。
- 环境记录：系统 `pytest`/`python3 -m pytest` 缺少 pytest；按仓库 `HANDOFF.md` 使用已记录的 xverif Python 环境运行同一测试层，没有更换 backend、数据或测试内容，也未进行沙箱外重试。

## 剩余差异

P0、P1 已关闭。P2 已完成 registry/generation、真实 engine 子进程、UDS 与严格 DesignDB bundle 首轮实现，旧单进程 session 和邻近目录 `.so` 猜测已删除。2026-08-09 用户明确裁剪 TCP 与 file transport，因此二者不再开发或作为验收门禁；schema enum 保留，实际选择必须 fail closed 且不得 fallback。下一项是 UDS idle timeout、启动崩溃与 cleanup_failed 补偿矩阵，以及 MCP direct/fake-LSF 生命周期；严格 validator 和 response gate 保持开启，不为旧测试放宽 schema。
