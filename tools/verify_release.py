#!/usr/bin/env python3
"""Run release gates and retain commands, exit codes and logs in a new directory."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build = args.build_dir.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    environment = dict(os.environ, PYTEST_DISABLE_PLUGIN_AUTOLOAD='1')
    environment.pop('XDEBUG_ACTION_COVERAGE_LOG', None)
    commands = [
        ('baseline', [sys.executable, str(ROOT / 'tools/check_compat_baseline.py')]),
        ('paths', [sys.executable, str(ROOT / 'tools/check_no_local_paths.py')]),
        ('ctest', ['ctest', '--test-dir', str(build), '--output-on-failure']),
        ('pytest', [sys.executable, '-m', 'pytest', str(ROOT / 'tests'), '-q', '--xfst-bin', str(build / 'xdebug-fst'), '--junitxml=' + str(output / 'pytest.xml')]),
        ('actions', [sys.executable, str(ROOT / 'tools/audit_action_coverage.py'), '--repo-root', str(ROOT),
                     '--trace', str(output / 'actions.jsonl'), '--applicability', str(ROOT / 'tests/coverage/action_applicability.json'),
                     '--require-complete', '--output-json', str(output / 'action-coverage.json')]),
    ]
    rows = []
    for name, command in commands:
        started = time.monotonic()
        gate_environment = dict(environment)
        if name == 'pytest':
            gate_environment['XDEBUG_ACTION_COVERAGE_LOG'] = str(output / 'actions.jsonl')
        with (output / (name + '.log')).open('w') as log:
            result = subprocess.run(command, cwd=ROOT, env=gate_environment, stdout=log, stderr=subprocess.STDOUT)
        rows.append({'gate': name, 'command': command, 'exit_code': result.returncode, 'elapsed_seconds': time.monotonic() - started})
        (output / 'result.json').write_text(json.dumps({'ok': all(r['exit_code'] == 0 for r in rows), 'gates': rows}, indent=2) + '\n')
        print(name + ': ' + str(result.returncode), flush=True)
        if result.returncode:
            return result.returncode
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
