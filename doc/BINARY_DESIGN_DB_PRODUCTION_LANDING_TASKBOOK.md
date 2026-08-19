# binary-v1 DesignDB 正式落地任务书

## 一、目标

把已通过性能探索的组合路线正式落为 xdebug-fst 的默认生产路径：patched Verilator 在一次
前端处理中生成仿真模型、FST 能力和完整 `binary-v1` DesignDB bundle；xdebug-fst 严格按
manifest 选择 mmap reader，在 session open 时建立后端无关查询索引，并把 Wellen 完整名称
索引后的 miss 作为确定 miss。

正式落地不是删除兼容能力。旧 `xdebug.design-db-bundle.v1` `.so` reader 和
`--design-db` 继续用于兼容测试，但新建工程、开发文档、标准集成门禁和性能验收均以
`--design-db-binary` 为主路径，不再要求用户手工编译 DesignDB C++ 或手写 manifest。

## 二、冻结边界

- Verilator/Wellen 继续使用 `dependencies.lock.json` 锁定的官方 revision；所有修改只保存在
  xdebug-fst patch，不依赖个人 fork 或分支。
- C/C++ 只使用同级 `xdebug_oc/.toolchains/gcc-13` 中 GCC/G++ 13.3.1；版本、真实路径或
  CMake cache 不匹配时失败关闭，禁止系统编译器 fallback。
- Wellen 继续只按需读取原始 FST；不生成 VCD/JSON/私有波形索引或离线全量快照。
- binary manifest、文件和 backend 选择必须严格一致；禁止扩展名猜测、失败后改开 `.so`。
- 默认不改动或重建 tracked fixture cache。正式端到端门禁使用临时目录或 build tree 中的
  小型 RTL 冷构建；legacy fixtures 只承担兼容语义回归。
- 不提交生成的 RTL、FST、`.xddb`、obj_dir、日志、本机绝对路径或构建产物。
- 不走 PR、不推送远端，除非用户后续明确要求。

## 三、正式产品合同

### 3.1 Verilator 生产合同

`--design-db-binary` 必须在同一 Verilator invocation 的同一 `--Mdir` 中生成：

1. `<prefix>__DesignDb.xddb`；
2. `xdebug-design-db.json`，严格为 bundle v2，`format` 固定为 `binary-v1`，`database`
   只包含同目录相对文件名；
3. 普通 `--cc/--exe/--build/--trace-fst` 产物按原 Verilator 语义生成。

`.xddb` 与 manifest 必须采用临时文件加 rename 的提交顺序。任一写入、flush、close 或 rename
失败均使 Verilator 返回失败，不能留下一个看似完整但混合新旧世代的可消费 bundle。
`--design-db` 与 `--design-db-binary` 保持互斥；前者继续只生成 C++ 兼容产物。

### 3.2 xdebug-fst 消费合同

- session manager 只读取严格 v1 `.so` 或严格 v2 `binary-v1` manifest，并把解析出的格式显式
  传给 engine。
- engine 对 binary 使用只读 mmap，对 `.so` 使用 legacy dlopen；两条路径均构建同一个
  `DesignQueryIndex`，不复制 Action 语义。
- Wellen `build_signal_index()` 完成后，exact/local/packed-selection miss 直接返回不存在；
  不允许恢复 hierarchy 全量重扫。
- `session.open/get/list` 的公共 session evidence 显式返回 `design_db_format`；registry 持久化
  同一字段，使重启后查询不会丢失或猜测实际 backend。

### 3.3 用户入口合同

- `tools/build.sh` 仍一次构建 patched Verilator、Wellen C API 与 xdebug-fst，并在完成信息中
  给出正式 Verilator 路径。
- 开发指南给出一个可复制的 binary-v1 单次构建示例，说明 `obj_dir` 可直接作为
  `target.daidir`，无需第二次 DesignDB 编译或转换。
- converter 保留为迁移旧 `.so` bundle 的显式工具，不作为新设计默认生产路径。

## 四、测试策略

### 4.1 Producer 单元与回归

- Verilator `t_xdd*` 全部继续通过。
- 新增/扩展 Verilator 回归，检查 binary 模式同时生成严格 manifest，且未生成
  `__DesignDb.cpp`；legacy 模式只生成 C++，不生成 `.xddb`/v2 manifest。
- 覆盖自定义 `--prefix`、`--Mdir`、重复构建覆盖和不可写/提交失败路径。
- producer 直接输出继续与 `.so` 全字段、关系顺序及 resolve 行为 parity。

### 4.2 xdebug-fst 合同与集成

- 增加 SessionInfo 序列化/反序列化、registry 和 public response 的
  `design_db_format` 测试；旧 registry 记录缺字段时只能按明确兼容规则处理，不能猜 artifact。
- 增加临时小型 RTL 冷构建集成：调用统一构建产出的 patched Verilator，一次 invocation
  生成 simulator + FST + binary bundle，再由 xdebug-fst `session.open` 并执行
  `signal.resolve`、`value.at`、`trace.driver`、`trace.active_driver`、
  `trace.active_driver_chain`、`signal.changes`。
- 集成测试断言 session evidence 为 `binary-v1`、六类响应完整且进程实际 mmap `.xddb`；
  不调用 converter，不编译 `__DesignDb.cpp`，不读取 `.so`。
- legacy `.so` fixture 继续跑全量测试，确保兼容路径没有被默认路线改坏。

### 4.3 失败关闭与性能守护

- 保留并扩展 manifest/schema、截断、section、字符串和关系越界测试。
- 生产 manifest 指向缺失/错误扩展/越界 symlink 时必须在 session start 前拒绝。
- 64K 不重跑完整矩阵；复用已冻结报告数据，并增加轻量合同确保查询索引始终建立、Wellen
  miss 路径不再出现 recursive fallback。只有实现改变格式或算法时才重跑对应性能门禁。

## 五、实施阶段与提交

### 阶段 0：任务书、goal 与现状冻结

- [x] 审计实验实现、默认构建、fixture、文档和 session evidence。
- [x] 写入本任务书并冻结正式产品合同。
- [x] 建立 goal，以第六节为唯一验收标准。
- [x] 记录当前 patch/lock、依赖 revision、工作树和 fixture cache 状态。

计划提交：`文档：建立 binary-v1 DesignDB 正式落地任务书`

### 阶段 1：原子 producer bundle

- [x] 修改 Verilator patch，让 binary 模式原子生成 `.xddb` 与严格 manifest。
- [x] 补齐 prefix/Mdir、互斥产物、重复构建及写入失败回归。
- [x] 更新 patch hash/版本和依赖锁，使用私有 GCC/G++ 重建 patched Verilator。

计划提交：`构建：让 Verilator 原子生成 binary-v1 bundle`

### 阶段 2：格式证据与 session 持久化

- [ ] 为 SessionInfo、registry 和公共 session JSON 增加 `design_db_format`。
- [ ] 明确旧 registry 记录的兼容读取规则及新记录的严格校验。
- [ ] 覆盖 open/get/list、重启恢复和非法格式测试。

计划提交：`功能：持久化并公开 DesignDB backend 格式`

### 阶段 3：正式端到端门禁

- [ ] 建立不依赖 fixture cache 的小型 RTL 冷构建测试。
- [ ] 证明单次 Verilator invocation 生成 simulator、FST 与可直接打开的 binary bundle。
- [ ] 执行六类 Action，并证明无 converter、无 DesignDB C++/SO、实际 backend 为 mmap binary。
- [ ] 保留 `.so` compatibility 和完整 parity 回归。

计划提交：`测试：增加 binary-v1 默认生产路径端到端门禁`

### 阶段 4：默认文档、全量验收与收尾

- [ ] 更新开发指南、架构文档和 fixture 再生成说明，以 binary-v1 为新设计默认路径。
- [ ] 运行统一构建、CTest、全量 pytest、Verilator 回归和直接 producer/backend parity。
- [ ] 检查 testdata diff、绝对路径、`.codex`、patch/lock 和工作树卫生。
- [ ] 更新本任务书进度与正式落地报告。

计划提交：`文档：发布 binary-v1 DesignDB 正式生产路径`

## 六、验收标准

1. patched Verilator 一次 invocation 原子生成 simulator/FST 所需产物、`.xddb` 和严格 v2
   manifest；用户不再手写 manifest，也不再编译 DesignDB C++/SO。
2. producer 失败不会发布半成品或新旧混合 bundle；自定义 prefix/Mdir、重复构建、互斥模式
   和写入失败都有自动测试。
3. xdebug-fst 严格按 manifest 使用 mmap binary；无扩展名猜测、`.so` fallback 或 converter
   隐式参与。
4. session open/get/list 与 registry 重启恢复均持久化、返回 `design_db_format=binary-v1`；非法或
   矛盾格式失败关闭。
5. `DesignQueryIndex` 对 binary/legacy 共用并在 design session 中必建；Wellen 完整索引 miss
   不再 hierarchy 重扫，均有防回退测试。
6. 从临时 RTL 冷构建到六类 Action 的标准端到端测试通过，并证明没有 `__DesignDb.cpp/.so`；
   legacy `.so` 兼容和 SO/binary 完整 parity 同时通过。
7. 统一构建只使用锁定官方 Verilator/Wellen revision 和私有 GCC/G++ 13.3.1，patch/hash/lock
   一致，无工具链或依赖 fallback。
8. CTest、全量 pytest 和 Verilator `t_xdd*` 全部通过；已有性能结论未被新路径破坏，若关键
   指标回退则记录数据并修复后再验收。
9. tracked fixture cache 无 diff；仓库不包含本机绝对路径、`.codex` 或生成大产物，工作树只含
   本 goal 的分阶段中文详细提交；是否推送由用户另行明确指示。

## 七、风险与停止条件

- Verilator manifest 与 `.xddb` 的双文件提交无法跨文件做到文件系统事务；采用“先完成并
  rename 数据，再原子 rename manifest”的发布顺序，以 manifest 作为唯一可见提交点。
- 旧 registry 没有格式字段。兼容规则必须由已有持久化信息确定；若无法无歧义恢复，旧记录
  应明确失效并要求重开 session，不能根据文件扩展名猜测。
- 真实端到端测试会构建一个全新小设计，但只写 build/tmp，不修改 tracked fixture cache；
  若环境缺少锁定依赖对象或私有工具链则直接失败，不切换系统工具。
- 若正式化需要改变 binary-v1 格式，必须先停止并修订任务书、版本号和性能基线，不能在 v1
  名义下静默改变布局。

## 八、进度记录

- 2026-08-19：完成正式化缺口审计。确认 binary mmap、查询索引和 Wellen definitive miss
  已实现并通过探索验收，但 producer manifest、session 公共格式证据、默认冷构建门禁和主路径
  文档仍缺失；未修改 fixture cache。
- 2026-08-19：建立正式落地 goal。冻结 Verilator revision
  `c3be3c80501bec8d4c3b7f5256890332df62d410`、Wellen revision
  `34f40595c4330bcac364397679cdc3d5ae6b5c0c`、Verilator patch SHA-256
  `226de48a441d2b7983dee74da385a91b5af3f45a22d5a6d8e4e15c7afa30f64a` 和
  GCC/G++ 13.3.1；patch 与 lock 一致，`testdata/` 无 diff。
- 2026-08-19：完成 producer 正式化。`--design-db-binary` 先完整写入临时 `.xddb` 和
  manifest，撤销旧 manifest 后替换数据库，最后以 manifest rename 作为 bundle 可见提交点；
  正常、重复、自定义 prefix/Mdir、legacy 互斥和阻塞 manifest 失败场景均通过。patchset 升级为
  `xdebug-design-db-v3-production-bundle`，在锁定官方 revision 上由私有 GCC/G++ 13.3.1
  完成统一冷构建；未修改 tracked fixture cache。
