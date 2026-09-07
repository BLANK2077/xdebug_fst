# xdebug-fst 开源发布与工具链环境任务书

## 目标与验收合同

以 `69f5a5c` 为实现基线，交付 Linux x86_64 完整预编译包、对应源码、机器可读环境锁、环境准备/检查脚本和 GitHub Draft Release。目标平台为 EL8、Ubuntu 22.04/24.04；产品版本 0.1.0，首个候选标签 0.1.0-rc.1。

运行包包含 xdebug-fst、必要运行库、patched Verilator、schema/catalog、示例和许可材料。支持 CLI、JSON、XOUT、stdio-loop、UDS；MCP 为外部集成。GCC 编译器不随运行包分发，生成 simulator 的前置工具单独说明并检查。保留 73 Action 和冻结 xdebug.v1 schema，不修改 oracle 迁就实现。

环境版本必须覆盖 GCC/G++、rustc/Cargo、Python、CMake、Make、Autoconf、Flex、Bison、Perl、pkg-config、Git 和系统开发包。GCC/G++ 保持 13.3.1；Rust 依据现有成功环境及上游最低 1.90.0 要求锁定并验证。`toolchains.lock.json` 管工具链，现有 `dependencies.lock.json` 管上游 revision、patch 和 Cargo lock。提供 build/simulate/runtime 检查、显式联网准备、离线构建、独立前缀和容器入口；禁止隐式 fallback、sudo、清缓存或修改用户 shell 启动文件。

公开发布须关闭来源和许可审批；自有代码拟 BSD-3-Clause，第三方保留原许可。审查 Wellen、Verilator patch、Rust 全依赖、JSON、GCC 运行库、冻结 schema/oracle/RTL/VIP 派生测试资产及拟公开历史。权属和合同不能由自动扫描推定；不明项记录并阻断公开发布，不删除证据或改写历史。Draft Release 不代表许可批准，不修改仓库 private 可见性。

## 执行次序与阶段提交

1. **任务书与 Goal**：首先提交本任务书；第二步建立 Goal，后续进度全部更新本文件。
2. **来源审查**：建立组件/版权主体/版本/许可/分发义务/证据/审批状态清单；提供许可全文、SPDX 和对应源码要求。提交 `合规：建立开源来源清单与分发义务审查`。
3. **环境锁与工具**：核实并锁定实际版本；实现 `check_environment.py`、`prepare_environment.py`、Rust 固定版本、构建集成、EL8 容器；准备网络和离线入口，记录实际环境。提交 `构建：锁定工具链与环境依赖版本`、`工具：提供环境检查和在线离线准备入口`。
4. **可迁移交付**：完整 install/staging、相对运行库/schema 定位、patched Verilator、文件白名单、工具包/源码包/对应第三方源码/SHA256/SPDX/构建记录。提交 `发布：实现可迁移完整工具包与对应源码打包`。
5. **版本与文档**：help/version、真实构建身份、用户安装构建及 RTL→FST/DesignDB→查询示例、边界、并排升级回退、贡献指南。提交 `接口：统一实际构建身份与版本入口`、`文档：补全安装使用与维护指南`。
6. **验收**：统一检查入口及 CI，实际包三平台验收、全量 pytest/CTest、73 Action/基线/矩阵、独立 ASan/UBSan、长波形和异常/并发。提交 `测试：补全发布环境与安装产物门禁`。
7. **候选发布**：版本源码/构建/附件对应，显式标签绑定已验收提交；推送候选标签、建立 Draft Release、上传并下载核验。提交 `发布：准备首版预编译候选与发行材料`。无 PR、不更改远端默认分支、不发布正式版本、不公开仓库。

## 必需验收

- 错误/缺失工具、损坏归档、缺离线对象均非零退出，无替代下载或系统编译器 fallback。
- 显式准备后清洁独立目录断网构建；不依赖作者工作区或未申明缓存。两次构建比较并记录差异，未经验证不声称逐字节可复现。
- 安装包移到新目录、离开源码树仍工作；没有本机绝对 RPATH，不要求 LD_LIBRARY_PATH；显式错误数据目录必须失败。
- EL8/Ubuntu 22.04/24.04 完成版本、最小 RTL 编译仿真、session 查询关闭；安装目录可只读。
- 全量 pytest、CTest、73 Action、基线/矩阵和 ASan/UBSan 通过，无新增 skip/xfail 掩盖问题。已有报告 790 pytest 仅为历史证据。
- 测试损坏 FST/DesignDB、并发、退出、超时后健康度；长波形按时间点数、变化数、加载信号数独立扩展，记录延迟/RSS/预算，不放宽 timeout 掩盖失败。
- 发布物包含全部许可/对应源码，来源不明和权利人批准独立列为公开阻断。
- Draft Release 下载 SHA256 与本地候选一致，tag/source/binary/SPDX/环境/验收对应同一版本。

## 缓存、外部状态和权限

现有 `.tmp/` 未跟踪内容保留，不整体加入 Git。复用现有 fixture/cache，仅新增或确实变更场景才生成新资产。清洁验证使用独立 build，不重建原有 fixture。Wellen/Verilator 外部仓库只读。EDA 如确需执行，使用工具直接在非沙箱环境运行；不隐式重采原版 oracle。Git 远端操作/gh 在当前非沙箱执行环境运行。中文详细分阶段 commit。

## 执行账本

| 阶段 | 状态 | 证据/阻断 |
| --- | --- | --- |
| 0 任务书 | 完成 | 7cebed3；Goal 已建立，基线 69f5a5c |
| 1 许可审查 | 进行中 | 719df0d；用户确认个人开发；120 个 Rust 锁定组件许可已获取，GCC/LZ4 对应源码已下载；测试资产/历史公开边界待核查 |
| 2 环境定义 | 进行中 | 6ee6285；GCC 13.3.1、Rust/Cargo 1.97.1、Python 3.12.13、CMake 4.4.2 已核实；RPM 离线安装成功，87 项定向测试及 4 项环境测试通过 |
| 3 打包 | 进行中 | 已生成两次 install staging；补齐 schema/catalog、运行库和 Verilator 路径。实际仿真发现 LZ4/atomic 链接依赖，正在修复 |
| 4 版本文档 | 进行中 | 新 help/version 和真实 metadata 编译及定向测试通过；文档待补 |
| 5 验收 | 待执行 | 三平台能力需检查可用 runner |
| 6 Draft Release | 待执行 | private 仓库；标签须绑定验收提交 |

所有测试记录实际命令、退出码和报告路径；没有执行的门禁不得标记通过。无法取得外部环境/审批时，继续完成独立工作并如实记录阻断。

### 当前验证记录

- `.tmp/release-targeted-tests.log`：87 项 catalog/request/build 定向测试通过。
- `.tmp/release-env-tests.log`：4 项显式工具链/离线损坏包/CLI 身份测试通过。
- `.tmp/release-build-fix.log`：普通构建完成；新增统一 data_path 解决 catalog 与 schema 安装路径。
- `.tmp/prepare-gcc.log`：公开锁定 RPM 安装到独立前缀成功，gcc/g++ 自报告 13.3.1。
- 三个容器基础镜像已按 digest 拉取；容器构建第一次因宿主回环代理不可达失败，第二次以 host network 修正同一环境网络，未更换镜像或工具链。
- Cargo vendor 已覆盖全部锁定目标；无 license 字段未知的 crate。历史/合同审查尚未因此自动通过。

### 安装与环境阶段补充

- `.tmp/release-smoke-host.json`：本机从 staging 完成 RTL→FST/DesignDB→session 查询和关闭。
- `.tmp/platform-el8/smoke.json`、`.tmp/platform-ubuntu22/smoke.json`、`.tmp/platform-ubuntu24/smoke.json`：三平台只读挂载安装包、断网、带 init，7 类公开调用和 count=1 断言通过。当前为阶段 staging，最终 RC 仍需同门禁。
- Ubuntu 实测补齐编译器 MPFR 3.1.6/Jansson 2.14 依赖及 multiarch 路径，修正 GCC linker script 的发行版固定路径，未替换 GCC 13.3.1。
- 容器 PID 1 无 reaper 时 close 保留证据并失败；平台测试及文档明确使用 --init，没有放宽产品退出检查。
- `environment/el8-packages.lock` 冻结完整引导 RPM 集合；基础镜像 digest 和官方仓库地址写入工具链清单。
- `.tmp/environment-el8-install-v2.log`：在断网 EL8 容器从锁定归档安装 GCC/Rust/CMake，并源码编译 Python 3.12.13；安装流程继续验证中。

### 完整回归与冷环境发现

- EL8 完整 NEVRA 锁镜像成功，image ID `7a15f3b2fbdd8dcedc1afd11ba011c5423fba42cbce701aa8b694c11477ad126`。独立环境源码编译 Python 3.12.13 成功；冷依赖准备补齐 patch 2.7.6 和新 Git 仓库 detached HEAD。
- 初轮全量回归 774 通过、20 失败；修复测试临时目录和显式离线审计入口后，剩 2 项：AXI stress OSD 二次复杂度超时、当前测试证据哈希因源码变更需刷新。已改事件扫描；只更新矩阵中 current 测试的 hash/line，不修改 original consumers、oracle 或协议 schema。
- `.tmp/lw2/report.json`：阶段包长波形三个档位通过，峰值 RSS 163272 KiB，最大单请求约 446 ms。最终候选仍需运行。
- `.tmp/license-review.json`：120 个 crate 校验通过；扫描 2694 个历史 blob，364 个 vendor/保密标记候选，无定义的凭据形状命中。个人权属确认仍为公开阻断，详见同目录 RELEASE_LICENSE_REVIEW.md。
- 新增统一 verify_release 入口及手动 CI 三构建矩阵。CI 尚未在 GitHub 执行；独立冷构建和 sanitizer 正在本机执行。
- `.tmp/release-osd-tests.log`：OSD 优化后 43 项 AXI differential、P3-E 离线审计、矩阵定向测试全部通过；原超时和冻结响应保持不变。
- 发布门禁入口实测修复 pytest 自定义参数的 conftest 发现（显式 tests 路径），并将 Action trace 只传给全量 pytest，避免前序 CTest 占用同一路径。
- 包内 ELF 检查新增 glibc 上限、开发者路径和逃逸 symlink 拒绝；补齐 RPM doc 目录中的 LZ4 许可。旧 Verilator 缓存暴露 Make 不跟踪 DEFENV 编译参数问题：recipe stamp 升级时只重新编译嵌入默认路径的 V3Options.cpp，不清理 fixture 或整个缓存。
- `.tmp/release-final-pytest.xml`：普通构建 794/794、0 skip；`.tmp/release-action-coverage.json`：2069 条 trace，73/73 Action 的 10 个适用维度完整。
- `.tmp/gates-ubsan/result.json`：统一 UBSan 门禁全部通过（794 pytest、7 CTest、73 Action），halt_on_error=1，无 ASAN RSS 放宽环境变量。
- 两次独立断网完整构建均成功。干净检出验证 793/794，一项测试硬编码 build/；已改用被测二进制的构建目录，并同步 current 证据哈希。未重建冻结 fixture。
- 冷打包暴露选定 binutils/readelf 缺 libdebuginfod；新增精确 elfutils-debuginfod-client 0.190 RPM 及 readelf 2.40 build 检查。编译器核心版本和归档不变，重新安装独立完整 compiler prefix，复用已验证的 Python/Rust/CMake 和构建缓存。
