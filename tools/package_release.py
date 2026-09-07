#!/usr/bin/env python3
"""Build a relocatable release from CMake install staging, never from an entire build tree."""
from __future__ import annotations
import argparse
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
import tomllib
import urllib.parse
from audit_release_sources import vendor_inventory
from check_no_local_paths import FORBIDDEN_CONTENT

ROOT = Path(__file__).resolve().parents[1]

def digest(path):
    with Path(path).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()

def run(command, **kw):
    return subprocess.run([str(c) for c in command], check=True, **kw)

def text(command):
    return subprocess.check_output([str(c) for c in command], text=True).strip()

def archive_tree(source, target, epoch):
    def normalize(info):
        info.uid = info.gid = 0
        info.uname = info.gname = ''
        info.mtime = epoch
        return info
    with target.open('wb') as raw, gzip.GzipFile(filename='', mode='wb', fileobj=raw, mtime=epoch) as gz:
        with tarfile.open(fileobj=gz, mode='w') as tar:
            tar.add(source, arcname=source.name, filter=normalize)

def unpack_rpm(path, directory):
    producer = subprocess.Popen(['rpm2cpio', str(path)], stdout=subprocess.PIPE)
    result = subprocess.run(['cpio', '-idm', '--quiet', '--no-absolute-filenames'], stdin=producer.stdout, cwd=directory)
    producer.stdout.close()
    if producer.wait() or result.returncode:
        raise ValueError('RPM extraction failed: ' + str(path))

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--patchelf', type=Path, required=True)
    p.add_argument('--cache', type=Path, required=True)
    p.add_argument('--vendor', type=Path, required=True)
    p.add_argument('--install-only', action='store_true', help='Create staging for tests, without final source/archives')
    args = p.parse_args()
    verified_vendor = vendor_inventory(args.vendor)
    build, output = args.build_dir.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    version = json.loads(text([build / 'xdebug-fst', '--version', '--json']))
    rev = text(['git', '-C', ROOT, 'rev-parse', 'HEAD'])
    if not args.install_only:
        dirty = text(['git', '-C', ROOT, 'status', '--porcelain', '--untracked-files=no'])
        if dirty or version['git_revision'] != rev:
            raise ValueError('Final packaging requires clean tracked files and binary matching HEAD')
    lock = json.loads((ROOT / 'toolchains.lock.json').read_text())
    dependency_lock = json.loads((ROOT / 'dependencies.lock.json').read_text())
    epoch = int(text(['git', '-C', ROOT, 'show', '-s', '--format=%ct', rev]))
    stage = output / ('xdebug-fst-' + version['version'] + '-linux-x86_64')
    run(['cmake', '--install', build, '--prefix', stage], stdout=subprocess.DEVNULL)
    license_dir = stage / 'share/licenses/xdebug-fst'
    # Runtime RPMs are selected explicitly, rather than copying arbitrary host libraries.
    rpm_evidence = []
    for key in ('libgcc', 'libstdc++', 'libatomic', 'lz4'):
        spec = lock['archives'][key]
        package = args.cache / urllib.parse.unquote(spec['url'].rsplit('/', 1)[-1])
        if digest(package) != spec['sha256']:
            raise ValueError('Runtime RPM checksum mismatch: ' + key)
        with tempfile.TemporaryDirectory(dir=output) as temporary:
            unpack_rpm(package, temporary)
            library_name = 'liblz4' if key == 'lz4' else key
            pattern = 'libgcc_s*.so*' if key == 'libgcc' else library_name + '.so.*'
            candidates = list(Path(temporary).glob('usr/lib64/' + pattern)) + list(Path(temporary).glob('lib64/' + pattern))
            if not candidates:
                raise ValueError('Runtime library absent: ' + key)
            for candidate in candidates:
                dest = stage / 'lib' / candidate.name
                if candidate.is_symlink():
                    dest.symlink_to(candidate.readlink())
                else:
                    shutil.copy2(candidate, dest)
            notices = Path(temporary) / 'usr/share/licenses'
            if notices.exists():
                shutil.copytree(notices, license_dir / key, dirs_exist_ok=True)
            documents = Path(temporary) / 'usr/share/doc'
            if documents.exists():
                for notice in documents.rglob('*'):
                    if notice.is_file() and notice.name.upper().startswith(('LICENSE', 'LICENCE', 'COPYING', 'NOTICE', 'COPYRIGHT', 'AUTHORS')):
                        destination = license_dir / key / notice.relative_to(documents)
                        destination.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copy2(notice, destination)
        rpm_evidence.append({'name': key, **spec})
    for name in ('atomic', 'lz4'):
        (stage / 'lib' / ('lib' + name + '.so')).symlink_to('lib' + name + '.so.1')
    # Verilator's configured makefile must use the user's explicitly selected GCC.
    mk = stage / 'tools/verilator/share/verilator/include/verilated.mk'
    data = mk.read_text()
    for variable in ('CXX', 'LINK'):
        data = re.sub(r'^' + variable + r' = .*$', variable + ' = $(XDEBUG_TOOLCHAIN_ROOT)/bin/g++', data, flags=re.M)
    data += '\n# Installed simulator link dependencies, explicitly shipped with this bundle.\n'
    data += 'LDFLAGS += -L$(VERILATOR_ROOT)/../../../../lib -Wl,-rpath,$(VERILATOR_ROOT)/../../../../lib\n'
    mk.write_text(data)
    pc = stage / 'tools/verilator/share/pkgconfig/verilator.pc'
    pc.write_text(re.sub(r'^prefix=.*$', 'prefix=${pcfiledir}/../..', pc.read_text(), flags=re.M))
    wrapper = stage / 'bin/verilator'
    wrapper.write_text('''#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
if [[ -d /usr/include/x86_64-linux-gnu ]]; then
    export CPLUS_INCLUDE_PATH=/usr/include/x86_64-linux-gnu${CPLUS_INCLUDE_PATH:+:$CPLUS_INCLUDE_PATH}
    export LIBRARY_PATH=/usr/lib/x86_64-linux-gnu${LIBRARY_PATH:+:$LIBRARY_PATH}
fi
for arg in "$@"; do
    if [[ "$arg" == --binary || "$arg" == --build ]]; then
        : "${XDEBUG_TOOLCHAIN_ROOT:?Set XDEBUG_TOOLCHAIN_ROOT to GCC 13.3.1 prefix}"
        [[ "$XDEBUG_TOOLCHAIN_ROOT" = /* && -x "$XDEBUG_TOOLCHAIN_ROOT/bin/g++" ]] || exit 2
        [[ "$("$XDEBUG_TOOLCHAIN_ROOT/bin/g++" -dumpfullversion)" == 13.3.1 ]] || {
            echo 'GCC 13.3.1 required; no compiler fallback' >&2; exit 2;
        }
    fi
done
exec "$root/tools/verilator/bin/verilator" "$@"
''')
    wrapper.chmod(0o755)
    shutil.copytree(ROOT / 'examples', stage / 'share/examples')
    for manifest in args.vendor.glob('*/Cargo.toml'):
        directory = manifest.parent
        destination = license_dir / 'rust' / directory.name
        for notice in directory.rglob('*'):
            if notice.is_file() and notice.name.upper().startswith(('LICENSE', 'LICENCE', 'COPYING', 'NOTICE', 'COPYRIGHT', 'AUTHORS')):
                target = destination / notice.relative_to(directory)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(notice, target)
    shutil.copytree(build / '_deps/verilator-src/LICENSES', license_dir / 'verilator/LICENSES')
    shutil.copy2(build / '_deps/verilator-src/LICENSE', license_dir / 'verilator/LICENSE')
    shutil.copy2(ROOT / 'toolchains.lock.json', stage / 'share/xdebug-fst/toolchains.lock.json')
    helpers = stage / 'share/xdebug-fst/tools'
    helpers.mkdir()
    for name in ('check_environment.py', 'smoke_installed.py'):
        shutil.copy2(ROOT / 'tools' / name, helpers / name)
    for file in ('README.md', 'README.zh-CN.md', 'docs/RELEASE_GUIDE.md', 'LICENSE', 'THIRD_PARTY.md', 'CONTRIBUTING.md'):
        if (ROOT / file).exists():
            destination = stage / file
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / file, destination)
    elf_inventory = []
    for source in sorted(stage.rglob('*')):
        if not source.is_file() or source.is_symlink():
            continue
        with source.open('rb') as f:
            elf = f.read(4) == b'\x7fELF'
        if not elf:
            continue
        run(['strip', '--strip-debug', source])
        dynamic = text(['readelf', '-d', source])
        if '(NEEDED)' in dynamic:
            relative = os.path.relpath(stage / 'lib', source.parent)
            run([args.patchelf, '--set-rpath', '$ORIGIN/' + relative, source])
            required = re.findall(r'Name: GLIBC_(\d+(?:\.\d+)+)', text(['readelf', '--version-info', source]))
            maximum = max((tuple(map(int, v.split('.'))) for v in required), default=(0,))
            if maximum > tuple(map(int, lock['runtime_glibc_min'].split('.'))):
                raise ValueError('ELF requires newer glibc than the declared baseline: ' + str(source.relative_to(stage)))
            elf_inventory.append({'path': str(source.relative_to(stage)), 'glibc_required_max': '.'.join(map(str, maximum)),
                                  'rpath': text([args.patchelf, '--print-rpath', source])})
    for source in sorted(stage.rglob('*')):
        if source.is_symlink():
            if not source.resolve().is_relative_to(stage):
                raise ValueError('Installed symlink escapes package: ' + str(source.relative_to(stage)))
        elif source.is_file():
            content = source.read_bytes()
            if any(pattern.search(content) for pattern, _ in FORBIDDEN_CONTENT):
                raise ValueError('Installed file contains developer-home path: ' + str(source.relative_to(stage)))
    (stage / 'share/xdebug-fst/elf-inventory.json').write_text(json.dumps(elf_inventory, indent=2) + '\n')
    environment = json.loads((build / 'build-environment.json').read_text())
    # Published provenance contains versions and executable identities, never developer prefixes.
    for check in environment['checks']:
        if 'path' in check:
            check['executable'] = Path(check.pop('path')).name
    environment.update(version=version, runtime_packages=rpm_evidence,
                       dependency_lock_sha256=digest(ROOT / 'dependencies.lock.json'),
                       source_date_epoch=epoch,
                       build_os_packages_lock_sha256=digest(ROOT / 'environment/el8-packages.lock'),
                       build_recipe_sha256=digest(ROOT / 'tools/build.sh'),
                       verification_scope='See separate release acceptance reports; packaging is not an approval')
    (stage / 'share/xdebug-fst/build-environment.json').write_text(json.dumps(environment, indent=2) + '\n')
    if args.install_only:
        print(stage)
        return 0
    archive_tree(stage, output / (stage.name + '.tar.gz'), epoch)
    with tempfile.TemporaryDirectory(dir=output) as temporary:
        work = Path(temporary)
        source = work / ('xdebug-fst-' + version['version'] + '-source')
        source.mkdir()
        raw = subprocess.check_output(['git', '-C', str(ROOT), 'archive', rev])
        with tarfile.open(fileobj=io.BytesIO(raw)) as tar:
            tar.extractall(source, filter='data')
        (source / 'SOURCE_REVISION').write_text(rev + '\n')
        archive_tree(source, output / (source.name + '.tar.gz'), epoch)
        thirdparty = work / ('xdebug-fst-' + version['version'] + '-third-party-source')
        thirdparty.mkdir()
        for name in ('wellen', 'verilator'):
            spec = dependency_lock[name]
            repo = os.environ.get(spec['repository_env'])
            if not repo:
                raise ValueError('Source packaging requires ' + spec['repository_env'])
            upstream = thirdparty / name
            upstream.mkdir()
            raw = subprocess.check_output(['git', '-C', repo, 'archive', spec['revision']])
            with tarfile.open(fileobj=io.BytesIO(raw)) as tar:
                tar.extractall(upstream, filter='data')
            # Git bundles allow offline preparation using the exact original object identities.
            bundle_repo = work / (name + '-git')
            run(['git', 'init', '-q', bundle_repo])
            run(['git', '-C', bundle_repo, 'fetch', '--no-tags', str(Path(repo).resolve()), spec['revision']], stdout=subprocess.DEVNULL)
            run(['git', '-C', bundle_repo, 'update-ref', 'refs/heads/locked', 'FETCH_HEAD'])
            run(['git', '-C', bundle_repo, 'bundle', 'create', thirdparty / (name + '.bundle'), 'refs/heads/locked'])
        shutil.copytree(ROOT / 'patches', thirdparty / 'patches')
        shutil.copytree(ROOT / 'wellen_capi', thirdparty / 'wellen_capi', ignore=shutil.ignore_patterns('target'))
        shutil.copytree(ROOT / 'wellenx_capi', thirdparty / 'wellenx_capi', ignore=shutil.ignore_patterns('target'))
        shutil.copytree(args.vendor, thirdparty / 'vendor')
        shutil.copy2(ROOT / 'rust/Cargo.lock', thirdparty / 'Cargo.lock')
        for key in ('gcc13-source', 'gcc8-source', 'lz4-source'):
            spec = lock['archives'][key]
            rpm = args.cache / urllib.parse.unquote(spec['url'].rsplit('/', 1)[-1])
            if digest(rpm) != spec['sha256']:
                raise ValueError('Corresponding source checksum mismatch: ' + key)
            shutil.copy2(rpm, thirdparty / rpm.name)
        archive_tree(thirdparty, output / (thirdparty.name + '.tar.gz'), epoch)
    shutil.copy2(stage / 'share/xdebug-fst/build-environment.json', output / 'build-environment.json')
    # SPDX inventory includes the entire vendored workspace, including non-runtime targets.
    packages = []
    for info in verified_vendor:
        packages.append({'name': info['name'], 'SPDXID': 'SPDXRef-crate-' + re.sub(r'[^A-Za-z0-9.-]', '-', info['name'] + '-' + info['version']),
                         'versionInfo': info['version'], 'downloadLocation': 'https://crates.io/crates/' + info['name'] + '/' + info['version'],
                         'licenseDeclared': info['license'], 'licenseConcluded': 'NOASSERTION',
                         'checksums': [{'algorithm': 'SHA256', 'checksumValue': info['checksum']}],
                         'copyrightText': 'NOASSERTION', 'filesAnalyzed': False,
                         'comment': 'Locked source distribution inventory; includes build, test and non-Linux targets.'})
    components = [
        ('main', 'xdebug-fst', version['version'], 'BSD-3-Clause', 'https://github.com/BLANK2077/xdebug_fst'),
        ('wellen', 'Wellen', dependency_lock['wellen']['revision'], 'BSD-3-Clause', dependency_lock['wellen']['official_url']),
        ('verilator', 'Verilator', dependency_lock['verilator']['revision'], 'LGPL-3.0-only OR Artistic-2.0', dependency_lock['verilator']['official_url']),
        ('json', 'nlohmann-json', '3.11.2', 'MIT', 'https://github.com/nlohmann/json'),
        ('json-validator', 'json-schema-validator', 'NOASSERTION', 'MIT', 'https://github.com/pboettch/json-schema-validator'),
        ('schema', 'xdebug-v1-frozen-baseline', version['schema_revision'], 'MIT', 'NOASSERTION'),
    ]
    for key in ('libgcc', 'libstdc++', 'libatomic', 'lz4', 'gcc13-source'):
        spec = lock['archives'][key]
        components.append((key.replace('+', 'p'), key, spec['version'], 'BSD-2-Clause' if key == 'lz4' else 'GPL-3.0-or-later WITH GCC-exception-3.1' if key != 'gcc13-source' else 'GPL-3.0-or-later', spec['url']))
    for ident, name, component_version, license_id, url in components:
        packages.append({'name': name, 'SPDXID': 'SPDXRef-' + ident, 'versionInfo': component_version,
                         'downloadLocation': url, 'licenseDeclared': license_id, 'licenseConcluded': 'NOASSERTION',
                         'copyrightText': 'NOASSERTION', 'filesAnalyzed': False})
    sbom = {'spdxVersion': 'SPDX-2.3', 'dataLicense': 'CC0-1.0', 'SPDXID': 'SPDXRef-DOCUMENT',
            'name': 'xdebug-fst-' + version['version'],
            'documentNamespace': 'https://github.com/BLANK2077/xdebug_fst/sbom/' + rev,
            'creationInfo': {'creators': ['Tool: xdebug-fst-package-release'], 'created': __import__('datetime').datetime.fromtimestamp(epoch, __import__('datetime').timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')},
            'packages': packages,
            'relationships': [{'spdxElementId': 'SPDXRef-DOCUMENT', 'relationshipType': 'DESCRIBES', 'relatedSpdxElement': 'SPDXRef-main'}] +
                             [{'spdxElementId': 'SPDXRef-main', 'relationshipType': 'OTHER', 'relatedSpdxElement': item['SPDXID'],
                               'comment': 'Component included in binary or corresponding-source distribution; not an assertion of static linkage.'}
                              for item in packages if item['SPDXID'] != 'SPDXRef-main']}

    (output / 'sbom.spdx.json').write_text(json.dumps(sbom, indent=2) + '\n')
    (output / 'SHA256SUMS').write_text(''.join(digest(f) + '  ' + f.name + '\n' for f in sorted(output.iterdir()) if f.is_file() and f.name != 'SHA256SUMS'))
    print(output)
    return 0

if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        raise SystemExit('Release packaging failed: ' + str(exc))
