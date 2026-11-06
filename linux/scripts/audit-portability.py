#!/usr/bin/env python3
"""Audit release evidence without building, installing, or changing the host."""
import argparse
import json
from pathlib import Path
import platform
import sys

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--matrix-report', type=Path, default=ROOT/'test-results/distro-matrix-next/report.json')
parser.add_argument('--arm-report', type=Path, help='Native arm64 matrix report, if run separately')
parser.add_argument('--appimage-report', type=Path)
parser.add_argument('--flatpak-report', type=Path)
parser.add_argument('--lid-report', type=Path)
parser.add_argument('--output', type=Path, default=ROOT/'test-results/portability-audit.json')
args = parser.parse_args()

checks = []
def check(name, passed, evidence=None, reason=None):
    item = {'name': name, 'status': 'passed' if passed else 'incomplete'}
    if evidence: item['evidence'] = str(evidence)
    if reason: item['reason'] = reason
    checks.append(item)

def read_report(path, name):
    try:
        return json.loads(path.read_text()), None
    except (OSError, ValueError) as error:
        check(name, False, path, 'missing or invalid JSON: '+str(error))
        return None, str(error)

matrix, _ = read_report(args.matrix_report, 'native-amd64-matrix')
if matrix is not None:
    targets = matrix.get('targets', [])
    complete = bool(matrix.get('complete')) and bool(targets) and all(t.get('complete') for t in targets)
    check('native-amd64-matrix', complete, args.matrix_report,
          None if complete else 'every selected native target must complete build, E2E, package install, protocol and removal')

if args.arm_report:
    arm, _ = read_report(args.arm_report, 'native-arm64-matrix')
    if arm is not None:
        complete = bool(arm.get('complete')) and bool(arm.get('targets')) and all(t.get('complete') for t in arm['targets'])
        check('native-arm64-matrix', complete, args.arm_report,
              None if complete else 'every selected native arm64 target must complete')
else:
    check('native-arm64-matrix', False, reason='supply a report from a native arm64 runner')

for label, path in [('appimage-runtime', args.appimage_report), ('flatpak-runtime', args.flatpak_report)]:
    if path:
        report, _ = read_report(path, label)
        if report is not None:
            check(label, report.get('complete') is True, path, None if report.get('complete') is True else 'runtime report is incomplete')
    else:
        check(label, False, reason='runtime report was not supplied')

if args.lid_report:
    lid, _ = read_report(args.lid_report, 'physical-lid')
    if lid is not None:
        largest = lid.get('largest_execution_pause_seconds')
        version_ok = lid.get('evereye_version') in {'EverEye 0.4.0', '0.4.0'}
        digest_ok = isinstance(lid.get('binary_sha256'), str) and len(lid['binary_sha256']) == 64
        passed = lid.get('complete') is True or (version_ok and digest_ok and isinstance(largest, (int, float)) and largest < 2.0 and lid.get('interpretation') == 'No long execution pause observed')
        check('physical-lid', passed, args.lid_report, None if passed else 'report must show EverEye 0.4.0, a binary digest, and no long execution pause')
else:
    check('physical-lid', False, reason='supply a 0.4.0 host lid report')

result = {'schema': 1, 'complete': bool(checks) and all(item['status'] == 'passed' for item in checks),
          'architecture': platform.machine(), 'checks': checks}
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(result, indent=2)+'\n')
print(json.dumps(result, indent=2))
sys.exit(0 if result['complete'] else 1)
