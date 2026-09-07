# 开源来源与分发边界

自有新增代码由个人开发者 BLANK2077 按根目录 BSD-3-Clause 发布（用户在本次发布任务中确认个人开发身份）。此声明不覆盖第三方代码、受外部合同约束材料或第三方商标。

| 组件 | 锁定来源与许可证 | 分发处理 |
| --- | --- | --- |
| xdebug-fst / wellenx 新增代码 | 本仓库、BSD-3-Clause | 随源码及二进制携带 LICENSE |
| 冻结 xdebug v1 schema、examples、参考材料 | compat/xdebug-v1/BASELINE.json；MIT，BLANK2077 | 保留 LICENSE.original、来源和哈希；不重写冻结文件 |
| Wellen / C API | dependencies.lock.json；BSD-3-Clause | 保留上游版权与许可，包含本地 patch、对应源码 |
| Verilator / DesignDB patch | dependencies.lock.json；上游逐文件许可 | LGPL-3.0 分发路径；保留 LGPL/Artistic 双许可和其他逐文件声明，提供完整对应源码及修改脚本 |
| nlohmann JSON | third_party/nlohmann 文件头；MIT | 保留文件头及许可，随源码提供 |
| JSON schema validator | third_party/json-schema-validator/LICENSE；MIT | 许可副本在 licenses/ |
| Rust 依赖 | rust/Cargo.lock、licenses/rust-dependencies.json | 锁文件全部组件审查；打包时逐文件校验 vendor 并携带源码、许可及 SPDX；清单明确包括构建、测试和非 Linux 目标 |
| GCC libstdc++ / libgcc | GCC 8.5.0 runtime / GCC 13.3.1 nonshared、GPL-3.0-or-later WITH GCC-exception-3.1 | 核对实际库来源；随附 GPL、Runtime Library Exception 和精确对应源码，缺对应材料不得标为公开可发布 |
| fixture、RTL、oracle、协议重放 | tracked 测试文件及冻结 provenance manifest | 独立来源审查；不因格式为 FST/JSON 或使用开源 producer 而推定已获授权 |

## 待关闭的公开发布事项

本文件是技术审查记录，不是第三方权利人的授权书。

- **GCC 对应源码**：已改用锁定的 AlmaLinux RPM，核对 SHA256 并附 GCC 8/GCC 13 源码 RPM；最终候选需通过同一打包校验。
- **测试资产来源**：必须逐类核查 VIP/XAMBA/SVT 派生重放与冻结输出的原始合同边界；现有原版 MIT 文件不足以自动覆盖 vendor 材料。
- **历史**：公开 Git 历史前审查专有产物与内部信息；不能只检查当前文件名。
- **Rust 全闭包**：licenses/rust-dependencies.json 按当前锁文件与本地 crate manifest 生成，许可证扫描不能代替全文和异常条款审查；发布打包必须校验 checksum 和许可文件。

没有公开审批结论的候选仅可保留在当前 private 仓库 Draft Release，不更改仓库可见性，不称为已获全面合规认证。

## 上游依据

- BSD: https://opensource.org/license/BSD-3-clause
- Verilator: https://verilator.org/guide/latest/copyright.html
- GCC exception: https://www.gnu.org/licenses/gcc-exception-3.1-faq.html.en

最终以锁定版本随附的完整许可文本及适用合同为准。不得将 Synopsys NPI/FSDB/VIP 安装内容、头文件、库、手册或许可凭据纳入本工具包。

本轮个人开发者审查与待确认项详见 `doc/RELEASE_LICENSE_REVIEW.md`。
