#!/usr/bin/env python3
"""Prepare explicitly selected, checksum-locked tools. Network requires --download."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]

def sha256(path):
    with Path(path).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()

def fetch(spec, cache, download):
    url = spec['url']
    target = cache / url.rsplit('/', 1)[-1]
    expected = spec.get('sha256', '')
    if len(expected) != 64:
        raise ValueError('Missing locked SHA256; refusing acquisition: ' + url)
    if not target.exists():
        if not download:
            raise ValueError('Offline archive missing: ' + str(target) + '; prepare with --download explicitly')
        partial = target.with_suffix(target.suffix + '.partial')
        try:
            with urllib.request.urlopen(url, timeout=60) as response, partial.open('wb') as out:
                shutil.copyfileobj(response, out)
            if sha256(partial) != expected:
                raise ValueError('Downloaded SHA256 mismatch: ' + url)
            partial.rename(target)
        finally:
            partial.unlink(missing_ok=True)
    if sha256(target) != expected:
        raise ValueError('Cached SHA256 mismatch: ' + str(target))
    return target

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--prefix', type=Path, required=True)
    p.add_argument('--cache', type=Path, required=True)
    p.add_argument('--lock', type=Path, default=ROOT / 'toolchains.lock.json')
    p.add_argument('--download', action='store_true')
    p.add_argument('--install', action='store_true', help='Install into a new prefix; otherwise verify/prepare archives only')
    p.add_argument('--component', action='append', help='Locked archive key; repeat to select multiple')
    args = p.parse_args()
    lock = json.loads(args.lock.read_text())
    keys = args.component or list(lock['archives'])
    if any(k not in lock['archives'] for k in keys):
        p.error('Unknown component; see toolchains.lock.json archives')
    if args.install and args.prefix.exists():
        p.error('Install prefix must be new; existing environments are never overwritten')
    args.cache.mkdir(parents=True, exist_ok=True)
    archives = [(k, fetch(lock['archives'][k], args.cache, args.download)) for k in keys]
    if args.install:
        args.prefix.mkdir(parents=True)
        for key, archive in archives:
            spec = lock['archives'][key]
            if spec['kind'] == 'rust-installer':
                with tempfile.TemporaryDirectory(dir=args.cache) as temporary:
                    with tarfile.open(archive) as tar:
                        tar.extractall(temporary, filter='data')
                    installers = list(Path(temporary).glob('*/install.sh'))
                    if len(installers) != 1:
                        raise ValueError('Expected one Rust installer')
                    subprocess.run(['bash', str(installers[0]), '--prefix=' + str(args.prefix.resolve() / 'rust'), '--disable-ldconfig', '--components=rustc,cargo,rust-std-x86_64-unknown-linux-gnu'], check=True)
            elif spec['kind'] == 'rpm':
                with tempfile.TemporaryDirectory(dir=args.cache) as temporary:
                    rpm = subprocess.Popen(['rpm2cpio', str(archive.resolve())], stdout=subprocess.PIPE)
                    unpack = subprocess.run(['cpio', '-idm', '--quiet', '--no-absolute-filenames'], stdin=rpm.stdout, cwd=temporary)
                    rpm.stdout.close()
                    if rpm.wait() or unpack.returncode:
                        raise ValueError('RPM extraction failed')
                    source = Path(temporary) / 'opt/rh/gcc-toolset-13/root/usr'
                    if not source.is_dir():
                        raise ValueError('RPM does not contain the locked toolchain prefix')
                    shutil.copytree(source, args.prefix / 'gcc-13', dirs_exist_ok=True, symlinks=True)
            else:
                raise ValueError('Component is download-only: ' + key)
        prefix = args.prefix.resolve()
        lines = ['# Generated environment; source this file explicitly.']
        if (prefix / 'gcc-13').exists():
            lines += ['export XDEBUG_TOOLCHAIN_ROOT=' + shlex.quote(str(prefix / 'gcc-13'))]
        paths = [str(prefix / item / 'bin') for item in ('gcc-13', 'rust') if (prefix / item).exists()]
        lines += ['export PATH=' + shlex.quote(':'.join(paths)) + ':"$PATH"',
                  'export CARGO_HOME=' + shlex.quote(str(prefix / 'cargo-home'))]
        (prefix / 'activate.sh').write_text('\n'.join(lines) + '\n')
    print('Verified: ' + ', '.join(k for k, _ in archives))
    return 0

if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        raise SystemExit('Environment preparation failed: ' + str(exc))
