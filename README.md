# xdebug-fst

xdebug-fst 使用 Wellen 按需读取原始 FST，结合 patched Verilator 生成的 DesignDB，提供波形查询、静态关系、活动驱动及协议分析。公开接口为冻结的 `xdebug.v1`，共 73 个 Action。

首版提供 Linux x86_64 完整预编译包。目标运行平台为 EL8、Ubuntu 22.04/24.04；各候选版本的实际验收结果见随包报告。仓库当前为 private，Draft Release 不等于已经公开发布或完成全部开源授权审查。

## 预编译包

下载同一 Release 的工具包与 `SHA256SUMS`，核对 SHA256 后解压。包内有 `bin/xdebug-fst`、`bin/verilator`、必要运行库、schema、示例和许可证，不需要设置 `LD_LIBRARY_PATH`。

```bash
sha256sum --ignore-missing -c SHA256SUMS
tar -xzf xdebug-fst-0.1.0-rc.1-linux-x86_64.tar.gz
export XDEBUG_PREFIX="$PWD/xdebug-fst-0.1.0-rc.1-linux-x86_64"
"$XDEBUG_PREFIX/bin/xdebug-fst" --version --json
"$XDEBUG_PREFIX/bin/xdebug-fst" --help
```

查询已有 FST/DesignDB 不需要 Rust、GCC 或 EDA 安装。编译新 RTL 的 simulator 需要显式准备 GCC/G++ 13.3.1、make、Perl、Python 和系统 C 开发文件；GCC 编译器本体不放入运行包。版本、下载地址及 SHA256 由 `toolchains.lock.json` 定义。

## 环境准备与源码构建

环境脚本需要 Python 3.12 或更新版本作为引导解释器，以及 `rpm2cpio`、`cpio`、`readelf`。正式构建使用锁定的 Python 3.12.13；EL8 容器入口已声明引导依赖和完整 OS 包版本。

```bash
# 在源码目录执行；prefix 必须是新目录，缓存可重复使用。
python3.12 tools/prepare_environment.py \
  --prefix "$PWD/.release-env" --cache "$PWD/.release-cache" \
  --download --install
source .release-env/activate.sh

# 只在准备阶段联网；获取锁定 Git 对象并 vendor 全部 Cargo 依赖。
python3 tools/prepare_release_dependencies.py \
  --prefix "$PWD/.release-deps" --build-dir "$PWD/build-release" \
  --download --vendor
source .release-deps/activate-dependencies.sh

python3 tools/check_environment.py build
XDEBUG_RELEASE_VERSION=0.1.0-rc.1 tools/build.sh --build-dir "$PWD/build-release" --jobs 4
```

准备完成后，构建以 `--locked --offline` 运行。脚本不自动 fetch、切换编译器、覆盖现有环境、清理已有构建目录或修改 shell 启动文件。离线分发、容器构建、安装验证和打包操作见 [发布与环境指南](docs/RELEASE_GUIDE.md)。

## 第一次查询

从工具包的示例生成 FST 和 binary-v1 DesignDB。`XDEBUG_TOOLCHAIN_ROOT` 来自环境激活文件，也可显式指向已验证的 GCC 13.3.1 前缀。

```bash
mkdir demo
cd demo
"$XDEBUG_PREFIX/bin/verilator" --binary --timing --trace-fst \
  --design-db-binary --top-module top --prefix Vtop --Mdir obj_dir \
  "$XDEBUG_PREFIX/share/examples/counter/top.sv"
./obj_dir/Vtop
"$XDEBUG_PREFIX/bin/xdebug-fst" --stdio-loop --json
```

最后一个命令输出 ready 消息后，逐行输入以下请求；`waves.fst` 和 `obj_dir` 换成当前示例目录下的绝对路径：

```json
{"api_version":"xdebug.v1","action":"session.open","target":{"fsdb":"/absolute/demo/waves.fst","daidir":"/absolute/demo/obj_dir"},"args":{"name":"demo"}}
{"api_version":"xdebug.v1","action":"value.at","target":{"session_id":"demo"},"args":{"signal":"TOP.top.count","time":"10ns"}}
{"api_version":"xdebug.v1","action":"trace.driver","target":{"session_id":"demo"},"args":{"signal":"TOP.top.count"}}
{"api_version":"xdebug.v1","action":"session.close","target":{"session_id":"demo"},"args":{}}
```

`value.at` 应返回 8 位值 `00000001`。协议中的 `fsdb` 是保留的历史字段名，此工具实际只打开 FST；不会把 FSDB 转成 FST。时间使用带单位字符串。用 `actions` 查看能力列表，用 `schema` 查询各 Action 的完整合同。

## 边界与维护

- 新设计使用 `--design-db-binary`，同一次 Verilator 调用生成 simulator 与 `.xddb`/manifest。legacy `.so` 仅用于已有 bundle 的显式兼容，binary 失败不回退到 `.so`。
- 提供 one-shot、stdio-loop、UDS；MCP 由外部适配器集成，首版不附带独立 MCP 服务，也不支持 TCP/file transport。
- 大设计历史基准不能代表所有长波形；以 Release 的场景、规模和资源报告为准。
- 容器中运行会话服务时使用 `--init`，由 init 回收孤儿进程。安装目录可只读，用户工作目录和会话状态目录必须可写。
- 升级时将新版本解压到独立目录，关闭旧会话后切换路径；回退切回旧目录并重新打开会话。不要覆盖运行中的程序或设计数据库。

自有新增代码采用 [BSD-3-Clause](LICENSE)，第三方边界及未关闭事项见 [THIRD_PARTY.md](THIRD_PARTY.md)。贡献与问题反馈见 [CONTRIBUTING.md](CONTRIBUTING.md)。
