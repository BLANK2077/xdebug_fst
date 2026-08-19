# 本机绝对路径清理与全历史重写任务书

## 目标

从当前版本以及所有可达 Git 历史中清除 `/workspace` 和 `${REPO_ROOT}/..`
等本机路径。运行时所需路径统一通过环境变量提供；`.codex/` 不再由 Git
跟踪，但本机保留 `.codex/config.toml`。

全部修改、分阶段提交、测试、历史改写与全引用审计必须先在本地完成。只有
所有验收通过后，才允许一次性强推三个远端分支和八个 parity 标签；不得中途
推送，不创建 PR。

## 环境变量合同

| 变量 | 用途 | 本机值 |
| --- | --- | --- |
| `WELLEN_HOME` | 官方 Wellen 本地 Git 对象库 | 仅写入被忽略的本机 Codex 配置 |
| `VERILATOR_HOME` | 官方 Verilator 本地 Git 对象库 | 仅写入被忽略的本机 Codex 配置 |
| `XFST_CONDA_ENV` | pytest runner Python | 仅写入被忽略的本机 Codex 配置 |
| `XDEBUG_ORIGINAL_ROOT` | 只读原版 xverif checkout | 仅写入被忽略的本机 Codex 配置 |

`--original-root` 显式参数优先于 `XDEBUG_ORIGINAL_ROOT`。需要原版 checkout 的
工具在二者均缺失时必须 fail closed。`XFST_CONDA_ENV` 缺失时使用当前
`sys.executable`，不得提供开发者本机默认路径。

## 验收标准

- [ ] `.codex/` 从当前索引和全部历史移除，并由 `.gitignore` 忽略。
- [ ] 本机 `.codex/config.toml` 保留四个环境变量，但不出现在任何 Git tree 中。
- [ ] 当前版本的脚本、测试、冻结元数据和文档不含本机绝对路径。
- [ ] 历史文本与二进制对象均不含 `/workspace` 或 `${REPO_ROOT}/..`。
- [ ] 不参与运行的生成 Makefile、`__verFiles.dat` 和仿真程序不再跟踪。
- [ ] 必需的 matches DesignDB 使用相对源码路径生成，FST 语义和 XDD ABI 不变。
- [ ] 新增路径泄漏门禁，同时覆盖已跟踪文本、二进制和 `.codex/`。
- [ ] 环境变量、参数优先级和缺失变量 fail-closed 测试通过。
- [ ] 兼容基线、依赖合同、CTest、完整 pytest 和相关 XDD 回归通过。
- [ ] Wellen、Verilator HOME 仓库在操作前后保持干净且未改变。
- [ ] 三个远端分支与八个 parity 标签只在全部本地验收后一次性强推。
- [ ] 强推后以全新 clone 复核全部远端 refs 和提交对象。

## 分阶段实施

### 阶段 0：任务书、Goal 与恢复点

- [x] 将完整计划写入本文件。
- [x] 建立抽象目标与验收标准明确的 Goal。
- [x] 记录所有本地/远端 refs 的旧 SHA，并确认远端无漂移。
- [x] 在仓库外创建包含全部 refs 的 Git bundle，记录恢复方式。
- [ ] 独立中文提交本任务书；不推送。

### 阶段 1：环境变量与 Codex 配置隔离

- [x] `.gitignore` 忽略整个 `.codex/`，从当前索引移除配置但保留本机文件。
- [x] 本机配置写入四个环境变量。
- [x] pytest runner 删除本机 Python 默认值。
- [x] 原版兼容工具支持 `XDEBUG_ORIGINAL_ROOT` 与显式参数优先级。
- [x] 增加环境合同单元测试并独立中文提交；不推送。

### 阶段 2：生成物与 Fixture 路径清理

- [x] 删除已被统一 fixture 工具取代的 `.build_fixtures.sh`。
- [x] 取消跟踪并忽略非运行必需的生成元数据和仿真程序。
- [x] 以相对源码路径和 prefix-map 重建 matches DesignDB。
- [x] 审计 FST、DesignDB 导出及相关行为，必要 fixture 变化独立提交；不推送。

### 阶段 3：元数据、文档与防回归门禁

- [ ] `BASELINE.json` 使用 `env:XDEBUG_ORIGINAL_ROOT` 描述来源。
- [ ] 当前文档全部改用环境变量、仓库相对路径或中性示例。
- [ ] 新增同时扫描文本和二进制的已跟踪路径门禁。
- [ ] 当前 HEAD 路径扫描为零命中并独立中文提交；不推送。

### 阶段 4：本地完整回归

- [ ] 运行环境合同与路径泄漏定向测试。
- [ ] 运行兼容基线、依赖合同、CTest、完整 pytest 与相关 XDD 回归。
- [ ] 确认仅重建必要的 matches fixture，其他 fixture/cache 不变。
- [ ] 确认两个 HOME 仓库状态不变。

### 阶段 5：全历史改写与本地全引用审计

- [ ] 在临时镜像中改写约 376 个提交，删除历史 `.codex/`。
- [ ] 历史文本使用环境变量或中性路径，历史二进制使用等长中性替换。
- [ ] 改写三个分支和 `parity-p0` 至 `parity-p7` 八个标签。
- [ ] 删除改写工具留下的原始 refs/reflog，并在镜像中完成垃圾回收。
- [ ] 对每个改写引用扫描文本、二进制及 Git tree，全部为零命中。
- [ ] 在改写后的当前分支再次完成完整回归。

### 阶段 6：一次性强推与远端复核

- [ ] 强推前用 `ls-remote` 对比阶段 0 快照；发生漂移立即停止。
- [ ] 一次性强推三个分支和八个标签，不进行中间推送。
- [ ] 从远端全新 clone，复核所有 refs、文本、二进制和 `.codex/`。
- [ ] 同步当前工作区到改写后分支，恢复本机忽略配置并确认工作树干净。
- [ ] 更新本任务书最终结果并完成 Goal。

## 历史改写与恢复约束

- 全部相关提交 SHA 会改变，其他使用者必须重新 clone 或硬重置。
- 仓库外 bundle 保留旧路径，仅用于本地恢复，绝不上传。
- Git 强推只能消除远端 refs 对旧提交的可达性，不能控制 fork、既有 clone 或
  GitHub 内部缓存。
- 远端漂移、分支保护或强推拒绝均不得通过备用发布方式绕过，必须停止并报告。

## 进度记录

| 日期 | 阶段 | 状态 | 证据 |
| --- | --- | --- | --- |
| 2026-08-19 | 阶段 0 | 进行中 | 任务书已建立；尚未修改运行代码、fixture 或远端 refs。 |
| 2026-08-19 | 阶段 0 | 完成 | Goal 已建立；远端三个分支和八个标签已记录；完整恢复 bundle 为仓库外 `xdebug_fst-pre-path-scrub-20260819.bundle`，`git bundle verify` 确认历史完整。远端 feature tip 为 `48f07b4`，本地另有 10 个未推送提交，改写时分别保留且不把这 10 个提交发布到远端。 |
| 2026-08-19 | 阶段 1 | 完成 | `.codex/` 已退出索引并被忽略，本机配置保留四项变量；pytest 默认使用 `sys.executable`；三个兼容工具统一支持 `XDEBUG_ORIGINAL_ROOT`，显式参数优先。环境合同、兼容基线与构建合同定向测试 9/9 通过。 |
| 2026-08-19 | 阶段 2 | 完成 | 删除旧 fixture 总脚本及 20 个无消费者的 Makefile、verFiles 元数据和仿真程序；四个受支持重建脚本加入 prefix-map。matches FST 与旧文件逐字节一致，DesignDB 全部动态导出一致且不再含本机路径，相关行为测试 4/4、fixture 哈希校验通过。 |
