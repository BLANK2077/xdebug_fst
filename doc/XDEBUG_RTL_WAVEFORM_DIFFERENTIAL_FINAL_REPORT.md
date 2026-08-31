# xdebug 原版 RTL 与波形差分最终报告

## 结论

Goal `01a050fa-b864-7ce2-af88-56083d84ea21` 的范围内差分已经关闭。最终验收比较的是测试内容、
刺激、观察时间和 73 项公开 Action 的可观察语义，不要求原版 FSDB、当前 FST、RTL 镜像或生成产物
跨侧哈希相同。哈希只用于锁定证据身份和证明当前 fixture 可重复生成。

最终矩阵包含 88 个场景：81 个 `semantic-equivalent`，7 个 `proven-unobservable`；`partial`、
`missing`、`unclassified`、`unqueued_gap` 均为 0，P3 queue 为空。原版 23 个 fixture、103 个 HDL、
25 个声明波形输出和 260 个测试 consumer 均可反向定位到具体场景。

## 对比口径

- 原版行为权威固定为 runtime `8eecf71271cc523d93bf03f6b9f9b6fa04ed3ee8`、schema
  `c45099040abf3dbe194d3ba27c207d7637b39ba9f9d662fad3d9d50dda99fb2c` 和严格 73 Action。
- 原版 Goal-start 资产与行为 revision 分开冻结；外部工作树后续提交或用户修改不能替换基线。
- 双侧比较单位为 RTL construct、刺激/seed、观察时间或窗口、公开请求、完整响应、完整性和截断字段。
- FSDB 与 FST 二进制、源文件布局、生成器实现及文件哈希不是跨侧等价条件。
- 当前实现只以 Wellen 读取原始 FST，并用开源 Verilator/DesignDB 提供静态设计事实；NPI 只曾用于
  只读采集冻结原版 oracle，不进入当前实现、构建、运行依赖或持续门禁。

## 资产复用与补测结果

- 能直接脱离专有依赖使用的 RTL、配置和刺激优先逐字节复制，包括活动驱动类 RTL、P0 case 以及
  XIF 的六份 `event_*.json`。
- 原版 UVM、SVT/XAMBA VIP、XIF agent、VCS/FSDB producer 不能作为开源执行输入时，当前仓库使用
  pin-level RTL/harness 或冻结公开握手流重放同一测试内容；没有复制专有库，也没有把 FSDB 转换成
  FST。
- 基础类型、四态/delta、design/combined、68 个 active trace case、stream、APB、AXI 六波形和
  XIF event 都已有双侧公开语义证据。协议大场景保留完整事务、beat、seed、phase order、XOUT 和
  export artifact，而非只比较计数或 preview。
- 7 个 `proven-unobservable` 只覆盖冻结公开 schema 明确无法表达的有限信息，包括 SVA/NPI 私有
  assertion/probe 面和少量历史资产合同差异；每项均有未来 schema 漂移时失败关闭的静态门禁。

## 最终机器证据

| 证据 | 结果 |
| --- | --- |
| 资产清单 | original RTL 103；current RTL 85；current FST 85；current VCD 1；live check 通过 |
| 语义矩阵 | 88 场景；81 等价；7 有界不可观察；所有未关闭计数为 0 |
| pytest | 790/790，通过；未使用 skip/xfail |
| CTest 普通构建 | 7/7，通过；包含 schema、UDS、session lifecycle/stability、Wellen FST、DesignDB |
| ASan | 独立构建，`detect_leaks=1:halt_on_error=1`，串行 CTest 7/7 |
| UBSan | 独立构建，`halt_on_error=1:print_stacktrace=1`，串行 CTest 7/7 |
| 73 Action 审计 | 2069 条新轨迹；73/73 complete；`unknown_actions=[]` |
| 负向 Action | `clock_point_query`、`no.such.action` 均明确返回 `UNKNOWN_ACTION`，单列为预期拒绝 |
| manifest/matrix/closure | write/check 双稳定，`git diff --check` 通过 |
| 专有产物 | 本 Goal 没有新增 FSDB、daidir、simv、VPD、WLF、UCDB、专有头库或日志 |

最终文件摘要：

- `compat/xdebug-v1/rtl-wave-assets.manifest.json`：
  `4c797805aa763d3135ed696f70c5efaa260c191e41d23c2bf06097c653643ce4`
- `tests/coverage/rtl_wave_semantic_matrix.json`：
  `f25bef32293b2b7fec1a018c9e5112d93835a7e927f39211f2c21d6b264e4652`
- `tests/data/rtl_wave_differential/p3e-closure.audit.json`：
  `ae77be5ba790061f14e9819b67af7187a51520109c58bc58917add638b90b5b3`

## P4 中发现并加固的门禁

- P3-B 集成测试原先仍要求最终矩阵保留 `partial/missing` key；现改为精确锁定 81/7 和空 P3 queue。
- Phase5 集成测试原先仍指向早期 partial 子集审计；现锁定完整 P3-C public oracle，并继续校验早期
  partial 审计作为嵌套历史证据存在。
- P3-D 的 `batch` 已实测承载子请求 limit/truncation 结果，旧 applicability 的两个 N/A 与运行证据
  冲突；删除 N/A 后，`batch` 必须持续提供这两维真实证据，73/73 门禁不再允许 overlap。
- action 审计把明确返回 `UNKNOWN_ACTION` 的负向请求与未知实现 action 分开；`--require-complete`
  现在同时要求 73/73 完整且 `unknown_actions` 为空。

全量回归曾在 sanitizer 构建后的高负载窗口中，让 AXI stress 的一个 OSD 请求命中内部 UDS 30 秒
读超时；同 fixture 五 profile 定向复现 5/5 通过，系统空闲后的全新全量回归 790/790 通过，因此
没有形成稳定产品红灯，也没有修改 timeout、fixture 或 oracle。

## 写入边界与外部状态

全部运行时 HOME、TMP、cache、UDS、Cargo cache、日志和输出均位于当前仓库。ASan/UBSan 使用
仓库内独立构建目录；离线 Cargo 依赖从既有用户缓存只读复制到仓库 `.tmp`，UBSan 直接复用仓库内
已锁定的 Verilator 工具副本，没有联网或写用户缓存。

最终外部只读快照为：xverif HEAD `5110099482b9433aa7db475c1498d7dc9d6819de`，Wellen HEAD
`afab0abd1fe4c06db9744f0b7b20b18d23b7f8df`，Verilator HEAD
`bf01d667c8b27f2f7cee456bb35a84e5372434df`。Wellen、Verilator porcelain 为空；xverif 的既有
dirty/untracked 状态由冻结器逐资产核对并只读记录。整个 Goal 只修改当前 `xdebug_fst` 目录，未执行
fallback、PR 或远端推送。
