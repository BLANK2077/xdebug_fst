# binary-v1 DesignDB 正式落地报告

## 结论

Verilator 单次生成 simulator/FST 能力与 binary-v1 DesignDB、xdebug-fst mmap、session 查询
索引和 Wellen definitive miss 已正式落为新设计默认生产路径。用户不需要第二次 Verilator、
DesignDB C++/SO 编译、converter 或手写 manifest。

legacy `xdd-so` reader 继续存在，只承担已有 bundle 和 fixture 的显式兼容，不是 binary 打开
失败后的 fallback。冻结的 xdebug.v1 公共 response schema 没有变化。

## 最终数据流

```text
RTL ── patched Verilator（一次 invocation）
 ├─ simulator ──运行──► 原始 FST ──► Wellen 按需读取
 └─ binary-v1 producer
      ├─ <prefix>__DesignDb.xddb.tmp ──rename──► .xddb
      └─ xdebug-design-db.json.tmp ──最后 rename──► bundle 提交点

FST + bundle ──► session.open
                  ├─ 严格解析 v2 manifest
                  ├─ 只读 mmap .xddb
                  ├─ 建立共享 DesignQueryIndex
                  └─ 持久化 backend 格式证据并执行 action
```

Wellen 第一次名称查询建立完整名称索引。索引完成后，存在结果和不存在结果都由该索引确定；连续
miss 不再触发 hierarchy 全量重扫。设计查询在 binary 和 legacy reader 之上共用同一份
`DesignQueryIndex`，没有复制 Action 语义。

## 生产合同

- `--design-db-binary` 在指定 `--Mdir` 生成 `<prefix>__DesignDb.xddb` 和严格 bundle v2
  manifest，不生成 `__DesignDb.cpp` 或 `.so`。
- producer 先完成临时数据库，再撤销旧 manifest、替换数据库，最后 rename 新 manifest；任何
  中间失败都不会留下可消费的半成品 bundle。
- session manager 不扫描目录猜测 artifact，不按扩展名选择 reader，不从 binary 回退到 `.so`。
- design session 的 registry state 必须持久化 `binary-v1` 或 `xdd-so`；旧 design record 缺少
  格式证据时明确要求重开 session。
- engine debug log 记录实际 backend 格式和查询索引构建指标；公共协议不暴露本地实现细节。
- `tools/benchmark_large_rtl_trace.py` 默认使用 binary-v1，并验证 producer manifest；
  `xdd-so` 只作为显式性能对比参数。

## 自动门禁

新增或强化的门禁覆盖：

- 自定义 prefix/Mdir、重复构建确定性、legacy/binary 产物互斥和 manifest 提交失败关闭；
- 同一 RTL 的 direct binary producer 与 legacy emitter 在 signal、driver、load、port、关系顺序
  和 resolve 结果上的完整 parity；
- 临时 RTL 单次 `--binary --timing --trace-fst --design-db-binary` 冷构建、运行仿真、打开
  session，并执行 `signal.resolve`、`value.at`、`trace.driver`、`trace.active_driver`、
  `trace.active_driver_chain`、`signal.changes`；
- `/proc/<pid>/maps` 证明 session 实际 mmap `.xddb`，registry 与 debug log 均证明
  `binary-v1`；
- Wellen 连续 definitive miss 的 hierarchy 扫描计数保持为一次；
- legacy `.so` reader、converter、失败关闭和 binary/SO Action parity 继续回归。

## 最终验收结果

| 验收项 | 结果 |
| --- | --- |
| 统一构建 | 通过；锁定官方依赖，私有 GCC/G++ 13.3.1 |
| CTest | 7/7 通过 |
| 完整 pytest | 460/460 通过 |
| Verilator `t_xdd*` | 15/15 通过 |
| direct producer/backend parity | 通过 |
| Verilator patch SHA 与 lock | 一致 |
| 本机路径门禁 | 通过 |
| tracked fixture cache | 无 diff，未重建 |
| `.codex` | 未被 Git 索引 |
| 官方 Verilator/Wellen HOME | 工作树无 diff |

64K 性能矩阵没有重复执行，因为正式实现沿用已完成矩阵验证的 binary-v1 布局和索引拓扑；
Wellen miss 状态守护由轻量扫描计数门禁覆盖。既有冻结结果继续由
[`TRACE_IMPLEMENTATION_EXPLORATION_REPORT.md`](TRACE_IMPLEMENTATION_EXPLORATION_REPORT.md)
承载。本次增加了轻量算法门禁，并把大规模基准默认入口切到 producer 直接 binary 路径。

## 分阶段提交

- `bf65fcf`：建立正式落地任务书并冻结验收边界。
- `89adf9f`：让 patched Verilator 原子发布 binary-v1 bundle。
- `445baf6`：持久化 backend 格式证据并保持公共 schema 不变。
- `9f71e68`：增加单次构建、mmap、六类 Action 与 definitive miss 端到端门禁。
- `41bcbd2`：把标准大规模 RTL 工具切换到 binary-v1，保留 legacy fixture 边界。
- `b5340eb`：增加 direct producer 与 legacy emitter 全字段 parity 门禁。
