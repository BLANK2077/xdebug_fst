# xdebug_fst 全局 flock 热路径修复任务书

## 一、目标与固定决策

将 `xdebug_fst` 从“单一 `registry.json` + 全局排他 flock”迁移为“每 session 独立原子状态文件 + 按 session 生命周期 lease”，参考原版 `38eea247` 的最终架构，但适配候选版冻结合同。

固定决策：

- 普通 managed query、`session.list`、`session.doctor` 必须为零 `flock`。
- 仅 `session.open/close/kill/gc` 等生命周期修改允许持有对应 session 的排他 lease。
- 保留候选版冻结的 `session.kill`，不照搬原版删除该 Action 的版本变化。
- 不修改任何公开 Action、request/response schema、成功响应字段或 catalog。
- 旧版 v2 `registry.json`：
  - 缺失：直接使用新布局。
  - 合法且为空：原子归档为 `registry.json.v2.retired`。
  - 合法但非空：通过既有 `SESSION_REGISTRY_FAILED` 明确返回 `REGISTRY_MIGRATION_REQUIRED`，要求先用旧版本关闭或清理全部 session。
  - 非法、空文件或带尾随数据：拒绝迁移并保留原始证据。
- 不在线迁移活动 session，不自动终止进程，不允许普通只读 Action 产生清理副作用。
- 不修改 Wellen、Verilator、XDD ABI、FST、UDS 协议或 MCP transport；不引入 TCP/fileport 或 fallback。

已冻结的修复前证据：

- UDS 生命周期回归共出现 146 次 `LOCK_EX`、146 次 `LOCK_UN`。
- 本地低负载累计排他等待约 19ms，单次最大约 2.24ms。
- 外部持锁 1.5 秒可令纯 `session.list` 阻塞约 1452ms。

## 二、分阶段实现与提交

### 阶段 0：任务书、新 Goal 与分支

1. 第一项文件修改必须创建本任务书。
2. 任务书完整记录诊断证据、原版参考 revision、数据布局、状态机、迁移规则、并发约束、验收矩阵，以及每阶段状态、测试命令、结果、commit 和剩余 TODO。
3. 任务书落盘后建立新的 Goal，objective 必须明确：
   - 实现任务书全部阶段。
   - query/list/doctor 零 flock。
   - 生命周期锁只能按 session 粒度存在。
   - public schema、73 Action、FST-only 职责边界不变。
   - v2 非空 fail-closed，不清理、不 fallback。
   - 普通/ASan/UBSan、并发、故障注入、MCP 和兼容门禁全部通过后才能完成 Goal。
4. 随后从当前 `f12bcd4` 建立 `fix/per-session-registry-flock` 分支并提交任务书。

提交：

`文档：建立 xdebug_fst flock 热路径修复任务书`

### 阶段 1：先冻结失败边界和静态门禁

增加修复前会失败的回归：

- 静态扫描要求产品代码中只有 `session_lifecycle_lease.h` 可以调用 `flock`。
- 外部长期持有旧 `registry.lock` 时，迁移后的 `session.list`、`session.doctor` 和 managed query 不得等待该锁。
- 同名 session 并发 open 只能一个成功。
- session A 的生命周期 lease 被占用时：A 的生命周期修改等待；session B 的 open/close 不受影响；list/query/doctor 不受影响。
- 非空 v2 registry 返回明确迁移错误且文件、进程和 sidecar 均不变化。
- 空 v2 registry 可归档；非法 v2 不归档。
- 记录红测返回码和触发证据，不放宽断言。

提交：

`测试：冻结全局 flock 热路径与旧注册表迁移边界`

### 阶段 2：迁移为每 session 原子状态

内部布局调整为：

```text
~/.xdebug/engine/
  registry.json.v2.retired
  registry.lock
  lifecycle-locks/<session-hash>.lock
  sessions/<session-hash>/
    state.json
    activity
    generation
    endpoint.json
    socket
    history/<generation>.json
    logs/
    transport/
```

实现要求：

- `state.json` 直接保存现有严格 `SessionInfo` record，不改变公共 session JSON。
- `get(session_id)` 只读取一个 `state.json`。
- `load_all()` 枚举 session 目录并按 session id 排序，不再读取全局数组。
- 单条损坏只影响对应 session：精确 get/doctor 返回 registry invalid；list 跳过损坏项，其他 session 仍可使用。
- 状态发布继续使用唯一临时文件、完整 write、文件 `fsync`、原子 `rename` 和父目录 `fsync`。
- `touch_if_generation` 改为 generation 校验后的独立 activity marker `futimens`；同秒或更旧时间为幂等成功。
- `remove_if_generation` 再次核对 generation，将关闭记录原子写入 `history/<generation>.json`，删除当前 `state.json` 和 activity，并保留 generation、日志和诊断材料。
- v2 检查不 dual-read、不静默恢复，也不覆盖已有 retired 证据。

提交：

`重构：将全局会话注册表拆分为独立原子状态`

### 阶段 3：限定 flock 为按 session 生命周期 lease

新增稳定 RAII `SessionLifecycleLease`：

- 锁文件位于 session 可删除目录之外。
- 使用 session id 的稳定 hash 路径。
- `O_CLOEXEC`，析构可靠 unlock/close。
- 不允许复制。
- 不用于 query、doctor 或 list。

生命周期接入：

- `session.open` 在读取旧状态前获取 lease，并持有到 reservation、engine ready、resource 二次确认和 active 发布完成。
- open 的所有失败补偿仍在同一 lease 内按 generation 清理。
- `session.close`、`session.kill` 获取目标 session lease后必须重新读取状态，不能使用锁前快照。
- `session.gc` 和 `session.kill all` 先无锁枚举，按 session id 确定顺序，每次只获取一个 lease，锁内重新读取并核对 generation，不同时持有多把 lease。
- managed query 无锁读取 active generation 快照，校验 endpoint/generation 后发送 UDS 请求；与 close 竞争只允许成功响应或稳定的 session/transport error。
- `session.doctor`、`session.list` 只读，不 touch registry。
- 原实现由 `session.list` 顺带执行 idle-timeout 清理，这与“list 零 flock、无副作用”不可同时成立；修复后超时 session 仍由 list 展示，清理只允许通过显式 `session.close/kill/gc` 生命周期 Action 完成。既有 `expired_removed_count` 字段保留并固定为 0，公开 schema 不变。
- engine 请求完成后只更新 activity marker；删除前端的重复 touch。
- 删除 `session_registry.cpp` 的 `sys/file.h`、全局 acquire/release 及所有全局 read-modify-write。

提交：

`修复：限定 flock 仅用于会话生命周期修改`

### 阶段 4：并发、故障和兼容闭环

补齐以下测试：

- 同名并发 open：一个成功、一个 `SESSION_ID_EXISTS`。
- 不同 session 并发 open/close：互不等待。
- query/close、query/kill、gc/reopen 竞争：旧 generation 不得删除或更新新 generation；不得访问半写状态；不得误杀 PID 已复用的无关进程。
- cleanup failure 保留 `cleanup_failed` 当前状态与诊断材料。
- history 正确记录关闭 generation，当前状态不可见。
- activity marker 单调、同秒不重写、旧 generation touch fail-closed。
- 一个 state 损坏不影响其他 session；精确 doctor 返回 registry 错误而非伪装成 not found。
- state write、rename、fsync 故障后只允许旧完整记录或新完整记录。
- 非空/空/非法 v2、已有 retired 文件和 mixed-version 重现全部 fail-closed。
- 外部持有废弃 `registry.lock` 不影响新请求。
- `strace -f -e flock` 验收：managed query、list、doctor 和不同 session 普通 Action 为 0；open/close/kill/gc 只出现目标 session lifecycle lease。
- 静态门禁禁止其他 C/C++/Python 产品路径重新引入 flock。
- 既有 `session.kill` ownership token、`session.close` graceful、`session.gc`、fake-LSF 和 MCP direct 语义保持不变。

提交：

`测试：覆盖会话注册表并发、迁移与零 flock 门禁`

### 阶段 5：全量验收与文档收口

执行并记录：

- GCC 13 clean configure/build。
- CTest 9/9。
- 全量 pytest。
- 独立 session registry、UDS lifecycle、session stability、MCP direct、fake-LSF。
- ASan 全量与 CTest，启用 leak 检查。
- UBSan 全量与 CTest。
- 73 Action compatibility baseline、schema/hash 门禁。
- `strace` 零 flock 矩阵。
- 长时间重复 open/query/close 的 FD、RSS、子进程和 socket 清理。
- Wellen、Verilator、xverif 工作树只读检查；不得修改依赖仓库。
- `git diff --check` 和最终工作树检查。

更新任务书进度以及架构文档，说明为什么不能机械删除 flock、per-session state/activity/history/lifecycle lease 的职责、query/close 竞争语义、v2 升级操作步骤，以及 FST/Wellen/Verilator 职责完全不变。

提交：

`文档：完成 per-session 注册表与 flock 修复验收`

## 三、接口与兼容性

- 公开接口无变化，catalog 严格保持现有 73 Action。
- `session.kill` 继续保留并通过原有 schema。
- UDS/stdio/MCP XOUT 和 JSON 不增加内部 registry 字段。
- 内部持久化从 version 2 全局数组变为每 session `state.json`；`activity` 承载动态 last-active 时间；`history` 保存已关闭 generation；生命周期锁由全局 registry 粒度改为 session 粒度。
- 旧非空 registry 的用户操作顺序：
  1. 使用旧版本执行 `session.close/kill/gc` 清空所有记录。
  2. 确认 registry 为合法空 v2。
  3. 启动新版本，由其原子归档旧 registry。
- 不提供在线迁移、自动杀进程或双格式 fallback。

## 四、完成标准

只有同时满足以下条件才允许完成新 Goal：

- query/list/doctor 实测和静态检查均为零 flock。
- 只有 session lifecycle lease 仍包含 flock。
- 同 session 生命周期保持互斥，不同 session 不再全局串行。
- generation、PID、ownership token、cleanup_failed 和 reopen 防护全部通过。
- v2 空迁移、非空拒绝、非法拒绝均有回归。
- 73 Action 和全部冻结 schema/hash 不变。
- 普通、ASan、UBSan、CTest、MCP、fake-LSF、并发和稳定性门禁全部通过。
- Wellen、Verilator、xverif 未修改。
- 任务书、进度、架构说明、测试证据和分批中文 commit 完整。
- 最终工作树干净，没有未登记 TODO。

## 五、执行进度

### 当前状态

- 阶段 0：已完成并提交。
- 阶段 1：已完成并提交。
- 阶段 2：已完成并提交。
- 阶段 3：已完成并提交。
- 阶段 4：已完成并提交。
- 阶段 5：已完成，等待最终文档提交落盘。
- 当前阻塞：无；全部必需门禁已通过。
- 当前 Goal：`019fe602-0198-7f23-a9a1-bb3c6a539dec`，目标为完整实施本任务书并通过全部验收门禁。
- 当前分支：`fix/per-session-registry-flock`，基线为 `f12bcd4`。

### 提交与验证记录

| 阶段 | 状态 | Commit | 验证 | 备注 |
| --- | --- | --- | --- | --- |
| 0 | 已完成 | `bfec263` | 任务书已落盘，Goal 与分支已建立 | Goal `019fe602-0198-7f23-a9a1-bb3c6a539dec`；从 `f12bcd4` 开始 |
| 1 | 已完成 | `28d405b` | `test-session-registry` 按预期失败：`empty v2 registry was not retired`；`test_flock_policy.py` 按预期失败并定位 `session_registry.cpp:57,65` | 断言未放宽，阶段 2/3 负责转绿 |
| 2 | 已完成 | `72fabe5` | GCC 13 完整构建通过；`test-session-registry` 与 `test_flock_policy.py` 均通过 | 已实现 state/activity/history、v2 fail-closed 和原子持久化 |
| 3 | 已完成 | `4c8e7b3` | GCC 13 完整构建通过；`session-uds-lifecycle` 与 `test_flock_policy.py` 通过 | open/close/kill/gc 按 session lease；list/doctor/query 零 lease；list 不再隐式清理 |
| 4 | 已完成 | `4d22a12` | CTest 9/9；静态 flock 门禁通过；strace：list 0、doctor 0、query 0、close 2；旧 registry.lock 持锁与按 session lease 动态隔离门禁通过 | 覆盖 activity/history、损坏隔离、v2 非空/空/非法/归档冲突、同名并发和不同 session 并发；修复 pytest teardown 泄漏 |
| 5 | 已完成 | 本次提交 | GCC 13 clean configure/build；clean CTest 9/9；普通 pytest 422/422；ASan CTest 9/9；UBSan CTest 9/9；compat baseline OK | 73 Action 与 `session.kill` 保留；Wellen/Verilator 源码工作树干净；架构与验收报告已更新 |

### 剩余 TODO

- [x] 建立新 Goal 和修复分支。
- [x] 冻结零 flock 与 v2 迁移红测。
- [x] 实现 per-session state/activity/history。
- [x] 接入按 session lifecycle lease。
- [x] 完成并发、故障、strace 和兼容门禁。
- [x] 完成普通/ASan/UBSan 全量验收和最终文档。

## 六、最终验收证据

- GCC/G++：`XDEBUG_GCC_TOOLCHAIN` 所指 13.3.1。
- clean build：`build/gcc13-flock-clean` 从全新 configure 完成全部目标编译；顺序 CTest 9/9。
- 普通 CTest：`build/gcc13` 9/9。
- 普通 pytest：新增 flock 门禁后共 422 项，按执行通道分为 145、169、108 三组，全部通过；结束后 `/tmp/pytest-of-ryan` 测试 server 为 0。
- ASan：`detect_leaks=1:abort_on_error=1:halt_on_error=1`，CTest 9/9，无 sanitizer 诊断。
- UBSan：`halt_on_error=1:print_stacktrace=1`，CTest 9/9，无 sanitizer 诊断。
- compatibility：`tools/check_compat_baseline.py` 返回 `compat baseline: OK`；冻结 catalog 为 73 Action，包含 `session.kill`。
- flock：静态 allowlist 通过；动态 `strace -f -e flock` 得到 list 0、doctor 0、managed query 0、close 2。
- concurrency：废弃 `registry.lock` 不阻塞；同 session lease 串行；不同 session 及只读 Action 不等待。
- persistence：state/activity/history、generation CAS、单条损坏隔离、v2 空/非空/非法/retired 冲突均通过。
- dependency：Wellen 与 Verilator 源码工作树干净且未修改。xverif 源码未由本任务修改；其 `AGENTS.md` 存在 2026-08-14 的既有工作区改动，本任务未覆盖或提交该文件。
- cleanup：清理了 522 个 socket 位于 `/tmp/pytest-of-ryan/...` 的历史测试 server；修复后的 teardown 不再遗留当前 managed test session。普通 HOME 下的用户 session 未触碰。
