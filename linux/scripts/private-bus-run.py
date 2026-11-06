#!/usr/bin/env python3
"""Only entry point for protocol fixtures: always creates a fresh session bus."""
import os
from pathlib import Path
import subprocess
import sys
root=Path(__file__).resolve().parents[1]
env=os.environ.copy()
env['EVEREYE_PRIVATE_BUS_PARENT']=env.get('DBUS_SESSION_BUS_ADDRESS','')
raise SystemExit(subprocess.call(['dbus-run-session','--',sys.executable,str(root/'tests/integration.py'),*sys.argv[1:]],env=env))
