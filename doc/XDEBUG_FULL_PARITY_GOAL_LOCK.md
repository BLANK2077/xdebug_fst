# xdebug-fst 完整兼容 Goal 不可漂移约束

## 一、与当前 active Goal 的关系

本文是当前 active Goal `019fe602-0198-7f23-a9a1-bb3c6a539dec` 的权威执行附件，约束编号为 **`GOAL-FST-DIRECT-001`**。Goal objective 的第一句要求实现 [`XDEBUG_FULL_PARITY_TASKBOOK.md`](XDEBUG_FULL_PARITY_TASKBOOK.md) 中定义的全部计划；任务书又明确引用本文，因此本文中的要求属于当前 Goal 的组成部分，而不是 Goal 之外的建议。

Goal 系统不允许在 active 状态原地改写 objective。旧 objective 中仍可见的 TCP/file transport 表述已被用户后续指令废止，不得据此恢复 TCP/file；不得为了改写文字而把尚未完成的 Goal 错误标记为 `complete` 或 `blocked`，也不得重建 Goal 以丢失既有执行状态。

## 二、唯一允许的波形数据流

当前 Goal 只需要并且也必须完整适配 **FST 波形**。唯一允许的数据流是：

```text
当前 session 的原始 .fst
          │
          ▼
Wellen 直接、按需读取层级/时间/delta/类型/值变化
          │
          ▼
xdebug-fst action 在请求期间查询、采样和推理
          │
          ├── 可选结合 Verilator DesignDB 的静态 HDL 事实
          └── 可选写出用户显式请求的最终 export 产物
```

这里的“适配 FST”不是另建一套离线“FST 分析系统”，也不是把 FST 转换后分析。以下路径全部禁止：

- FST → VCD → action；
- FST → JSON 波形或全量内存波形快照 → action；
- FST → 私有索引或离线数据库 → action；
- FST → export 文件 → 重新加载 → action；
- Wellen/FST 失败 → 其他波形格式、backend、fixture 或 transport fallback。

VCD 只允许作为可读的 fixture 源描述来生成 FST；测试、差分和最终验收实际打开的波形必须是 `.fst`。如果 FST 缺失必要事实，必须修复 FST 生成链或 Wellen 的 FST 读取能力，并先保存失败证据，不得绕过。

Verilator DesignDB 只提供 FST 不包含的源位置、driver/load、端口连接、控制依赖等静态 HDL 事实。它不得读取、保存、重建或替代运行时波形。对 Verilator 的任何修改仍须先有失败差分和现有 XDD ABI 不足的证据，并保持最小、附加、局部、向后兼容。

## 三、范围锁定

- 波形输入：只支持 FST，并且必须把冻结的 xdebug action 能力完整适配到 FST。
- transport：保留 UDS、stdio、MCP direct 与 fake-LSF 计划；TCP/file 不实现、不验收，显式选择时 fail closed，绝不 fallback。
- export：只允许用户显式请求的最终产物；不得作为 transport、中间数据库或后续分析输入。
- Wellen：直接打开当前 session 的原始 `.fst`，按请求所需信号和时间范围访问；不得建立可跨请求重载的波形副本。
- Verilator：只补充静态设计事实，修改尽可能克制；没有修改前失败证据就不得扩展。

## 四、每批次强制门禁

每个功能批次开始、提交和验收时必须逐项确认：

1. 实际数据流仍是“原始 `.fst` → Wellen 按需读取 → action”。
2. 测试日志列出的所有实际波形输入均以 `.fst` 结尾。
3. 没有新增 FST 转换、离线数据库、私有索引或全量波形快照。
4. 没有重新加载显式 export 产物。
5. 没有 backend、fixture、格式或 transport fallback。
6. Verilator 修改若存在，具备修改前失败证据、必要性证明和独立回归。
7. TCP/file 仍为明确拒绝的裁剪项。

任一项不满足时，该批次不得提交，最终 Goal 也不得标记为 `complete`；即使 73 个 action 或现有测试已经通过也不例外。

## 五、Goal 完成判定

`GOAL-FST-DIRECT-001` 与任务书 P0–P7 的其余条件必须同时满足。只有全部冻结 action、schema、错误合同、差分、FST-only 门禁、Wellen/Verilator 回归和仓库清洁性均有真实证据，且不存在上述禁止路径时，才允许完成当前 Goal。
