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
import urllib.parse
import zipfile

ROOT = Path(__file__).resolve().parents[1]

def sha256(path):
    with Path(path).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()

def fetch(spec, cache, download):
    url = spec['url']
    target = cache / urllib.parse.unquote(url.rsplit('/', 1)[-1])
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
    p.add_argument('--jobs', type=int, default=4)
    p.add_argument('--component', action='append', help='Locked archive key; repeat to select multiple')
    args = p.parse_args()
    lock = json.loads(args.lock.read_text())
    kinds = ('bootstrap-tool-rpm', 'rpm', 'toolchain-runtime-rpm', 'patchelf-wheel', 'rust-installer', 'cmake-wheel', 'python-source')
    keys = args.component or [k for k, s in lock['archives'].items() if s['kind'] in kinds]
    if any(k not in lock['archives'] for k in keys):
        p.error('Unknown component; see toolchains.lock.json archives')
    keys.sort(key=lambda k: kinds.index(lock['archives'][k]['kind']) if lock['archives'][k]['kind'] in kinds else len(kinds))
    if args.jobs < 1:
        p.error('--jobs must be positive')
    if args.install and args.prefix.exists():
        p.error('Install prefix must be new; existing environments are never overwritten')
    args.cache.mkdir(parents=True, exist_ok=True)
    archives = [(k, fetch(lock['archives'][k], args.cache, args.download)) for k in keys]
    if args.install:
        args.prefix.mkdir(parents=True)
        for key, archive in archives:
            spec = lock['archives'][key]
            if spec['kind'] == 'bootstrap-tool-rpm':
                with tempfile.TemporaryDirectory(dir=args.cache) as temporary:
                    rpm = subprocess.Popen(['rpm2cpio', str(archive.resolve())], stdout=subprocess.PIPE)
                    unpack = subprocess.run(['cpio', '-idm', '--quiet', '--no-absolute-filenames'], stdin=rpm.stdout, cwd=temporary)
                    rpm.stdout.close()
                    if rpm.wait() or unpack.returncode:
                        raise ValueError('Bootstrap tool RPM extraction failed')
                    directory = args.prefix / 'release-tools/bin'
                    directory.mkdir(parents=True, exist_ok=True)
                    for subdir in ('bin', 'usr/bin'):
                        source = Path(temporary) / subdir
                        if source.exists():
                            shutil.copytree(source, directory, dirs_exist_ok=True, symlinks=True)
            elif spec['kind'] == 'rust-installer':
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
            elif spec['kind'] == 'patchelf-wheel':
                directory = args.prefix / 'release-tools/bin'
                directory.mkdir(parents=True, exist_ok=True)
                with zipfile.ZipFile(archive) as wheel:
                    scripts = [n for n in wheel.namelist() if n.endswith('/scripts/patchelf')]
                    if len(scripts) != 1:
                        raise ValueError('Expected one patchelf executable')
                    (directory / 'patchelf').write_bytes(wheel.read(scripts[0]))
                    (directory / 'patchelf').chmod(0o755)
            elif spec['kind'] == 'toolchain-runtime-rpm':
                with tempfile.TemporaryDirectory(dir=args.cache) as temporary:
                    rpm = subprocess.Popen(['rpm2cpio', str(archive.resolve())], stdout=subprocess.PIPE)
                    unpack = subprocess.run(['cpio', '-idm', '--quiet', '--no-absolute-filenames'], stdin=rpm.stdout, cwd=temporary)
                    rpm.stdout.close()
                    if rpm.wait() or unpack.returncode:
                        raise ValueError('Compiler runtime RPM extraction failed')
                    library = args.prefix / 'gcc-13/lib64'
                    library.mkdir(parents=True, exist_ok=True)
                    for subdir in ('lib64', 'usr/lib64'):
                        directory = Path(temporary) / subdir
                        if directory.exists():
                            shutil.copytree(directory, library, dirs_exist_ok=True, symlinks=True)
            elif spec['kind'] == 'cmake-wheel':
                directory = args.prefix / 'cmake'
                directory.mkdir()
                with zipfile.ZipFile(archive) as wheel:
                    wheel.extractall(directory)
                for executable in (directory / 'cmake/data/bin').iterdir():
                    executable.chmod(0o755)
            elif spec['kind'] == 'python-source':
                with tempfile.TemporaryDirectory(dir=args.cache) as temporary:
                    with tarfile.open(archive) as tar:
                        tar.extractall(temporary, filter='data')
                    source = next(Path(temporary).glob('Python-*'))
                    environment = dict(__import__('os').environ)
                    cc = args.prefix.resolve() / 'gcc-13/bin/gcc'
                    if not cc.is_file():
                        raise ValueError('Python source build requires selected gcc component')
                    environment['CC'] = str(cc)
                    environment['LD_LIBRARY_PATH'] = str(args.prefix.resolve() / 'gcc-13/lib64')
                    try:
                        subprocess.run(['./configure', '--prefix=' + str(args.prefix.resolve() / 'python'), '--with-ensurepip=install'], cwd=source, env=environment, check=True)
                    except subprocess.CalledProcessError:
                        logs = args.prefix / 'logs'
                        logs.mkdir(exist_ok=True)
                        if (source / 'config.log').exists():
                            shutil.copy2(source / 'config.log', logs / 'python-config.log')
                        raise
                    subprocess.run(['make', '-j' + str(args.jobs)], cwd=source, env=environment, check=True)
                    subprocess.run(['make', 'install'], cwd=source, env=environment, check=True)
            else:
                raise ValueError('Component is download-only: ' + key)
        prefix = args.prefix.resolve()
        compiler = prefix / 'gcc-13'
        patcher = prefix / 'release-tools/bin/patchelf'
        if compiler.exists() and (compiler / 'lib64/libmpfr.so.4').exists():
            if not patcher.is_file():
                raise ValueError('Relocatable compiler runtime requires the patchelf component')
            for executable in compiler.rglob('*'):
                if not executable.is_file() or executable.is_symlink():
                    continue
                with executable.open('rb') as stream:
                    elf = stream.read(4) == b'\x7fELF'
                if elf:
                    dynamic = subprocess.check_output(['readelf', '-d', str(executable)], text=True)
                    if '(NEEDED)' in dynamic:
                        relative = __import__('os').path.relpath(compiler / 'lib64', executable.parent)
                        subprocess.run([str(patcher), '--set-rpath', '$ORIGIN/' + relative, str(executable)], check=True)
            for script in compiler.glob('lib/gcc/*/*/libstdc++.so'):
                script.write_text(script.read_text().replace('/usr/lib64/libstdc++.so.6', 'libstdc++.so.6'))
            for script in compiler.glob('lib/gcc/*/*/libgcc_s.so'):
                script.write_text(script.read_text().replace('/lib64/libgcc_s.so.1', 'libgcc_s.so.1'))
            for script in compiler.glob('lib/gcc/*/*/libatomic.so'):
                script.write_text(script.read_text().replace('/usr/lib64/libatomic.so.1', 'libatomic.so.1'))
        lines = ['# Generated environment; source this file explicitly.']
        if (prefix / 'gcc-13').exists():
            lines += ['export XDEBUG_TOOLCHAIN_ROOT=' + shlex.quote(str(prefix / 'gcc-13'))]
        paths = [str(prefix / item / 'bin') for item in ('gcc-13', 'rust', 'release-tools') if (prefix / item).exists()]
        if (prefix / 'python').exists():
            paths.insert(0, str(prefix / 'python/bin'))
            lines += ['export XDEBUG_PYTHON=' + shlex.quote(str(prefix / 'python/bin/python3'))]
        if (prefix / 'cmake').exists():
            paths.insert(0, str(prefix / 'cmake/cmake/data/bin'))
        lines += ['export PATH=' + shlex.quote(':'.join(paths)) + ':"$PATH"',
                  'export CARGO_HOME=' + shlex.quote(str(prefix / 'cargo-home'))]
        lines += ['if [ -d /usr/include/x86_64-linux-gnu ]; then',
                  '  export CPLUS_INCLUDE_PATH=/usr/include/x86_64-linux-gnu${CPLUS_INCLUDE_PATH:+:$CPLUS_INCLUDE_PATH}',
                  '  export C_INCLUDE_PATH=/usr/include/x86_64-linux-gnu${C_INCLUDE_PATH:+:$C_INCLUDE_PATH}',
                  '  export LIBRARY_PATH=/usr/lib/x86_64-linux-gnu${LIBRARY_PATH:+:$LIBRARY_PATH}', 'fi']
        (prefix / 'activate.sh').write_text('\n'.join(lines) + '\n')
    print('Verified: ' + ', '.join(k for k, _ in archives))
    return 0

if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        raise SystemExit('Environment preparation failed: ' + str(exc))
