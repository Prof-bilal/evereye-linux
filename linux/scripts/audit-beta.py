#!/usr/bin/env python3
"""Audit the evidence required for the EverEye 0.4.0 public beta."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--matrix-report', type=Path, default=ROOT/'test-results/distro-matrix-beta/report.json')
parser.add_argument('--lid-report', type=Path, default=ROOT/'test-results/lid-check.json')
parser.add_argument('--landing-results', type=Path, default=ROOT.parent/'landing/test-results/.last-run.json')
parser.add_argument('--output', type=Path, default=ROOT/'test-results/beta-audit.json')
args = parser.parse_args()

required_targets = {
    'ubuntu-22.04', 'ubuntu-24.04', 'ubuntu-26.04', 'debian-12', 'debian-13',
    'fedora', 'rocky-9', 'arch', 'opensuse-leap', 'alpine-musl',
}
checks = []

def source_fingerprint():
    digest = hashlib.sha256()
    excluded = {'dist', 'test-results', '__pycache__', 'build', '.git', '.flatpak-builder', '.cache'}
    for path in sorted(ROOT.rglob('*')):
        relative = path.relative_to(ROOT)
        if not path.is_file() or any(part in excluded or part.startswith('build-') for part in relative.parts[:-1]):
            continue
        digest.update(str(relative).encode() + b'\0' + path.read_bytes() + b'\0')
    return digest.hexdigest()

def add(name, passed, evidence=None, reason=None):
    item = {'name': name, 'status': 'passed' if passed else 'incomplete'}
    if evidence:
        item['evidence'] = str(evidence)
    if reason:
        item['reason'] = reason
    checks.append(item)

try:
    matrix = json.loads(args.matrix_report.read_text())
    targets = {item.get('target'): item for item in matrix.get('targets', [])}
    current_hash = source_fingerprint()
    complete = (matrix.get('complete') is True and matrix.get('source_sha256') == current_hash and
                required_targets <= set(targets) and all(targets[name].get('complete') for name in required_targets))
    add('native-x86_64-matrix', complete, args.matrix_report,
        None if complete else 'all ten supported x86_64 targets must complete on the exact current source revision')
except (OSError, ValueError) as error:
    add('native-x86_64-matrix', False, args.matrix_report, f'missing or invalid report: {error}')

tests = ROOT/'test-results/tests.txt'
try:
    text = tests.read_text()
    passed = re.search(r'Totals:\s+(\d+) passed, 0 failed, 0 skipped', text)
    add('native-regression', bool(passed and int(passed.group(1)) >= 74), tests,
        None if passed and int(passed.group(1)) >= 74 else 'expected at least 74 passed native checks')
except OSError as error:
    add('native-regression', False, tests, f'missing test log: {error}')

try:
    lid = json.loads(args.lid_report.read_text())
    valid = (lid.get('evereye_version') in {'EverEye 0.4.0', '0.4.0'} and
             isinstance(lid.get('binary_sha256'), str) and len(lid['binary_sha256']) == 64 and
             isinstance(lid.get('largest_execution_pause_seconds'), (int, float)) and
             lid['largest_execution_pause_seconds'] < 2.0 and
             lid.get('interpretation') == 'No long execution pause observed')
    add('physical-lid-0.4.0', valid, args.lid_report,
        None if valid else 'expected an EverEye 0.4.0 report with digest and no long execution pause')
except (OSError, ValueError) as error:
    add('physical-lid-0.4.0', False, args.lid_report, f'missing or invalid report: {error}')

try:
    landing = json.loads(args.landing_results.read_text())
    valid = landing.get('status') == 'passed' and not landing.get('failedTests')
    add('landing-e2e', valid, args.landing_results, None if valid else 'landing browser tests must pass')
except (OSError, ValueError) as error:
    add('landing-e2e', False, args.landing_results, f'missing or invalid result: {error}')

source = ROOT/'dist/evereye-0.4.0-source.tar.gz'
binary = ROOT/'dist/evereye-0.4.0-linux-x86_64.tar.gz'
manifest = ROOT/'dist/SHA256SUMS'
artifacts_ok = source.is_file() and binary.is_file() and manifest.is_file()
if artifacts_ok:
    expected = {line.split()[1]: line.split()[0] for line in manifest.read_text().splitlines() if len(line.split()) == 2}
    artifacts_ok = all(expected.get(path.name) == hashlib.sha256(path.read_bytes()).hexdigest() for path in (source, binary))
add('release-artifacts', artifacts_ok, manifest, None if artifacts_ok else 'source and x86_64 archives must exist and match SHA256SUMS')

result = {'schema': 1, 'complete': bool(checks) and all(item['status'] == 'passed' for item in checks), 'checks': checks}
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
sys.exit(0 if result['complete'] else 1)
