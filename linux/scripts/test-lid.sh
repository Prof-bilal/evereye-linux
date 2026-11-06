#!/usr/bin/env bash
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
python3 - "$project_dir/build/evereye" "$project_dir/test-results/lid-check.json" <<'PY'
import json
import hashlib
from pathlib import Path
import subprocess
import sys
import time

binary, report = sys.argv[1:]

def command(*args):
    process = subprocess.run([binary, *args], capture_output=True, text=True, timeout=20)
    result = json.loads(process.stdout)
    if process.returncode or not result.get('ok'):
        raise SystemExit(result.get('error', 'Could not contact EverEye'))
    return result

status = command('status')
if status.get('active'):
    if not status['options'].get('lid') or status.get('remainingSeconds', 0) < 50:
        raise SystemExit('End the current session in EverEye, then run this command again.')
    print('Using your existing active session. This script will not end it.', flush=True)
else:
    status = command('start', '1m', '--lid', '--no-power-saver')
    print('Started a 1-minute session. It will end automatically.', flush=True)

print('\nClose the lid for about 20 seconds, then reopen it.\nThe result will appear here after 40 seconds.\n', flush=True)
clock = lambda: time.clock_gettime(time.CLOCK_BOOTTIME)
last = clock()
finish = last + 40
largest = 0
samples = 0
while clock() < finish:
    time.sleep(1)
    now = clock()
    largest = max(largest, now - last)
    last = now
    samples += 1
    if samples % 5 == 0:
        print(f'Checking… {max(0, int(finish - now))} seconds remaining', flush=True)

version_process = subprocess.run([binary, '--version'], capture_output=True, text=True, timeout=20)
version = version_process.stdout.strip() if version_process.returncode == 0 else 'unknown'
result = {'largest_execution_pause_seconds': round(largest, 3),
          'sample_count': samples, 'duration_seconds': 40,
          'evereye_version': version,
          'binary_sha256': hashlib.sha256(Path(binary).read_bytes()).hexdigest(),
          'interpretation': 'No long execution pause observed' if largest < 3 else 'Long pause observed; investigate suspend or scheduling',
          'physical_lid_close_requires_user_confirmation': True}
path = Path(report)
path.parent.mkdir(parents=True, exist_ok=True)
path.write_text(json.dumps(result, indent=2) + '\n')
print(f'\nLargest execution pause: {largest:.2f} seconds', flush=True)
print(result['interpretation'], flush=True)
print('Paste the Largest execution pause line into the chat.', flush=True)
print(f'Result also saved to: {path}', flush=True)
PY
