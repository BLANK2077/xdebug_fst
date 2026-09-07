#!/usr/bin/env python3
"""Deterministic long/high-change-count FST gate; no tracked fixture is regenerated."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import subprocess
import time

PROFILES = [('time-250k', 250_000, 1), ('time-1m', 1_000_000, 1), ('loaded-32', 250_000, 32)]

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--prefix', type=Path, required=True)
    p.add_argument('--toolchain', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--timeout', type=int, default=60, help='Per public request wall budget in seconds')
    p.add_argument('--max-rss-mib', type=int, default=1024)
    args = p.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    prefix = args.prefix.resolve()
    environment = dict(os.environ)
    for key in ('LD_LIBRARY_PATH', 'XDEBUG_FST_DATA_ROOT', 'VERILATOR_ROOT', 'XVERIF_HOME'):
        environment.pop(key, None)
    environment['XDEBUG_TOOLCHAIN_ROOT'] = str(args.toolchain.resolve())
    environment['PATH'] = str(args.toolchain.resolve() / 'bin') + ':' + environment['PATH']
    rows = []
    for name, ticks, signals in PROFILES:
        work = output / name
        work.mkdir()
        home = work / 'home'
        home.mkdir()
        environment['HOME'] = str(home)
        rtl = ['`timescale 1ns/1ns', 'module top;']
        rtl += ['logic [31:0] count%d = 0;' % i for i in range(signals)]
        rtl += ['initial begin', '$dumpfile("waves.fst"); $dumpvars(0, top);',
                'for (int step = 0; step < %d; step++) begin #1;' % ticks]
        rtl += ['count%d = count%d + 32\'d%d;' % (i, i, i + 1) for i in range(signals)]
        rtl += ['end', '#1; $finish; end', 'endmodule']
        (work / 'top.sv').write_text('\n'.join(rtl) + '\n')
        command = [str(prefix / 'bin/verilator'), '--binary', '--timing', '--trace-fst', '--design-db-binary',
                   '--top-module', 'top', '--prefix', 'Vtop', '--Mdir', str(work / 'obj_dir'), str(work / 'top.sv')]
        with (work / 'build.log').open('w') as log:
            subprocess.run(command, cwd=work, env=environment, stdout=log, stderr=subprocess.STDOUT, timeout=180, check=True)
        with (work / 'simulation.log').open('w') as log:
            subprocess.run([str(work / 'obj_dir/Vtop')], cwd=work, env=environment, stdout=log, stderr=subprocess.STDOUT, timeout=180, check=True)
        exchanges = []
        def query(action, values=None, target=None, limits=None):
            req = {'api_version': 'xdebug.v1', 'action': action, 'args': values or {}}
            if target:
                req['target'] = target
            if limits:
                req['limits'] = limits
            started = time.monotonic()
            result = subprocess.run([str(prefix / 'bin/xdebug-fst'), '--json', '-'], input=json.dumps(req) + '\n',
                                    cwd=work, env=environment, text=True, capture_output=True, timeout=args.timeout)
            response = json.loads(result.stdout)
            if result.returncode or not response.get('ok'):
                raise RuntimeError(response)
            exchanges.append({'action': action, 'elapsed_ms': (time.monotonic() - started) * 1000})
            return response
        opened = query('session.open', {'name': 'lw'}, {'fsdb': str(work / 'waves.fst'), 'daidir': str(work / 'obj_dir')})
        session = {'session_id': opened['session']['session_id']}
        try:
            pid = opened['session']['server_pid']
            for i in range(signals):
                result = query('value.at', {'signal': 'TOP.top.count%d' % i, 'time': str(ticks) + 'ns'}, session)
                actual = result['data']['samples'][0]['values'][0]['value']['bits']
                assert int(actual, 2) == (ticks * (i + 1)) & 0xffffffff, (name, i, actual)
            # Read a narrow tail window after loading all selected signals.
            changes = query('signal.changes', {'signal': 'TOP.top.count0', 'time_range': {'begin': str(ticks - 10) + 'ns', 'end': str(ticks) + 'ns'}, 'mode': 'timeline'}, session)
            assert changes['summary']['actual_transition_count'] == 10, changes
            assert changes['summary']['returned_count'] == 11, changes
            assert changes['summary']['analysis_complete'] and not changes['summary']['response_truncated'], changes
            status = Path('/proc/%s/status' % pid).read_text()
            fields = {line.split(':')[0]: line.split(':', 1)[1].strip() for line in status.splitlines() if ':' in line}
            rss_kib = int(fields['VmHWM'].split()[0])
            assert rss_kib <= args.max_rss_mib * 1024, (name, rss_kib)
            rows.append({'profile': name, 'ticks': ticks, 'loaded_signals': signals,
                         'fst_bytes': (work / 'waves.fst').stat().st_size, 'peak_rss_kib': rss_kib,
                         'request_budget_seconds': args.timeout, 'rss_budget_mib': args.max_rss_mib,
                         'exchanges': exchanges, 'tail_changes_summary': changes['summary']})
        finally:
            query('session.close', target=session)
    report = {'ok': True, 'boundary': 'deterministic counters; not a GB-scale or arbitrary SoC capacity claim', 'profiles': rows}
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
