#!/usr/bin/env python3
"""Verify Cargo vendor contents and inventory historical source-review candidates.

Reports contain identifiers and paths, never matching secret text. A successful
technical scan does not grant rights or clear the public-release approval gate.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tomllib

ROOT = Path(__file__).resolve().parents[1]

def vendor_inventory(vendor):
    locked = {(p['name'], p['version']): p['checksum'] for p in tomllib.loads((ROOT / 'rust/Cargo.lock').read_text())['package'] if 'checksum' in p}
    rows = []
    for manifest in sorted(vendor.glob('*/Cargo.toml')):
        info = tomllib.loads(manifest.read_text())['package']
        key = (info['name'], info['version'])
        checksums = json.loads((manifest.parent / '.cargo-checksum.json').read_text())
        if key not in locked or checksums['package'] != locked[key]:
            raise ValueError('Cargo package checksum mismatch: ' + str(key))
        for filename, expected in checksums['files'].items():
            path = manifest.parent / filename
            if not path.resolve().is_relative_to(manifest.parent.resolve()):
                raise ValueError('Cargo vendor path escape')
            if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
                raise ValueError('Cargo file checksum mismatch: ' + str(path))
        notices = [str(p.relative_to(manifest.parent)) for p in manifest.parent.rglob('*') if p.is_file() and p.name.upper().startswith(('LICENSE', 'LICENCE', 'COPYING', 'NOTICE', 'COPYRIGHT', 'AUTHORS'))]
        if not info.get('license') or not notices:
            raise ValueError('Missing declared license/notice: ' + str(key))
        rows.append({'name': key[0], 'version': key[1], 'checksum': locked[key], 'license': info['license'].replace('/', ' OR '), 'notices': sorted(notices), 'files_verified': len(checksums['files'])})
    if {(r['name'], r['version']) for r in rows} != set(locked):
        raise ValueError('Vendor inventory does not cover the exact Cargo lock')
    return rows

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--vendor', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--history', action='store_true')
    args = p.parse_args()
    report = {'technical_checks_ok': True, 'public_release_approved': False, 'rust': vendor_inventory(args.vendor)}
    if args.history:
        objects = subprocess.check_output(['git', 'rev-list', '--objects', '--all'], cwd=ROOT, text=True).splitlines()
        names = {line.split(' ', 1)[0]: line.split(' ', 1)[1] for line in objects if ' ' in line}
        proc = subprocess.Popen(['git', 'cat-file', '--batch'], cwd=ROOT, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        findings = []
        patterns = {'vendor-or-confidential-marker': re.compile(rb'synopsys|xamba|\bsvt_|confidential|proprietary', re.I),
                    'credential-shape': re.compile(rb'-----BEGIN (?:RSA |OPENSSH |EC )?PRIVATE KEY-----|gh[pousr]_[A-Za-z0-9]{30,}|AKIA[0-9A-Z]{16}')}
        blobs = 0
        for oid, name in names.items():
            proc.stdin.write((oid + '\n').encode()); proc.stdin.flush()
            header = proc.stdout.readline().decode().split()
            data = proc.stdout.read(int(header[2])); proc.stdout.read(1)
            if header[1] != 'blob':
                continue
            blobs += 1
            labels = [label for label, pattern in patterns.items() if pattern.search(data)]
            if labels:
                findings.append({'object': oid, 'path': name, 'review_reasons': labels})
        proc.stdin.close()
        if proc.wait():
            raise ValueError('Git history scan failed')
        report['history'] = {'blobs_scanned': blobs, 'findings': findings, 'scope': 'All locally reachable refs; pattern screening only, not contract or authorship proof'}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print('Verified %d Cargo packages; public approval remains pending' % len(report['rust']))

if __name__ == '__main__':
    main()
