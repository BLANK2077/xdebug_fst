# Wellen/Verilator 依赖内聚与统一构建迁移计划

## 目标

将 xdebug 所需的 Wellen C API 与 Verilator XDD 扩展全部维护在本仓库中，构建仅引用官方 Wellen、Verilator 仓库中锁定的版本，不再依赖私人 fork、功能分支或预构建动态库。

统一入口为：

```bash
WELLEN_HOME=/path/to/official/wellen \
VERILATOR_HOME=/path/to/official/verilator \
./tools/build.sh
```

`WELLEN_HOME`、`VERILATOR_HOME` 只作为本地 Git 对象库。构建从锁定提交归档影子源码，不改变它们的 HEAD、索引或工作区，不执行 fetch，也不根据当前 checkout 猜测版本。

C/C++ 编译器固定为仓库同级 `xdebug_oc/.toolchains/gcc-13/bin/gcc` 和 `g++`。必须验证版本为 13.3.1；不允许回退到系统 GCC。

## 验收标准

- [ ] `dependencies.lock.json` 是依赖版本的唯一配置源，记录官方 URL、可读版本、精确 revision/tree、patchset 与 ABI 哈希。
- [ ] Wellen C API 位于本仓库，与 `wellenx_capi` 由本仓库 Rust workspace 一并构建。
- [ ] Verilator XDD 修改以本仓库 patch/overlay 形式维护，可重复应用到锁定的官方 master 提交。
- [ ] 只设置 `WELLEN_HOME`、`VERILATOR_HOME` 即可一次生成两套 Rust C API、patched Verilator 与 `xdebug-fst`。
- [ ] 所有 C/C++ 编译均使用私有 GCC/G++ 13.3.1；CMake cache、构建清单和 sanitizer 运行库检查均可证明未使用系统 GCC。
- [ ] HOME 仓库缺少锁定对象、patch 冲突、工具链错误或旧 CMake cache 使用其他编译器时 fail closed，不执行 fallback。
- [ ] CTest、完整 pytest、14 个 `t_xdd_*` 回归以及 ASan、UBSan 验收通过。
- [ ] 官方 HOME 仓库构建前后的 HEAD、索引与工作区状态保持不变。
- [ ] 除非新 Verilator 的真实输出变化要求更新，否则不重建 fixture cache；必要变化独立提交并附差异说明。

## 锁定基线

| 依赖 | 官方仓库 | 版本说明 | 锁定提交 |
| --- | --- | --- | --- |
| Wellen | `https://github.com/ekiwi/wellen.git` | `v0.25.6` | `34f40595c4330bcac364397679cdc3d5ae6b5c0c` |
| Verilator | `https://github.com/verilator/verilator.git` | `v5.050-109-gc3be3c805` | `c3be3c80501bec8d4c3b7f5256890332df62d410` |

## 分阶段实施与提交

### 阶段 0：基线、计划与 Goal

- [x] 在任何迁移修改前推送当前分支远端。
- [x] 将完整任务书、阶段状态和验收标准写入本文件。
- [x] 建立 goal，抽象迁移目标并写明验收要求。
- [ ] 提交并推送本计划文档。

### 阶段 1：迁入依赖侧修改

- [ ] 从 Wellen 功能分支提取生产需要的 `wellen_capi`，迁入本仓库；不迁入 query/example 实验工具。
- [ ] 建立仓库级 Rust workspace，使 `wellen_capi`、`wellenx_capi` 均依赖影子 Wellen 源码。
- [ ] 将 Verilator 功能分支相对锁定基线的 XDD 修改整理成有序 patch/overlay，并记录 patchset 哈希。
- [ ] 验证 patch 在新归档基线上可完整、重复地应用。
- [ ] 独立提交并推送。

### 阶段 2：版本锁与影子源码解析器

- [ ] 升级 `dependencies.lock.json`，以 JSON 为唯一版本来源；移除手写的 CMake lock 副本。
- [ ] 新增依赖准备工具：校验环境变量与 Git 对象、通过 `git archive` 解包、校验 tree、应用 patch、生成 stamp。
- [ ] 生成 `build/dependencies.resolved.json`；所有失败路径禁止 fetch、分支切换或版本 fallback。
- [ ] 测试任意 HOME checkout、脏工作区、对象缺失、patch 冲突和哈希不一致。
- [ ] 独立提交并推送。

### 阶段 3：私有工具链门禁与统一构建

- [ ] 新增 `tools/build.sh`，从 `${REPO_ROOT}/../.toolchains/gcc-13` 解析工具链。
- [ ] 强制绝对 `CC/CXX`，验证真实路径及 GCC/G++ 13.3.1；已有用户 `CC/CXX` 不得覆盖。
- [ ] CMake、Verilator configure/make、Cargo native build script 使用同一工具链；构建和测试优先解析私有 `lib64`。
- [ ] 发现旧 CMake cache 使用其他编译器时失败，不自动删除缓存。
- [ ] 生成 `build/toolchain.resolved.json`，记录工具链路径、版本与构建身份。
- [ ] 建立统一增量构建图，输出 Wellen C API、Wellen 扩展、patched Verilator 和 `xdebug-fst`。
- [ ] 独立提交并推送。

### 阶段 4：环境变量、测试与文档迁移

- [ ] 删除 `XDEBUG_WELLEN_REPO`、`XDEBUG_VERILATOR_REPO` 与私人分支约束，仅使用 `WELLEN_HOME`、`VERILATOR_HOME`。
- [ ] 测试引用影子源码及统一构建产物，不读取 HOME 当前工作区。
- [ ] 增加依赖、工具链、cache 编译器不一致等 fail-closed 测试。
- [ ] 更新 README/开发文档及 fixture 重建脚本。
- [ ] 独立提交并推送。

### 阶段 5：Fixture 审计与完整验收

- [ ] 先运行不重建 fixture 的单元测试、CTest、`t_xdd_*` 和完整 pytest。
- [ ] 只对 `counter`、`interface_modport`、`matches`、`phase5` 做代表性重新生成和语义比较。
- [ ] 只有确认 Verilator 输出真实变化时才更新 fixture/cache，并使用独立提交记录差异。
- [ ] 分别完成普通、ASan、UBSan 回归；确认 sanitizer 运行库来自私有工具链。
- [ ] 比较依赖 HOME 仓库构建前后状态，确认未发生任何修改。
- [ ] 更新本文件的结果、完成 goal、推送最终分支，不创建 PR。

## 进度记录

| 日期 | 阶段 | 状态 | 记录 |
| --- | --- | --- | --- |
| 2026-08-18 | 阶段 0 | 进行中 | 当前分支 `fix/per-session-registry-flock` 已在迁移修改前推送至 origin；计划文档与迁移 goal 已建立。 |

## 约束与失败策略

- 不修改两个 HOME 仓库；所有归档、patch 和编译均发生在 `build/`。
- 不自动下载依赖，不创建 PR，不私自选择备用版本或编译器。
- 构建缓存以 revision、tree、patchset、Cargo lock 和工具链身份共同失效；无关变化保留增量缓存。
- fixture/cache 变化仅限必要情形，并与代码迁移分开提交。
- 任何计划偏差先记录在本文件；若需要 fallback 或扩大范围，先向用户申请。
