# 发布、工具链与离线环境

## 权威清单

| 文件 | 职责 |
| --- | --- |
| `toolchains.lock.json` | GCC、Rust、Python、CMake、打包工具及辅助工具版本；公开下载地址与 SHA256；平台要求 |
| `rust-toolchain.toml` | Rust 1.97.1、minimal profile、x86_64 Linux target |
| `dependencies.lock.json` | Wellen/Verilator revision、tree、patch 哈希和 Cargo lock 身份 |
| `rust/Cargo.lock` | Rust 全部锁定依赖及 registry checksum |
| `requirements-test.lock` | CPython 3.12/Linux x86_64 测试 wheel 版本及哈希 |
| `environment/el8-packages.lock` | EL8 引导系统完整 RPM NEVRA 清单 |

GCC/G++ 固定 13.3.1，Rust/Cargo 固定 1.97.1，正式构建 Python 固定 3.12.13，CMake 固定 4.4.2。源码构建以锁定 EL8 环境验收；Ubuntu 是预编译包和 simulator 使用平台，不声称任意发行版都能匹配 EL8 构建环境。

`check_environment.py build` 要求精确版本；`simulate` 要求 GCC/G++ 精确版本，其他前置工具按已验证最低版本检查；`runtime` 检查 Linux x86_64 和 glibc >= 2.28。它不会安装任何软件。

## 环境脚本

`prepare_environment.py` 默认校验缓存，只有 `--download` 才联网；`--install` 才写入指定新前缀。可重复指定 `--component`，只准备 simulator 编译器时选择：

```bash
python3.12 tools/prepare_environment.py --prefix /absolute/compiler \
  --cache /absolute/download-cache --download --install \
  --component gcc --component g++ --component gcc-headers \
  --component gcc-atomic --component binutils --component gcc-mpfr \
  --component gcc-jansson --component patchelf
source /absolute/compiler/activate.sh
```

工具链只安装到前缀内。MPFR/Jansson 是选定 GCC/binutils 的必要依赖；脚本设置相对 RPATH，并修正发行版 linker script 中的固定系统库位置。Ubuntu 使用明确的 multiarch 头文件和库目录。不会将旧版本 `.so` 伪装为其他 ABI，也不会改动系统库。

EL8 安装系统 C 开发文件、zlib/LZ4 开发依赖、make、Perl、Python；Ubuntu 安装 `build-essential python3 perl zlib1g-dev liblz4-dev libatomic1`。引导阶段操作见对应 Containerfile，不在环境脚本内执行隐式 sudo。

安装失败会保留前缀和日志；不要把部分完成的目录当作有效环境。修正明确缺失项后使用新前缀，下载缓存继续复用。Python configure 失败日志保存在前缀的 `logs/python-config.log`。

## 离线构建

联网机器执行 README 的准备步骤，并下载测试 wheel：

```bash
python3 -m pip download --require-hashes -r requirements-test.lock --dest /absolute/wheels
```

工具链归档保留在显式缓存目录。依赖目录中的 Git 对象和 Cargo vendor 可转移；源码 Release 的第三方对应源码包额外包含 `wellen.bundle`、`verilator.bundle` 和 vendor 源码。转移后不要沿用旧机器生成的绝对路径激活文件，重新生成：

```bash
python3 tools/prepare_release_dependencies.py --prefix /absolute/deps \
  --build-dir /absolute/build --bundle-dir /absolute/third-party-source
```

在新的 `CARGO_HOME/config.toml` 明确设置离线 vendor 的实际路径：

```toml
[source.crates-io]
replace-with = "vendored-sources"
[source.vendored-sources]
directory = "/absolute/third-party-source/vendor"
[net]
offline = true
```

使用新环境激活文件和依赖激活文件，显式设置 `CARGO_HOME` 后执行 `tools/build.sh`。缺 Git 对象、crate、归档或校验不一致均应失败，不允许退回网络或其他工具版本。

## 容器

```bash
podman build -f tools/Containerfile.el8 -t xdebug-release-build .
```

Containerfile 固定基础镜像 digest、官方仓库地址和完整 RPM 集合，安装后逐项核对。若宿主代理只监听回环地址，显式使用 `podman build --network=host ...`；不要把宿主的回环代理地址留给隔离网络的容器。

将源码、下载缓存和独立输出目录显式挂载，在容器内运行相同环境准备、依赖准备、build 和验收脚本。准备完成后的验证容器使用 `--network=none --init`。不得把整个工作区作为待上传镜像内容；`.containerignore`/`.dockerignore` 已限制构建上下文。

## 测试与打包

```bash
python3 -m pip install --no-index --find-links /absolute/wheels \
  --require-hashes -r requirements-test.lock
PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python3 -m pytest
ctest --test-dir build-release --output-on-failure
```

关闭自动加载是为了使公共回归不受机器上其他项目 pytest 插件影响。它不屏蔽本项目的测试或失败。

```bash
python3 tools/package_release.py --build-dir build-release \
  --output /absolute/new-release-directory --patchelf /absolute/patchelf \
  --cache /absolute/download-cache --vendor /absolute/vendor
```

最终打包要求 tracked 工作树干净且二进制 revision 等于 HEAD。打包从 CMake install staging 创建，完整构建目录、私人缓存与日志不进入产物。`--install-only` 仅供安装测试，不生成正式源码附件。

包中附带 `share/xdebug-fst/tools/smoke_installed.py`。指定安装前缀、新工作目录和 GCC 前缀，可验证版本、RTL 编译、FST、binary DesignDB、会话查询、正确计数值及关闭；错误数据目录必须失败。

ASan、UBSan 使用独立构建目录。现有 fixture 不变时不重新生成；首次清洁构建可以编译 fixture reader 所需测试库，但不覆盖 tracked fixture。

## 发布和审批

RC 只上传到当前 private 仓库的 Draft Release，标签显式绑定验收提交；不自动公开仓库、创建 PR 或发布正式版本。附件包括工具包、自有源码、第三方对应源码、SHA256、SPDX、环境记录及验收摘要。上传后重新下载核对 SHA256。

许可证扫描、公开来源审查和实际合同授权分别记录。未知来源/义务必须保留为公开发布阻断，不能用技术测试通过替代授权。未运行的平台、测试或性能场景不得出现在“通过”列表中。
