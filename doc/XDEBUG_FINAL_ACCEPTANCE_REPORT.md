# xdebug-fst 最终能力语义验收报告

日期：2026-08-13

## 一、结论与口径

当前开源实现已在冻结 xdebug v1 基线上达到用户要求的“能力一致、信息语义一致”。验收不要求
实现结构、JSON 字段顺序、无序集合排列、summary/warning/suggestion 措辞或冗余诊断与原版
完全相同；严格要求 73 个公开 action 的任务能力，以及目标、时间、值、关系、driver/source、
计数、范围、未知状态、完整性、截断和错误原因等关键事实语义一致。

当前只读原版安装在 Goal 建立后发生二进制/schema 漂移，因此没有把它重新冻结，也没有用其
当前输出覆盖基线。最终验收使用仓库内带哈希的冻结 catalog、schema、examples、归一化规则，
以及开发过程中已保存的原版能力差分断言；`tools/check_compat_baseline.py` 通过。

## 二、唯一数据流与三方架构

唯一波形事实路径为：

`当前 session 原始 .fst → Wellen 直接按需读取 → xdebug action 结合 Verilator DesignDB 静态事实分析`

- FST 是唯一运行时波形输入和事实容器，不是 HDL、driver、协议或 X-origin 分析引擎。
- Wellen 保真提供层级、alias、timescale、四态宽度、real/string/event、delta 和采样事实。
- Verilator 只在 `--design-db` 路径附加必要静态事实；普通编译、仿真和调度路径保持原行为。
- xdebug action 负责 predicate、driver、端口关系、协议、表达式、X 来源、limits、完整性、
  截断、错误合同及公开响应投影。
- 不存在 FST→VCD/JSON 转换、预扫持久化、私有索引、离线分析库、全量快照或 export 回灌。
- TCP/fileport 是用户明确裁剪项，不实现 server，也不存在隐式 fallback；UDS、one-shot、
  stdio-loop、MCP direct 和 fake-LSF 是实际支持并测试的入口。

## 三、版本与依赖锁

| 仓库 | 分支 | 验收 revision |
| --- | --- | --- |
| xdebug-fst | `feature/full-xdebug-parity` | `b3f960ebe1d79a949fa890fecf19504777fb0196`（验收代码；报告提交为 `8312f1c`） |
| Wellen | `feature/xdebug-fst-capi` | `afab0abd1fe4c06db9744f0b7b20b18d23b7f8df` |
| Verilator | `feature/design-db-for-xdebug` | `bf01d667c8b27f2f7cee456bb35a84e5372434df` |

仓库定位只使用 `XDEBUG_WELLEN_REPO` 与 `XDEBUG_VERILATOR_REPO` 的绝对路径。所有最终 C/C++
构建使用 `XDEBUG_GCC_TOOLCHAIN` 指向的 GCC/G++ 13.3.1。`dependencies.lock.json` SHA256 为
`fbcb09ca0898fde20023c08d18fa50ce7c19b8250a76c37820697aef4fd1ea8e`；冻结基线元数据
`compat/xdebug-v1/BASELINE.json` SHA256 为
`5861be0f26b63779bba8cc7ce676227d3d8e071f16f44d8c5fc3540c24055f17`。

## 四、73 action 能力与信息语义

在当前 revision 上重新运行全部 405 项 pytest，并将测试已经发送/收到的 public exchange 复制
到 `/tmp/xdebug-final-action-coverage-p7.ndjson`。trace 只用于覆盖审计，不读取或转换 FST、
不参与 action 执行、不提交仓库。审计结果：

| 维度 | 运行观察 | 冻结 N/A | 合计 |
| --- | ---: | ---: | ---: |
| success | 73 | 0 | 73 |
| invalid request | 73 | 0 | 73 |
| resource missing | 67 | 6 | 73 |
| empty result | 48 | 25 | 73 |
| boundary time | 31 | 42 | 73 |
| multiple results | 53 | 20 | 73 |
| limits | 29 | 44 | 73 |
| truncation | 35 | 38 | 73 |
| completeness | 41 | 32 | 73 |
| X/Z | 23 | 50 | 73 |

全新 trace 包含 1212 次已知 public action 交换，73/73 action 的十个维度均无 missing，且
observed 与 N/A 没有重叠。审计器会把任何重叠作为冲突并使 `--require-complete` 失败。
`clock_point_query` 和 `no.such.action` 是故意发送的 unknown-action 负例，不属于公开 catalog。
trace、JSON 报告和 Markdown 报告 SHA256 分别为：

- `c7850a7ce93c04693c3e62f48eec7f87c8b19eca8d0ac446af1bb009db83abed`
- `f407da0d0d9a23fbba98ca8067849d0ad0f765bb46c25c25f7926749e4341fa6`
- `5909446cb8f1aa313b8c318416fc90b265260de692dfa9f8a29ab7f15afacbc3`

覆盖审计本身不是逐字段原版证明；能力语义由公共 schema gate、各 action 真实断言和 P6 七个
能力族共同承担。P6 清单验证活动 predicate/pattern、时序与 driver 优先级、module/interface/
ref/alias、多 driver 歧义、X-origin 分支/环/来源、time/limits、typed/delta 波形事实均有真实
证据。tagged 等仅改变 Verilator 前端写法而不引入新可观察事实的组合，不是独立 xdebug 能力。

## 五、最终执行结果

| 层级 | 结果 |
| --- | --- |
| GCC 13 普通 pytest | 405/405 通过 |
| GCC 13 普通 CTest | 9/9 通过 |
| GCC 13 ASan | CTest 9/9、pytest 405/405，通过；leak/遇错即停开启，无诊断 |
| GCC 13 UBSan | CTest 9/9、pytest 405/405，通过；遇错即停开启，无诊断 |
| session 生命周期 | 并发、重复 open/doctor/close、FD/RSS、SIGKILL/gc、MCP direct/fake-LSF 全部通过 |
| Wellen workspace | 207 项运行通过，8 项上游标记 ignored，0 失败 |
| Wellen C API | 端到端 C 测试通过，实际读取原始 FST、signal ref 0/1 基映射、时间和值 |
| wellenx C API | 2/2 通过 |
| Verilator DesignDB | 当前 14 个 `t_xdd_*` 独立通过，含 simple/full/UART/metadata |
| Verilator 普通行为 | 16 个 matches/tagged/interface/inout/coroutine 代表回归通过 |
| Verilator 构建/规范 | GCC 13 `make -C src -j2`、copyright、cppstyle 通过 |
| 冻结基线/适用性 | compat baseline、P6、boundary/completeness/empty/limit/multiple/truncation/XZ 全通过，N/A 与 observed 零冲突 |

所有测试均在本机开源环境直接运行，没有 license 依赖，也没有执行沙箱外 fallback。Verilator
普通 timing 用例实际使用已安装到对应仓库链路的 coroutine 支持并通过。

## 六、Git 与交付状态

- 三个仓库源码工作树在最终报告写入前均干净；构建与 test_regress 产物均被既有 ignore 排除。
- 阶段 annotated tag 已补齐 `parity-p0` 至 `parity-p6`；本报告提交后创建 `parity-p7`。
- 所有 xdebug-fst 提交使用详细中文标题和正文；未创建 PR、未推送远端。
- Git 中没有 FSDB、daidir、NPI header/library 或其他 proprietary 数据。提交的 `.vcd` 仅为
  可读 fixture 源描述，验收和生产 action 实际打开的仍只有 `.fst`。

在本报告提交、`parity-p7` 创建、三个仓库再次确认干净后，计划中的必需任务即全部完成。
