# xdebug-fst 完整兼容 Goal 不可漂移约束

## 一、与当前 active Goal 的关系

本文是当前 active Goal `019fe602-0198-7f23-a9a1-bb3c6a539dec` 的权威执行附件，约束编号为 **`GOAL-FST-DIRECT-001`**。Goal objective 的第一句要求实现 [`XDEBUG_FULL_PARITY_TASKBOOK.md`](XDEBUG_FULL_PARITY_TASKBOOK.md) 中定义的全部计划；任务书又明确引用本文，因此本文中的要求属于当前 Goal 的组成部分，而不是 Goal 之外的建议。

Goal 系统不允许在 active 状态原地改写 objective。旧 objective 中仍可见的 TCP/file transport 表述已被用户后续指令废止，不得据此恢复 TCP/file；不得为了改写文字而把尚未完成的 Goal 错误标记为 `complete` 或 `blocked`，也不得重建 Goal 以丢失既有执行状态。

### 2026-08-10 用户再次确认的解释锁

用户再次明确：**本项目只需要并且也必须适配 FST 波形，不得退化为“用 FST 做分析”。** 这句话在当前 Goal 中按以下唯一含义执行：

1. “适配 FST”是波形输入适配要求，FST 只提供运行时波形事实；
2. “分析”仍是冻结的 xdebug action 合同、算法和错误语义的职责，必要时结合 Verilator DesignDB 静态事实；
3. Wellen 只直接、按需、保真读取当前 session 的原始 `.fst`，不实现或替代 xdebug 调试语义；
4. 不得把 xdebug-fst 描述、设计或验收成“FST 分析引擎”，不得从“FST-only”推导出简化 action、只看信号和值变化、预扫全文件或建立中间分析库；
5. 后续交接摘要、上下文压缩、阶段切换、测试补洞和 Verilator 修改都必须显式继承 `GOAL-FST-DIRECT-001`，不得因旧 Goal objective、旧术语或历史实现重新解释本边界。

若任何实现方向同时满足不了“FST 是唯一且必须支持的波形输入”和“FST 不是分析引擎”这两项要求，该方向即违反当前 Goal，必须停止，不能以测试通过为由接受。

为防止后续上下文压缩只保留其中半句，`GOAL-FST-DIRECT-001` 必须机械地按一组不可拆分的双断言检查：

- **输入断言**：唯一且必须完整适配的运行时波形输入是当前 session 的原始 `.fst`；
- **职责断言**：FST 及 Wellen 只提供运行时波形事实，调试分析只能由冻结的 xdebug action 语义结合必要的 Verilator DesignDB 静态事实完成。

任何计划、代码、测试、提交说明、交接或验收只满足输入断言而没有满足职责断言，或只满足职责断言却引入非 FST 波形输入，均视为同等严重的 Goal 漂移。不得用“FST-only”“FST backend”“FST 分析”等缩写替代这组双断言。

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

本 Goal 对职责边界作如下不可漂移定义：**FST 是唯一波形输入容器，不是分析引擎；Wellen 是 FST 的保真按需访问层，不是 xdebug 语义的替代实现；xdebug-fst action 与必要的 Verilator DesignDB 静态事实组合才承担调试分析。** 因此适配工作只把原版 action 所需的运行时波形事实接到 FST，不得把 driver/load、active-driver、chain、X-origin、协议、表达式、窗口、统计、完整性或错误语义降级成仅凭 FST 信号和值变化的简化分析。文档或代码中出现“FST 分析”时，只能指 action 使用原始 FST 波形事实，绝不表示由 FST 格式本身承担分析。

VCD 只允许作为可读的 fixture 源描述来生成 FST；测试、差分和最终验收实际打开的波形必须是 `.fst`。如果 FST 缺失必要事实，必须修复 FST 生成链或 Wellen 的 FST 读取能力，并先保存失败证据，不得绕过。

Verilator DesignDB 只提供 FST 不包含的源位置、driver/load、端口连接、控制依赖等静态 HDL 事实。它不得读取、保存、重建或替代运行时波形。对 Verilator 的任何修改仍须先有失败差分和现有 XDD ABI 不足的证据，并保持最小、附加、局部、向后兼容。

active-driver 的 activation predicate 也属于上述静态 HDL 事实，而不是波形分析结果。运行时分支是否成立必须由 xdebug-fst 在目标 active time 通过 Wellen 直接按需读取当前原始 `.fst` 的控制叶子并做四态求值；predicate 缺失、不可解析、引用缺失或结果为 X/Z 时必须 unresolved/fail closed。禁止把 predicate 预先扫成离线索引，禁止缓存整份 FST，禁止读取导出文件，也禁止退回“第一条静态 driver”。

同值 NBA 的赋值事件时间同样受 `GOAL-FST-DIRECT-001` 约束：FST 只记录值变化，不能也不得被伪装成赋值事件数据库。Verilator DesignDB 只发布赋值语句所在敏感列表的直接信号与边沿这一静态事实；xdebug action 再要求 Wellen 对当前原始 `.fst` 中这些已确定的时钟/复位信号按需查找真实边沿。不得假定固定周期、任选全局时钟、扫描等值信号、让测试数据每拍变化，或生成事件索引来补洞。这个组合仍是“DesignDB 静态语义 + 原始 FST 运行时事实 → xdebug action 分析”，不是“用 FST 做分析”。

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
8. FST 仍只承担输入格式职责；没有 action 将原版调试语义降级或偷换为“FST 自身分析”。
9. 本批次的计划、提交说明和交接材料均继续引用 `GOAL-FST-DIRECT-001`，没有把“FST-only”缩写成会引起“FST 自身分析”误解的架构描述。

任一项不满足时，该批次不得提交，最终 Goal 也不得标记为 `complete`；即使 73 个 action 或现有测试已经通过也不例外。

## 五、Goal 完成判定

`GOAL-FST-DIRECT-001` 与任务书 P0–P7 的其余条件必须同时满足。只有全部冻结 action、schema、错误合同、差分、FST-only 门禁、Wellen/Verilator 回归和仓库清洁性均有真实证据，且不存在上述禁止路径时，才允许完成当前 Goal。

## 六、P6 第三十六批防漂移审计记录

仅 `default` 的 `case matches` 闭环再次验证本 Goal 的不可拆分双断言：Verilator DesignDB 提供唯一分支的恒真静态谓词，冻结的 xdebug action 执行 active-driver 合同，Wellen 只在 45ps/65ps 对当前 session 的原始 `.fst` 按需读取运行时事实。该批没有把 `matches` 语法、分支识别或 driver 判定下沉到 FST/Wellen，也没有引入 VCD/JSON 转换、私有索引、离线库、全量快照、TCP/fileport 或 fallback。

对应失败证据和实现为 Verilator `1ae90d55d`、`487482500`、`da63eb075` 及 xdebug-fst `813ee36`、`f8d0aee`。XDD header/ABI 未改变，Wellen 未修改；tagged、binding 与嵌套 wildcard 继续失败关闭。因此该批只能声明“仅 `default` 子集已闭环”，不能声明通用 matches 或 Goal 已完成。
