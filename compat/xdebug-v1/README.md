# 原版 xdebug v1 兼容基线

本目录冻结原版 xdebug runtime revision `8eecf71271cc` 对外发布的合同，用于
xdebug-fst 的离线实现、schema 校验和归一化差分。原版仓库保持只读，本目录不包含
原版 executable、engine、NPI/FSDB header、vendor library、FSDB、daidir 或任何
Synopsys proprietary payload。

冻结内容：

- `catalog.response.json`：从原版二进制执行无过滤 `actions` 请求得到的完整 JSON；
- `schemas/v1`：282 个 v1 schema，其中 `actions/` 下 146 个 public request/response schema；
- `LICENSE.original`：原版仓库 MIT License；
- `THIRD_PARTY.original.md`：原版仓库的第三方与 proprietary 边界声明；
- `BASELINE.json`：runtime、schema、数量和来源 revision；
- `SHA256SUMS`：全部冻结文件的逐文件哈希。

`repository_head` 与 `runtime_git_revision` 不同是有意记录：采集时原版工作树 HEAD
已经前进且存在用户修改，但被执行的已构建 runtime 明确报告 revision
`8eecf71271cc`。兼容基线以 runtime 自报告 build/schema revision、实际 action 响应
和复制后逐文件 hash 为准，不把原版当前工作树状态误写成二进制版本。

更新本基线必须单独提交，并同时更新 catalog、schemas、hash、revision、漂移检查和
差分预期。不得为了让新实现通过而直接修改冻结 schema。
