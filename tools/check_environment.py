#!/usr/bin/env python3
"""Check declared tools without installing or selecting a replacement compiler."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

def version_tuple(value):
    return tuple(int(x) for x in value.split('.'))

def check(mode, lock_path=ROOT / 'toolchains.lock.json', toolchain=None):
    lock = json.loads(Path(lock_path).read_text())
    checks = []
    checks.append({'tool': 'platform', 'actual': platform.system() + '/' + platform.machine(),
                   'ok': platform.system() == 'Linux' and platform.machine() == 'x86_64'})
    libc, version = platform.libc_ver()
    checks.append({'tool': 'glibc', 'actual': version, 'expected': '>=' + lock['runtime_glibc_min'],
                   'ok': libc == 'glibc' and bool(version) and version_tuple(version) >= version_tuple(lock['runtime_glibc_min'])})
    for name, spec in lock['tools'].items():
        if mode not in spec['roles']:
            continue
        command = list(spec['command'])
        if name in ('gcc', 'g++'):
            prefix = toolchain or os.environ.get('XDEBUG_TOOLCHAIN_ROOT')
            if not prefix or not Path(prefix).is_absolute():
                checks.append({'tool': name, 'ok': False, 'error': 'Set absolute XDEBUG_TOOLCHAIN_ROOT; no system compiler fallback'})
                continue
            command[0] = str(Path(prefix) / 'bin' / name)
        elif name == 'python':
            command[0] = sys.executable
        resolved = shutil.which(command[0])
        expected = spec.get('simulate_min_version', spec['version']) if mode == 'simulate' else spec['version']
        row = {'tool': name, 'expected': expected, 'ok': False}
        try:
            if not resolved:
                raise ValueError('executable missing: ' + command[0])
            command[0] = resolved
            result = subprocess.run(command, capture_output=True, text=True, timeout=15, check=True)
            match = re.search(r'\d+\.\d+(?:\.\d+)?', result.stdout)
            if not match:
                raise ValueError('unrecognized version output')
            actual = match.group()
            exact = mode == 'build' or name in ('gcc', 'g++')
            row.update(actual=actual, path=str(Path(resolved).resolve()),
                       ok=actual == expected if exact else version_tuple(actual) >= version_tuple(expected))
        except (OSError, ValueError, subprocess.SubprocessError) as exc:
            row['error'] = str(exc)
        checks.append(row)
    return {'schema_version': 1, 'mode': mode, 'ok': all(c['ok'] for c in checks),
            'lock_sha256': hashlib.sha256(Path(lock_path).read_bytes()).hexdigest(), 'checks': checks}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['build', 'simulate', 'runtime'])
    parser.add_argument('--lock', type=Path, default=ROOT / 'toolchains.lock.json')
    parser.add_argument('--toolchain', type=Path)
    parser.add_argument('--json', action='store_true')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    report = check(args.mode, args.lock, args.toolchain)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + '\n')
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        for c in report['checks']:
            print(('OK ' if c['ok'] else 'FAIL ') + c['tool'] + ': ' + c.get('actual', c.get('error', 'missing')) + ' expected=' + c.get('expected', 'Linux/x86_64'))
    return 0 if report['ok'] else 1

if __name__ == '__main__':
    raise SystemExit(main())
