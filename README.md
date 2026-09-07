# xdebug-fst

English | [简体中文](README.zh-CN.md)

xdebug-fst reads FST waveforms on demand through Wellen and combines them with a DesignDB produced by our patched Verilator. It provides waveform queries, static design relationships, active-driver tracing, and protocol analysis through the frozen `xdebug.v1` interface with 73 actions.

The first release provides a Linux x86_64 binary bundle targeting EL8 and Ubuntu 22.04/24.04. Download binaries, corresponding source, checksums, and validation reports from [GitHub Releases](https://github.com/BLANK2077/xdebug_fst/releases).

## Install a binary release

Download the archive and `SHA256SUMS` from the same release, verify the checksum, and extract into a new directory:

```bash
sha256sum --ignore-missing -c SHA256SUMS
tar -xzf xdebug-fst-0.1.0-linux-x86_64.tar.gz
export XDEBUG_PREFIX="$PWD/xdebug-fst-0.1.0-linux-x86_64"
"$XDEBUG_PREFIX/bin/xdebug-fst" --version --json
"$XDEBUG_PREFIX/bin/xdebug-fst" --help
```

The bundle includes `bin/xdebug-fst`, **our patched Verilator** at `bin/verilator`, its support files, required runtime libraries, schemas, examples, and licenses. It does not replace your system Verilator. No `LD_LIBRARY_PATH` setting is needed.

Querying existing FST/DesignDB files requires no Rust, GCC, or proprietary EDA installation. Building a simulator from RTL requires explicitly selected GCC/G++ 13.3.1, make, Perl, Python, and system C development files. The GCC compiler itself is not included in the runtime bundle.

## Prepare the environment and build from source

`toolchains.lock.json` records exact tool versions, download URLs, and SHA256 hashes, including Rust/Cargo 1.97.1, GCC/G++ 13.3.1, Python 3.12.13, and CMake 4.4.2. `rust-toolchain.toml` pins Rust; dependency commits and Cargo packages are also locked.

Bootstrap the environment with Python 3.12 or newer, `rpm2cpio`, `cpio`, and `readelf`. The release build uses the pinned Python 3.12.13. The EL8 container definition records bootstrap dependencies and exact OS package versions.

```bash
# Run from the source tree. Use a new prefix; the download cache is reusable.
python3.12 tools/prepare_environment.py \
  --prefix "$PWD/.release-env" --cache "$PWD/.release-cache" \
  --download --install
source .release-env/activate.sh

# Network access is explicit and limited to dependency preparation.
python3 tools/prepare_release_dependencies.py \
  --prefix "$PWD/.release-deps" --build-dir "$PWD/build-release" \
  --download --vendor
source .release-deps/activate-dependencies.sh

python3 tools/check_environment.py build
XDEBUG_RELEASE_VERSION=0.1.0 tools/build.sh --build-dir "$PWD/build-release" --jobs 4
```

After preparation, Cargo builds use `--locked --offline`. The scripts do not fetch implicitly, switch compilers, overwrite existing environments, clear build directories, or change shell startup files. See the [release and environment guide (Chinese)](docs/RELEASE_GUIDE.md) for offline preparation, containers, installation checks, and packaging.

## Run your first query

Generate an FST waveform and a binary-v1 DesignDB from the bundled counter example. The environment activation script sets `XDEBUG_TOOLCHAIN_ROOT`; you can also set it explicitly to a verified GCC 13.3.1 installation.

```bash
mkdir demo
cd demo
"$XDEBUG_PREFIX/bin/verilator" --binary --timing --trace-fst \
  --design-db-binary --top-module top --prefix Vtop --Mdir obj_dir \
  "$XDEBUG_PREFIX/share/examples/counter/top.sv"
./obj_dir/Vtop
"$XDEBUG_PREFIX/bin/xdebug-fst" --stdio-loop --json
```

After the ready message, enter one JSON request per line. Replace the example paths with absolute paths to your `waves.fst` and `obj_dir`:

```json
{"api_version":"xdebug.v1","action":"session.open","target":{"fsdb":"/absolute/demo/waves.fst","daidir":"/absolute/demo/obj_dir"},"args":{"name":"demo"}}
{"api_version":"xdebug.v1","action":"value.at","target":{"session_id":"demo"},"args":{"signal":"TOP.top.count","time":"10ns"}}
{"api_version":"xdebug.v1","action":"trace.driver","target":{"session_id":"demo"},"args":{"signal":"TOP.top.count"}}
{"api_version":"xdebug.v1","action":"session.close","target":{"session_id":"demo"},"args":{}}
```

`value.at` should return the 8-bit value `00000001`. The protocol retains the historical field name `fsdb`, but this tool opens FST files only; it does not convert FSDB. Times are strings with units. Use the `actions` action to list capabilities and `schema` to inspect their full contracts.

## Supported behavior and limitations

- For new designs, use `--design-db-binary` to generate the simulator and `.xddb`/manifest together. Legacy `.so` bundles require explicit compatibility mode; a binary database failure does not trigger a fallback.
- One-shot, stdio-loop, and Unix domain socket modes are supported. MCP requires an external adapter; this release does not include a standalone MCP server or TCP/file transport.
- Release reports state tested waveform sizes and resource use. They do not establish performance for every large design.
- Run containerized session services with `--init` to reap orphaned processes. The installation can be read-only; work and session-state directories must be writable.
- To upgrade, extract into a separate directory, close existing sessions, and switch the executable path. To roll back, select the previous installation and reopen sessions. Do not overwrite running executables or design databases.

New project code is licensed under [BSD-3-Clause](LICENSE). See [THIRD_PARTY.md](THIRD_PARTY.md) for third-party sources and distribution boundaries, and [CONTRIBUTING.md](CONTRIBUTING.md) for contributions and issue reports.
