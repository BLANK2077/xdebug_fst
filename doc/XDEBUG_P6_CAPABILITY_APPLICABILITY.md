# P6 能力与信息语义适用性审计

## 一、审计口径

2026-08-13 用户将验收口径修正为“能力一致、信息语义一致”，不要求输出或实现细节完全一致。
P6 因此按用户能否完成同一种调试工作、能否得到等价关键事实来划分能力族，不再把所有
SystemVerilog 语法构造与所有限制参数笛卡尔积分别当成新的 xdebug 能力。

这不缩减 73 个公开 action，不放宽 FST-only、完整性、截断、未知状态或错误原因；只把
“xdebug action 的分析能力”与“Verilator 前端支持多少 SystemVerilog 语法”分开。

机器可读门禁为
[`p6_capability_applicability.json`](../tests/coverage/p6_capability_applicability.json)，对应测试会
验证每个分析语义族都有仓库内真实回归，所有非独立能力项都有显式理由。

## 二、已覆盖的分析语义族

| 能力族 | 关键信息语义 | 代表证据 |
| --- | --- | --- |
| activation predicate/pattern | 活动分支、default、通配、binding 与源行 | case/casez/casex、inside、packed wildcard、PatternVar |
| sequential/precedence | NBA 活动时间、self hold、blocking/NBA、force 优先级 | mixed、double NBA、force、gated NBA |
| module/interface/ref/alias | 跨 output/inout/modport/ref 的方向和层级路径 | 双 inout、条件 output、interface 六跳、ref 五跳 |
| multi-driver ambiguity | 不任意选择候选，保留 statement/RHS 证据 | 双连续、双过程、跨实例 output、父子混合 |
| X-origin branch/loop/source | RHS/control/port 分支、loop 与真实来源并存 | loop+normal、ref driver loop+origin、X predicate |
| limits/time | 不同 onset、node/depth/chain frontier、pending 与续跑 | max_time_steps、depth+chain、node+chain |
| typed/delta waveform facts | 四态宽度、real/string/event、同物理时间 delta | Wellen typed FST、signal.changes delta |

这些证据共同覆盖 active-driver、chain 和 X-origin 的分析原语。新增同类 RTL 语法只有在引入
新的用户可观察语义（新的调度优先级、关系类型、未知状态或限制行为）时才形成新缺口；仅换一种
前端写法但产生相同 DesignDB/FST 事实，不再单列为完成门禁。

## 三、不属于独立 xdebug 能力的上游变体

- tagged union/expression/pattern 的语法编译属于 Verilator SystemVerilog 前端覆盖。xdebug 所需
  的 predicate、pattern binding、分支、driver 与来源语义已用等价静态事实独立覆盖。
- nested/arrayed interface 的具体语法形状属于 DesignDB topology producer 覆盖。xdebug 消费的
  是通用 port 与 `interface_modport_member` 关系；其跨层、alias、loop 与预算语义已有回归。
- primitive strength/tristate 的语法与 lowering 组合属于 Verilator 静态事实生成范围。xdebug
  的 primitive RHS、inout 遍历和多 driver 歧义能力已经独立覆盖。
- 已覆盖限制参数的所有笛卡尔积不是独立能力。每个限制单项及 branch+depth、branch+node、
  branch+chain 的代表性交互已验证；若未来发现计数或 frontier 结论错误，再以真实红测新增。

上述条目不是声称 Verilator 已支持这些所有语法，而是明确它们不再作为 xdebug 73-action
能力一致性的完成条件。不得反过来用这项裁定跳过新的 action、关系类型或信息语义缺口。

## 四、当前证据

- 新鲜运行 trace：73/73 action 均有 success 与 invalid request，十维审计无 missing。
- 冻结适用性：boundary 30+43、completeness 41+32、limits 29+44、multiple 53+19+1、
  truncation 35+2+36、X/Z 23+50，均完整分区 73。
- P6 combined：81/81。
- GCC 13 全量 pytest 与 CTest 9/9 通过。
- 原始波形仍全部由 Wellen 直接按需打开 `.fst`；没有转换、离线库、索引或 fallback。

资源缺失适用性的历史冻结原版差分证据继续有效；当前只读原版安装已发生二进制/schema 漂移，
不能用其当前输出悄悄替换冻结基线。
