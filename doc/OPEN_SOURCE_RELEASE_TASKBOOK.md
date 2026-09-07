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
| 0 任务书 | 已写入 | 基线 69f5a5c；仅既有 .tmp 未跟踪 |
| 1 许可审查 | 待执行 | 权属/合同审批须真实证据 |
| 2 环境定义 | 待执行 | 待读实际工具版本及下载来源 |
| 3 打包 | 待执行 | 原安装仅主程序且绝对 RPATH |
| 4 版本文档 | 待执行 | 原 metadata 硬编码原版 revision |
| 5 验收 | 待执行 | 三平台能力需检查可用 runner |
| 6 Draft Release | 待执行 | private 仓库；标签须绑定验收提交 |

所有测试记录实际命令、退出码和报告路径；没有执行的门禁不得标记通过。无法取得外部环境/审批时，继续完成独立工作并如实记录阻断。
