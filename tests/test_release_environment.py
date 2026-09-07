import hashlib
import json
from pathlib import Path
import subprocess

import pytest

from tools.check_environment import check
from tools.prepare_environment import fetch


def test_explicit_toolchain_does_not_use_path_compiler(tmp_path, monkeypatch):
    fake = tmp_path / 'system'
    fake.mkdir()
    cc = fake / 'gcc'
    cc.write_text('#!/bin/sh\necho 13.3.1\n')
    cc.chmod(0o755)
    monkeypatch.setenv('PATH', str(fake))
    monkeypatch.delenv('XDEBUG_TOOLCHAIN_ROOT', raising=False)
    lock = tmp_path / 'lock.json'
    lock.write_text(json.dumps({'runtime_glibc_min': '2.28', 'tools': {
        'gcc': {'version': '13.3.1', 'command': ['gcc', '-dumpfullversion'], 'roles': ['build']}}}))
    result = check('build', lock)
    assert not result['ok']
    assert 'no system compiler fallback' in result['checks'][-1]['error']


def test_wrong_explicit_gcc_version_is_rejected(tmp_path):
    (tmp_path / 'bin').mkdir()
    cc = tmp_path / 'bin/gcc'
    cc.write_text('#!/bin/sh\necho 13.3.0\n')
    cc.chmod(0o755)
    lock = tmp_path / 'lock.json'
    lock.write_text(json.dumps({'runtime_glibc_min': '2.28', 'tools': {
        'gcc': {'version': '13.3.1', 'command': ['gcc', '-dumpfullversion'], 'roles': ['build', 'simulate']}}}))
    assert not check('build', lock, tmp_path)['ok']
    assert not check('simulate', lock, tmp_path)['ok']


def test_offline_archive_missing_and_corrupt_fail_without_download(tmp_path, monkeypatch):
    import urllib.request
    def reject_network(*args, **kwargs):
        pytest.fail('offline preparation attempted network')
    monkeypatch.setattr(urllib.request, 'urlopen', reject_network)
    spec = {'url': 'https://example.invalid/archive.tar.xz', 'sha256': hashlib.sha256(b'correct').hexdigest()}
    with pytest.raises(ValueError, match='Offline archive missing'):
        fetch(spec, tmp_path, False)
    (tmp_path / 'archive.tar.xz').write_bytes(b'corrupt')
    with pytest.raises(ValueError, match='Cached SHA256 mismatch'):
        fetch(spec, tmp_path, False)
    assert (tmp_path / 'archive.tar.xz').read_bytes() == b'corrupt'


def test_cli_identity_and_usage_without_stdin(xfst_bin):
    result = subprocess.run([str(xfst_bin), '--version', '--json'], stdin=subprocess.DEVNULL,
                            capture_output=True, text=True, timeout=5, check=True)
    version = json.loads(result.stdout)
    assert version['name'] == 'xdebug-fst'
    assert len(version['git_revision']) == 40
    assert version['git_revision'] != version['compat_runtime_revision']
    for option, expected in [('--help', 0), ('--unknown-option', 2)]:
        result = subprocess.run([str(xfst_bin), option], stdin=subprocess.DEVNULL,
                                capture_output=True, timeout=5)
        assert result.returncode == expected
