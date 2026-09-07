#!/usr/bin/env bash
set -euo pipefail
# Container mount contract: checkout=/src, writable state=/env, build=/build,
# report=/reports. Preparation is online; build and verification run offline.
cd /src
case "${1:?prepare|build|verify}" in
  prepare)
    python3.12 tools/prepare_environment.py --prefix /env/tools --cache /env/archives --download --install --jobs 2
    source /env/tools/activate.sh
    python3 tools/prepare_release_dependencies.py --prefix /env/deps --build-dir /build --download --vendor
    python3 -m pip download --require-hashes -r requirements-test.lock --dest /env/wheels
    python3 -m pip install --no-index --find-links /env/wheels --require-hashes -r requirements-test.lock
    ;;
  build|verify)
    source /env/tools/activate.sh
    source /env/deps/activate-dependencies.sh
    export XDEBUG_RELEASE_VERSION=0.1.0-rc.1
    if [[ "$1" == build ]]; then
      flags=()
      case "${SANITIZER:-none}" in
        none) ;;
        asan) flags+=(--asan) ;;
        ubsan) flags+=(--ubsan) ;;
        *) echo 'Unknown sanitizer' >&2; exit 2 ;;
      esac
      bash tools/build.sh --build-dir /build --jobs 2 "${flags[@]}"
    else
      python3 tools/verify_release.py --build-dir /build --output /reports/gates
    fi
    ;;
  *) exit 2 ;;
esac
