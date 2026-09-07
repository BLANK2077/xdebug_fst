#!/usr/bin/env python3
"""Prepare locked Git repositories and Cargo vendor sources for offline builds."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--prefix', type=Path, required=True)
    p.add_argument('--build-dir', type=Path, required=True)
    p.add_argument('--download', action='store_true')
    p.add_argument('--bundle-dir', type=Path, help='Offline Git bundles named wellen.bundle and verilator.bundle')
    p.add_argument('--vendor', action='store_true', help='Vendor every locked Rust target for redistribution')
    args = p.parse_args()
    prefix = args.prefix.resolve()
    prefix.mkdir(parents=True, exist_ok=True)
    lock = json.loads((ROOT / 'dependencies.lock.json').read_text())
    env = dict(os.environ)
    for name in ('wellen', 'verilator'):
        spec = lock[name]
        repo = prefix / name
        if not repo.exists():
            subprocess.run(['git', 'init', '-q', str(repo)], check=True)
            subprocess.run(['git', '-C', str(repo), 'remote', 'add', 'origin', spec['official_url']], check=True)
        present = subprocess.run(['git', '-C', str(repo), 'cat-file', '-e', spec['revision'] + '^{commit}'], capture_output=True).returncode == 0
        if not present:
            if args.bundle_dir:
                source = str((args.bundle_dir / (name + '.bundle')).resolve())
                if not Path(source).is_file():
                    raise ValueError('Offline bundle missing: ' + source)
            elif args.download:
                source = spec['official_url']
            else:
                raise ValueError('Locked Git object missing for ' + name + '; supply --bundle-dir or explicit --download')
            subprocess.run(['git', '-C', str(repo), 'fetch', '--no-tags', source, spec['revision']], check=True)
        if subprocess.run(['git', '-C', str(repo), 'rev-parse', '--verify', 'HEAD'], capture_output=True).returncode:
            subprocess.run(['git', '-C', str(repo), 'checkout', '--detach', spec['revision']], check=True)
        env[spec['repository_env']] = str(repo)
    subprocess.run([sys.executable, str(ROOT / 'tools/prepare_dependencies.py'), '--repo-root', str(ROOT), '--build-dir', str(args.build_dir.resolve())], env=env, check=True)
    if args.vendor:
        vendor = prefix / 'vendor'
        if vendor.exists():
            raise ValueError('Vendor output exists; refusing to overwrite')
        command = ['cargo', 'vendor', '--locked', '--manifest-path', str(args.build_dir.resolve() / '_deps/wellen-src/Cargo.toml')]
        if not args.download:
            command.append('--offline')
        command.append(str(vendor))
        result = subprocess.run(command, env=env, text=True, stdout=subprocess.PIPE, check=True)
        cargo_home = prefix / 'cargo-home'
        cargo_home.mkdir(exist_ok=True)
        (cargo_home / 'config.toml').write_text(result.stdout + '\n[net]\noffline = true\n')
    lines = ['# source explicitly; never changes HOME or shell startup files']
    for name in ('wellen', 'verilator'):
        key = lock[name]['repository_env']
        lines.append('export ' + key + '=' + shlex.quote(env[key]))
    if (prefix / 'cargo-home/config.toml').exists():
        lines.append('export CARGO_HOME=' + shlex.quote(str(prefix / 'cargo-home')))
    (prefix / 'activate-dependencies.sh').write_text('\n'.join(lines) + '\n')
    return 0

if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        raise SystemExit('Dependency preparation failed: ' + str(exc))
