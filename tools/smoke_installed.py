#!/usr/bin/env python3
"""Exercise an installed bundle without developer libraries or schema paths."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True)
    parser.add_argument('--toolchain', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    prefix, work = args.prefix.resolve(), args.work_dir.resolve()
    work.mkdir(parents=True, exist_ok=False)
    env = dict(os.environ)
    for key in ('LD_LIBRARY_PATH', 'XDEBUG_FST_DATA_ROOT', 'XVERIF_HOME', 'VERILATOR_ROOT'):
        env.pop(key, None)
    # Only children receive a disposable HOME for managed sessions.
    home = work / 'home'
    home.mkdir()
    env['HOME'] = str(home)
    env['PATH'] = str(args.toolchain.resolve() / 'bin') + ':' + env.get('PATH', '')
    env['XDEBUG_TOOLCHAIN_ROOT'] = str(args.toolchain.resolve())
    executable = str(prefix / 'bin/xdebug-fst')
    def run(command, **kwargs):
        result = subprocess.run(command, cwd=work, env=env, text=True, capture_output=True, timeout=180, **kwargs)
        if result.returncode:
            raise RuntimeError('Command failed: ' + str(command) + '\n' + result.stdout + '\n' + result.stderr)
        return result
    version = json.loads(run([executable, '--version', '--json']).stdout)
    assert 'xdebug-fst' in run([executable, '--help']).stdout
    bad = subprocess.run([executable, '--not-a-real-option'], cwd=work, env=env, capture_output=True, timeout=10)
    assert bad.returncode == 2
    run([str(prefix / 'bin/verilator'), '--binary', '--timing', '--trace-fst', '--design-db-binary', '--top-module', 'top', '--prefix', 'Vtop', '--Mdir', str(work / 'obj_dir'), str(prefix / 'share/examples/counter/top.sv')])
    run([str(work / 'obj_dir/Vtop')])
    exchanges = []
    def query(action, args=None, target=None):
        request = {'api_version': 'xdebug.v1', 'action': action, 'args': args or {}}
        if target:
            request['target'] = target
        response = json.loads(run([executable, '--json', '-'], input=json.dumps(request) + '\n').stdout)
        assert response['ok'], response
        exchanges.append({'action': action, 'response': response})
        return response
    query('actions')
    query('schema', {'action': 'value.at', 'kind': 'request'})
    target = {'fsdb': str(work / 'waves.fst'), 'daidir': str(work / 'obj_dir')}
    opened = query('session.open', {'name': 'release_smoke'}, target)
    session_id = opened['session']['session_id']
    try:
        value = query('value.at', {'signal': 'TOP.top.count', 'time': '10ns'}, {'session_id': session_id})
        assert value['data']['samples'][0]['values'][0]['value']['bits'] == '00000001'
        query('signal.resolve', {'signal': 'TOP.top.count'}, {'session_id': session_id})
        query('trace.driver', {'signal': 'TOP.top.count'}, {'session_id': session_id})
    finally:
        query('session.close', target={'session_id': session_id})
    wrong = dict(env, XDEBUG_FST_DATA_ROOT=str(work / 'missing-data'))
    rejected = subprocess.run([executable, '--json', '-'], cwd=work, env=wrong, text=True,
                              input='{"api_version":"xdebug.v1","action":"actions","args":{}}\n', capture_output=True, timeout=10)
    assert rejected.returncode != 0 or not json.loads(rejected.stdout).get('ok'), rejected.stdout
    report = {'ok': True, 'version': version, 'exchanges': exchanges}
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'ok': True, 'version': version, 'actions': [r['action'] for r in exchanges]}))
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
