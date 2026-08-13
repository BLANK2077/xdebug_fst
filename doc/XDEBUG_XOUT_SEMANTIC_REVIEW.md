# xdebug 与 xdebug_fst 全 Action XOUT 语义评审报告

## 一、结论

本轮以“能力一致、信息语义一致”为验收口径，不要求逐字符一致。`xdebug_fst` 冻结 catalog 的 73 个 Action 均已取得同一次业务响应对应的 canonical JSON 与 XOUT，73/73 通过语义完整性审计；414 项 pytest 全部通过。候选实现没有退化为用 FST 做离线分析：FST 仍是唯一且必须适配的波形输入，分析逻辑仍由各 Action 执行，Wellen 只负责原始 FST 读取，未引入 FSDB/VCD 转换、TCP 或 fileport。

原版当前 catalog 同样有 73 项，但与候选冻结基线存在一进一出的版本漂移：原版独有 `apb.export`，候选独有 `session.kill`。本轮遵守冻结 public schema/action 能力约束，没有擅自增删 Action；表格按候选冻结 73 项记录，原版 `apb.export` 另列问题。

候选端所有 P0/P1 XOUT 问题均已关闭。仍保留的 P2 仅限配置、采样和诊断对象的层级 section；它们不丢失事实、不静默截断，并保留 JSON 字段路径便于机器结果与文本互相定位，因此明确接受。

## 二、证据与方法

- 原版 revision：`981388c92d5b48351f9e893371ae76dab6886d4f`。
- 原版正式 suite：`XVERIF_TEST_EXECUTION_ENV=host .conda-xverif/bin/python -m pytest --xverif-gate nightly --xverif-suite xdebug.native_xout_all -q`。
- 原版结果：1/1 suite 通过，73 个 primary、179 次捕获；复用 xverif 现成 fixture 缓存，没有 prepare、重建或 fallback。
- 原版真实输出报告 SHA-256：`5d89c7609597959658efb55206ad6771ece782279110a2a2993a7fd984aa0810`。suite 会改写报告中的耗时、临时缓存路径和 build id，本轮已恢复，xverif 工作树保持干净。
- 候选编译器：`XDEBUG_GCC_TOOLCHAIN` 指向的 GCC 13 工具链；构建目录 `build/gcc13`。
- 候选全量结果：414/414 pytest 通过；同响应 XOUT 捕获 1071 条，Action coverage 1223 条。
- 候选 XOUT 捕获 SHA-256：`7ed4b817746c2ac83fc45b6ebc887fbd65998b5ab568147367844143ec522213`。
- 候选 Action coverage SHA-256：`68a969d053673820dde0c1eee4ea6342bd78eb15a044ea0646617104479862fe`。
- 候选审计 JSON SHA-256：`61c3e808b879d73368acdfdda595f7a6702e1015aa48e68ef303fef308aee1e7`；73/73 通过。
- 跨后端不比较不同 fixture 的具体数值，而是分别证明各自 XOUT 对各自 JSON 的关键事实投影完整，再比较信息角色、结论类型和布局能力。

## 三、本轮对 xdebug_fst 的修复

1. 恢复 handler 专用 XOUT 调度。canonical response 通过 schema 校验后才渲染；managed UDS 用私有 sidecar 传递 XOUT，公开 JSON 不增加字段。
2. 默认 handler 不再返回 JSON dump；没有专用 renderer 时走统一通用 renderer。对象数组不再固定裁剪 20 行，嵌套集合不会被静默丢弃。
3. `value.at` 使用多信号乘多时间值矩阵，覆盖 2 信号乘 5 时间、X/Z、进制和缺失值语义。
4. trace 使用 source path、chain、hop、origin、ambiguity statement 和 RHS sample 领域表，保留查询时间、active time、X onset、关系、终止和完整性。
5. stream query/export 使用 transfer、stall、packet、首尾字段与多 beat preview 表；单 beat 不重复显示相同 head/tail。
6. `scope.roots` 使用统一 design/wave 对齐表，不再重复打印三套 root 数据。
7. session XOUT 显示最小必要身份 `session_id/mode/transport`，不显示 socket、PID、inode、缓存路径等运行时遥测。
8. export 和 rc 生成的嵌套 summary 会显示实际 artifact path、manifest/meta path 与格式。
9. one-shot、stdio-loop、managed UDS 与真实 xverif MCP direct 走同一 XOUT；MCP direct 与原生 one-shot/UDS 的代表请求已逐字比对。

## 四、原版独立问题清单

- P1 `session.open`：XOUT 只有 `status: opened`，未给 session id、mode、transport；用户必须转查 JSON 才能确认打开的是哪个会话。
- P1 `axi.export`、`event.export`、`list.export`、`stream.export`：成功 XOUT 有 `output_written` 和计数，但不显示最终 artifact path、manifest/meta path；写出目标确认需要转查 JSON。原版独有 `apb.export` 的 primary 是未写文件的 preview，语义完整，SHA 前缀为 `eca15fca8711`。
- P2 `session.close`、`session.gc`：输出完整 FSDB/daidir 缓存路径、socket、server PID、设备号和 inode，终端信息密度低且泄漏运行时细节。
- P2 `batch`：单个 child 的表包含 tool build id、git/schema revision，并同时显示等价 action count 字段，诊断噪声明显。
- P2 `actions`：`action_count` 与 `total_action_count` 在未过滤请求中重复表达同一事实。
- P2 测试基础设施：正式 suite 会修改 tracked Markdown 中的 elapsed、临时路径和 build id；测试通过不应导致 xverif 工作树变脏。

## 五、73 项逐项结论

哈希为各端 primary XOUT SHA-256 的前 12 位；原版结论是本轮重新评审结果，不沿用历史“全部 PASS”的判断。

| Action | 原版结论 | xdebug_fst 结论 | 原版哈希 | FST 哈希 |
| --- | --- | --- | --- | --- |
| `actions` | P2：冗余/泄漏 | 完整且精炼 | `10d4b42d62f2` | `b906abfc8bb3` |
| `apb.config.list` | 完整且精炼 | 完整且精炼 | `866fdce04303` | `7fcbdd23ea62` |
| `apb.config.load` | 完整且精炼 | 完整，接受 P2 布局 | `5da1cf98401c` | `3ec0aece1c15` |
| `apb.query` | 完整且精炼 | 完整且精炼 | `782dd84ec43b` | `ffebd8bd6194` |
| `apb.statistics` | 完整且精炼 | 完整，接受 P2 布局 | `43e086189031` | `0b52ea40aea1` |
| `apb.transaction.cursor` | 完整且精炼 | 完整且精炼 | `b478c3f7915a` | `8690ad57dc67` |
| `apb.transfer_window` | 完整且精炼 | 完整且精炼 | `6ac825d2e2e2` | `86c36cb9c7db` |
| `axi.analysis` | 完整且精炼 | 完整，接受 P2 布局 | `4fc0baf21bc0` | `998b9b33ee53` |
| `axi.channel_stall` | 完整且精炼 | 完整且精炼 | `1f500655f1ab` | `6edaf63a1620` |
| `axi.config.list` | 完整且精炼 | 完整，接受 P2 布局 | `2886a23c78dc` | `b6fea9c2394e` |
| `axi.config.load` | 完整且精炼 | 完整，接受 P2 布局 | `5e503b4e8fe8` | `f6b9efbbf980` |
| `axi.export` | P1：关键遗漏 | 完整且精炼 | `d0651474b53d` | `9f333809b46f` |
| `axi.latency_outlier` | 完整且精炼 | 完整且精炼 | `c5a523737c63` | `e75673d1c2ec` |
| `axi.outstanding_timeline` | 完整且精炼 | 完整且精炼 | `0da3fa40cb1a` | `39db19aab45b` |
| `axi.query` | 完整且精炼 | 完整且精炼 | `d3a63bbbb68e` | `52478fe34619` |
| `axi.request_response_pair` | 完整且精炼 | 完整且精炼 | `1940670fa637` | `b746f294c1ab` |
| `axi.statistics` | 完整且精炼 | 完整，接受 P2 布局 | `74f6c70d189c` | `3bb5a91653f1` |
| `axi.transaction.cursor` | 完整且精炼 | 完整，接受 P2 布局 | `fc17728ee28f` | `e8c2d96c6bb9` |
| `batch` | P2：冗余/泄漏 | 完整，接受 P2 布局 | `e65e2afa6b51` | `aca8fb700cbd` |
| `counter.statistics` | 完整且精炼 | 完整，接受 P2 布局 | `1f0061f9e520` | `7586db26349f` |
| `event.config.list` | 完整且精炼 | 完整，接受 P2 布局 | `ae8331ce28ae` | `ddd7103639e1` |
| `event.config.load` | 完整且精炼 | 完整，接受 P2 布局 | `5056e09b47e5` | `2b10f264ba27` |
| `event.export` | P1：关键遗漏 | 完整且精炼 | `4d832feeed88` | `5e8c858b05ce` |
| `event.find` | 完整且精炼 | 完整且精炼 | `94e9b7c8841e` | `58fc6efcb619` |
| `expr.eval_at` | 完整且精炼 | 完整且精炼 | `64d6731b0eca` | `c0249c0349c4` |
| `expr.normalize` | 完整且精炼 | 完整，接受 P2 布局 | `5fde614b0121` | `430ca6aa3089` |
| `list.add` | 完整且精炼 | 完整且精炼 | `d9e393bd77b6` | `dd7206f3ce4f` |
| `list.create` | 完整且精炼 | 完整且精炼 | `a1dfb16f43be` | `17e16f634c14` |
| `list.delete` | 完整且精炼 | 完整且精炼 | `2dac639a8acb` | `e4106176853b` |
| `list.export` | P1：关键遗漏 | 完整且精炼 | `178cd42f3c9d` | `5ce15fd30dbb` |
| `list.first_change` | 完整且精炼 | 完整且精炼 | `8f89714cf53c` | `d06ac4a99049` |
| `list.load` | 完整且精炼 | 完整，接受 P2 布局 | `d2c5332bee26` | `f6f938f4ba5c` |
| `list.show` | 完整且精炼 | 完整且精炼 | `ae6da1e74e20` | `cf2071956f86` |
| `list.validate` | 完整且精炼 | 完整且精炼 | `62f6a3a07c21` | `d159968dcd8f` |
| `nwave.rc.generate` | 完整且精炼 | 完整且精炼 | `941555e6c92f` | `1d45bb7d7b48` |
| `protocol.handshake.inspect` | 完整且精炼 | 完整，接受 P2 布局 | `80e7303148e4` | `24a74920d37c` |
| `schema` | 完整且精炼 | 完整且精炼 | `f04e0a5b1378` | `d470c9ef5c72` |
| `scope.list` | 完整且精炼 | 完整且精炼 | `4598188256c1` | `3ab6452386b7` |
| `scope.roots` | 完整且精炼 | 完整且精炼 | `55a2b79887a5` | `36c3747256a6` |
| `session.close` | P2：冗余/泄漏 | 完整且精炼 | `36a5c6afe327` | `0643a88c177f` |
| `session.doctor` | 完整且精炼 | 完整且精炼 | `2124fd9d0446` | `c1fe666936c4` |
| `session.gc` | P2：冗余/泄漏 | 完整且精炼 | `1783b9e06cf7` | `743343874463` |
| `session.kill` | 不适用：版本漂移 | 完整且精炼 | `-` | `d68f5cb56701` |
| `session.list` | 完整且精炼 | 完整且精炼 | `5570a98a9171` | `c126d31b3122` |
| `session.open` | P1：关键遗漏 | 完整且精炼 | `801dae73579a` | `db63f81cc4f7` |
| `signal.anomaly.inspect` | 完整且精炼 | 完整且精炼 | `deed83ad3ad7` | `0218c527ee1f` |
| `signal.canonicalize` | 完整且精炼 | 完整且精炼 | `b874df7a237f` | `bbcaaae66436` |
| `signal.changes` | 完整且精炼 | 完整且精炼 | `82b1ebbaaae1` | `74ad6a96b358` |
| `signal.resolve` | 完整且精炼 | 完整且精炼 | `5ec7613753f1` | `d1fcbbff2e6b` |
| `signal.sampled_pulse.inspect` | 完整且精炼 | 完整，接受 P2 布局 | `046df8eb1763` | `ae61727eb0dd` |
| `signal.stability` | 完整且精炼 | 完整且精炼 | `6cf87dd7d614` | `1f893d3effde` |
| `signal.statistics` | 完整且精炼 | 完整且精炼 | `9440f5d725df` | `6a1bad81616e` |
| `signal.xz_verify` | 完整且精炼 | 完整且精炼 | `dd19a24d4aa3` | `db43fc6b6c13` |
| `stream.config.get` | 完整且精炼 | 完整，接受 P2 布局 | `699952b1d0fe` | `5fd00ab3087d` |
| `stream.config.list` | 完整且精炼 | 完整且精炼 | `1a10b2480b41` | `07e7f20457b7` |
| `stream.config.load` | 完整且精炼 | 完整，接受 P2 布局 | `f027eb64dd4a` | `19b10421152a` |
| `stream.describe` | 完整且精炼 | 完整，接受 P2 布局 | `b116a3300d15` | `0289e0493b3b` |
| `stream.export` | P1：关键遗漏 | 完整且精炼 | `4d6181ade4b4` | `44cfb1459c35` |
| `stream.query` | 完整且精炼 | 完整且精炼 | `3b3b4023c3b0` | `31ab0f8fba46` |
| `stream.validate` | 完整且精炼 | 完整，接受 P2 布局 | `75d0680a4f2a` | `38580adc9e96` |
| `trace.active_driver` | 完整且精炼 | 完整且精炼 | `bb3568696518` | `7e004e4d137b` |
| `trace.active_driver_chain` | 完整且精炼 | 完整且精炼 | `04e8d4a857b0` | `bd044badbaec` |
| `trace.driver` | 完整且精炼 | 完整且精炼 | `32e4e1499702` | `c2207bbfcca4` |
| `trace.load` | 完整且精炼 | 完整且精炼 | `c34f9fbad2ac` | `d52e710e37a9` |
| `trace.x_origin` | 完整且精炼 | 完整且精炼 | `310b88bc25fb` | `ed663370ef38` |
| `value.at` | 完整且精炼 | 完整且精炼 | `f73705a07e91` | `3e995db6726f` |
| `verify.conditions` | 完整且精炼 | 完整，接受 P2 布局 | `746abce87927` | `a8dbf61e0a8b` |
| `waveform.cursor.delete` | 完整且精炼 | 完整且精炼 | `59bf99b00ace` | `945991a298c3` |
| `waveform.cursor.get` | 完整且精炼 | 完整且精炼 | `b4b37c93b48c` | `82fcb9510c4e` |
| `waveform.cursor.list` | 完整且精炼 | 完整且精炼 | `58e66d070eff` | `2a5bcde1737d` |
| `waveform.cursor.set` | 完整且精炼 | 完整且精炼 | `8c1072d673f4` | `b57c82fdd6f2` |
| `waveform.cursor.use` | 完整且精炼 | 完整且精炼 | `c725b84a7a39` | `28df1b5f0463` |
| `window.verify` | 完整且精炼 | 完整，接受 P2 布局 | `e4d501dfb767` | `5fce6bf45fc8` |

## 六、剩余接受差异

- `batch` 需要承载异构 child response，保留索引化嵌套 section；它比领域表紧凑性差，但不会静默丢失 child 事实，明确接受为 P2。
- 配置 load/get/describe、sampling requested/effective、filter 和 validation 保留字段路径式 section。该布局能准确对应 JSON 层级，且对象规模受 schema 和 Action limit 约束，明确接受为 P2。
- 原版与候选 catalog 的 `apb.export`/`session.kill` 差异属于冻结版本边界，不通过本轮 XOUT 修复改变公开能力。
- X/Z 值显示紧凑逻辑字面量并附必要 bit 证据；已知值不显示 `known=true`、width 或等价 bits。这是可读性优化，不是能力差异。

## 七、最终验收状态

最终验收通过：候选 73/73 Action 具备与各自 JSON 一致的关键信息语义，复杂 Action 已有领域布局，所有已识别 P0/P1 均关闭，P2 均已修复或在本报告明确接受。GCC 13 的 414/414 pytest 与 9/9 CTest 通过；GCC 13 ASan 的 414/414 pytest 与 9/9 CTest 通过；GCC 13 UBSan 的 414/414 pytest 与 9/9 CTest 通过。
