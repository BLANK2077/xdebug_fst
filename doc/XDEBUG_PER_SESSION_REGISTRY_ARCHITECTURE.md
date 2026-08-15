# xdebug-fst 每会话注册表与生命周期锁架构

## 一、修改目的

旧实现把所有 managed session 放在一个 `registry.json` 数组中。任何 get、list、touch、open、close 都先对同一个 `registry.lock` 执行排他 `flock`，再读取或重写整份文件。一次 managed query 会经过 frontend get、engine touch、frontend touch，因而把普通分析请求也串行化到全局锁上。

修复前 UDS 生命周期回归记录到 146 次 `LOCK_EX` 和 146 次 `LOCK_UN`；外部持有 `registry.lock` 1.5 秒时，纯 `session.list` 会等待约 1452ms。这不是 FST 或 action 分析性能问题，而是 session 元数据架构把只读请求错误地放进了全局临界区。

本次修改参考原版 xdebug `38eea247` 的 per-session 思路，但保留 xdebug-fst 已冻结的公开合同、`session.kill`、UDS-only transport 和 v2 注册表版本边界。目标不是机械删除锁，而是把锁缩小到真正需要互斥的同名 session 生命周期修改。

## 二、最终数据布局

```text
~/.xdebug/engine/
  registry.json.v2.retired
  registry.lock                       # 只作为废弃证据保留，不再读取或加锁
  lifecycle-locks/
    <session-hash>.lock               # 稳定的同名 session 生命周期 lease
  sessions/
    <session-hash>/
      state.json                      # 当前 generation 的低频生命周期状态
      activity                        # 高频 last_active，仅使用 mtime
      generation                      # engine/artifact generation guard
      endpoint.json
      socket
      history/
        <generation>.json             # 已关闭 generation 的完整审计记录
      logs/
      transport/
```

`state.json` 直接保存既有严格 `SessionInfo` record，不向公开 JSON/XOUT 暴露内部路径或新字段。每次发布使用唯一临时文件、完整 write、文件 `fsync`、原子 `rename` 和父目录 `fsync`。`load_all` 枚举各 session 的状态并按 `session_id` 排序；一个损坏文件只会被列表跳过，精确 get/doctor 则返回 `SESSION_REGISTRY_FAILED`，不会伪装成 session 不存在。

## 三、state、activity、history 的职责

`state.json` 只承担 opening、active、cleanup_failed 等生命周期事实以及 generation、PID、资源 fingerprint、ownership token hash。它不再因每个分析请求更新 last-active 而整文件重写。

`activity` 是独立 marker。engine 完成 UDS 请求后，先读取当前 state 并核对 generation，再用 `futimens` 单调推进 mtime；同秒或更旧时间幂等成功。旧 generation 无权更新新 session 的 activity。frontend 不再重复 touch，`session.list` 和 `session.doctor` 也不制造写副作用。

`history/<generation>.json` 在关闭时保存原记录，并增加 `closed_at` 与 `final_state=closed`。只有 history 原子发布成功后才删除当前 `state.json` 和 activity；generation marker、日志和诊断材料继续保留，用于防止旧清理误伤新 generation 和支持故障复盘。

## 四、生命周期 lease

唯一允许调用 `flock` 的产品代码是 `SessionLifecycleLease`。锁文件位于 session generation 目录之外，避免清理目录后等待者锁到不同 inode。lease 使用按 session id 稳定生成的路径、`O_CLOEXEC`、不可复制 RAII 对象，并在析构时 unlock/close。

操作边界如下：

| 操作 | 是否持 lease | 规则 |
| --- | --- | --- |
| `session.open` | 是，目标 session | 从读取旧状态到 active 发布及全部失败补偿始终持有 |
| `session.close/kill` | 是，目标 session | 加锁后重新读取 state，再校验 generation/ownership |
| `session.gc`、all | 是，每次一个 session | 无锁枚举，排序后逐项加锁，锁内重读，不同时持有多把锁 |
| `session.list` | 否 | 纯观察，不 touch、不清理 |
| `session.doctor` | 否 | 纯诊断，不 touch |
| managed query | 否 | 读取 active generation 快照并走 UDS；只允许成功或稳定 session/transport error |
| engine activity touch | 否 | generation CAS 后仅更新独立 marker |

持有 session A 的 lease 时，A 的 close/open 必须等待；session B 的生命周期操作以及 A/B 的 list、doctor、query 均不等待。query 与 close 竞争时，generation、endpoint 和 artifact guard 防止旧请求更新或删除新 generation。

旧版 `session.list` 还会顺带清理 idle session，这与“list 必须零 flock 且无副作用”不可同时成立。新架构保留 `expired_removed_count` 公开字段，但固定为 0；超时 session 仍可观察，清理必须显式调用 `session.close`、`session.kill` 或 `session.gc`。公开 schema 没有改变。

## 五、旧 v2 注册表迁移

升级规则严格 fail-closed：

- `registry.json` 不存在：直接使用 per-session 布局。
- 合法空 v2：原子归档为 `registry.json.v2.retired` 并同步父目录。
- 合法非空 v2：返回既有 `SESSION_REGISTRY_FAILED`，消息包含 `REGISTRY_MIGRATION_REQUIRED`；不终止进程、不迁移、不清理 sidecar。
- 空文件、非法 JSON、尾随数据或 schema 错误：拒绝并保留原文件。
- `registry.json` 与 `.v2.retired` 同时存在：拒绝覆盖既有证据。

非空旧表的操作顺序是：先用旧二进制关闭或 gc 全部 session，确认得到合法空 v2，再启动新二进制完成归档。不存在 dual-read、在线迁移、自动 kill 或 transport fallback。

## 六、对 Wellen、Verilator 和 FST 数据流的影响

本修复没有修改 Wellen 或 Verilator 仓库，也没有改变两者 ABI、依赖 revision 或构建产物。

xdebug-fst 对 Wellen 的需求仍然只有：直接打开当前 session 的原始 `.fst`，按需提供层级、时间、四态值、变化、alias、delta 及 typed value 事实。Wellen 不管理 session registry，不获取 lifecycle lease，也不承担 driver、协议或 X-origin 分析。不存在 FST 转 VCD/JSON、私有索引、离线数据库或全量快照；FST 是唯一且必须适配的波形输入。

Verilator DesignDB 仍只提供 FST 不包含的静态设计事实，例如 driver/load、端口关系、控制 predicate 和源码位置。本修复没有触碰 Verilator 的 AST、调度、生成模型或 DesignDB emitter。managed engine 仍把 Wellen 动态波形事实与 DesignDB 静态事实交给 xdebug action 组合分析；registry 只决定该 engine generation 是否属于当前 session。

因此本次变化发生在“frontend/engine 生命周期控制面”，不在“Wellen/Verilator/action 数据面”。73 个公开 Action、成功响应 schema、XOUT、UDS framing、FST-only 约束和 `session.kill` 均保持原合同。

## 七、验证结果

- 静态扫描：除 `session_lifecycle_lease.h` 外，产品代码没有 `flock`。
- 动态旧锁门禁：外部持有废弃 `registry.lock` 时，list、doctor、managed query 均不等待。
- 动态 lease 门禁：同 session close 等待；不同 session open 和只读 Action 不等待。
- `strace -f -e flock`：list 0、doctor 0、managed query 0、close 2（加锁与解锁）。
- 注册表：activity 单调、history 完整、损坏隔离、generation mismatch、v2 空/非空/非法/归档冲突均有回归。
- GCC 13 普通、ASan、UBSan CTest 均为 9/9；三种构建各自运行的 pytest 均为 422/422。
- MCP direct、fake-LSF、并发、FD/RSS 和 UDS lifecycle 门禁通过。

完整执行记录和 commit 对应关系见 [`XDEBUG_FLOCK_HOT_PATH_REPAIR_TASKBOOK.md`](XDEBUG_FLOCK_HOT_PATH_REPAIR_TASKBOOK.md)。
