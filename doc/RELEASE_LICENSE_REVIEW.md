# 0.1.0 个人开发者许可审查

维护者于 2026-09-07 确认自建 XAMBA VIP，并明确授权正式发布、推送后公开仓库。公开发布状态：**维护者已授权，正式附件技术验收完成，仓库已公开**。审查责任人为个人维护者 BLANK2077；不设置公司法务或组织审批流程。

## 已执行的技术检查

- 自有新增代码声明 BSD-3-Clause；冻结参考材料保留原 MIT 许可和来源哈希。根许可不覆盖第三方权利。
- 120 个 Cargo registry 包逐文件对照 vendor 的 `.cargo-checksum.json`，并将包 checksum 与 `rust/Cargo.lock` 比较。要求集合完全一致、许可声明及许可材料存在。`r-efi 6.0.0` 的许可全文位于 `AUTHORS`，必须随包保留。
- Rust 清单包括非 Linux、构建与测试依赖，不把整个锁文件误称为动态运行时依赖。涉及 Unicode、LLVM exception 等附加许可的组件保留原声明和全文；多选许可不自动归为 MIT。
- 运行包使用锁定的 AlmaLinux GCC 8.5.0 runtime RPM；GCC 13.3.1 用于构建，其中 nonshared 代码可能进入二进制。对应 GCC 8/GCC 13 发行源码 RPM 均纳入对应源码包；LZ4 RPM 和源码同样用 SHA256 核对。
- Wellen/Verilator 使用 `dependencies.lock.json` 指定的 commit、tree 和 patch，源码包保留原始源码、补丁及离线 Git bundle。Verilator 按 LGPL 路径准备对应源码，保留其双许可和逐文件其他声明。上游说明见 [Verilator Copyright](https://verilator.org/guide/latest/copyright.html)。
- 对当时全部本地可达历史扫描 2694 个 blob，364 个 blob 命中 vendor/保密词筛选；未命中脚本定义的私钥、GitHub token、AWS key 形状。命中不等于侵权，未命中也不等于无敏感信息。报告只输出对象 ID、路径和原因，不输出凭据内容。

重复检查入口：

```bash
python3 tools/audit_release_sources.py --vendor /absolute/vendor \
  --history --output /absolute/review.json
```

该脚本成功只表示列出的机械校验通过，报告中的 `public_release_approved` 始终为 false。历史扫描范围随本地 refs 变化，发布前重新执行。

## 来源核查范围与维护者授权

| 范围 | 已知证据 | 核查边界 |
| --- | --- | --- |
| 自有代码、Verilator/Wellen patch | 本地 Git 历史与自有版权声明 | 是否存在雇佣、委托或第三方合同限制 |
| SVT/XAMBA/APB/AXI pin-level mirror、冻结波形和 JSON oracle | 文件声明为无外部依赖的语义镜像，仓库保存 producer、consumer 和哈希 | 镜像、原始激励及导出结果是否允许再分发；有 MIT 参考文件不足以证明 vendor 资产授权 |
| 原始 RTL、测试来源、文档片段 | provenance manifest、逐文件扫描 | 来源是否均为自有或有明确公开授权，尤其原 EDA/VIP 关联材料 |
| 完整 Git 历史 | 所有本地可达 refs 的模式扫描报告 | 对命中对象和曾删除文件进行人工来源核查，不能仅审当前树 |

不分发 Synopsys 安装目录、库、头文件、手册或 license 凭据。个人开发身份并不自动解除第三方分发义务。本次授权适用于仓库正式发布和公开；不将 XAMBA 来源确认泛化为 SVT 历史对照数据均来自 XAMBA，也不授予任何第三方专有安装材料的分发权。原 RC 的历史技术报告保留原状态，正式发布证据另行记录维护者授权。


## 正式版本核验结果

0.1.0 已正式发布为 Latest，仓库已按维护者指令在推送后公开。正式源码提交 c8cbc65f85168754d687fd9f7e723409ac20832a。120 个 Rust 包、对应第三方源码和许可材料、131 组件 SPDX 校验通过；最新正式审查扫描 2749 个历史 blob，379 个来源标记候选，无定义的凭据形状命中。GitHub 9 个附件下载哈希一致，匿名访问和二进制下载核验通过。详细证据和范围见同目录 OPEN_SOURCE_RELEASE_TASKBOOK.md 的正式验收记录及 Release 附件；这不是对所有潜在第三方合同的法律结论。
