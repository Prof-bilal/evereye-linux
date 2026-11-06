#!/usr/bin/env python3
"""Exercise an actual AppImage, its bundled Qt origin and detached controller."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import uuid

ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('image',type=Path)
p.add_argument('--fixtures',type=Path,required=True)
p.add_argument('--legacy-fixtures',type=Path,required=True)
p.add_argument('--output',type=Path,default=ROOT/'test-results/appimage-runtime')
a=p.parse_args()
a.image=a.image.resolve(); a.output=a.output.resolve(); a.output.mkdir(parents=True,exist_ok=True)
env=os.environ | {'APPIMAGE_EXTRACT_AND_RUN':'1','QT_QPA_PLATFORM':'offscreen','QT_QPA_PLATFORMTHEME':'','QT_STYLE_OVERRIDE':'Fusion'}
steps=[]

def run(name,argv,cwd=None,environment=None,timeout=180):
    with (a.output/(name+'.log')).open('w') as log:
        result=subprocess.run([str(x) for x in argv],cwd=cwd,env=environment or env,stdout=log,stderr=subprocess.STDOUT,timeout=timeout)
    steps.append(dict(name=name,status='passed' if result.returncode==0 else 'failed',returncode=result.returncode))
    if result.returncode: raise RuntimeError(name+' failed')

complete=False
try:
    run('version',[a.image,'--version'])
    extraction=a.output/('extraction-'+uuid.uuid4().hex[:10]); extraction.mkdir()
    run('extract',[a.image,'--appimage-extract'],cwd=extraction)
    appdir=extraction/'squashfs-root'
    if not (appdir/'AppRun').exists(): raise RuntimeError('AppImage did not contain AppRun')
    traceenv=env | {'APPDIR':str(appdir),'APPIMAGE':str(a.image),'LD_DEBUG':'libs'}
    run('bundled-render',[appdir/'AppRun','--screenshot',a.output/'bundled.png'],environment=traceenv)
    trace=(a.output/'bundled-render.log').read_text(errors='replace')
    loaded=re.findall(r'calling init:\s+(\S*libQt6\S+)',trace)
    if not loaded or not any('libQt6Widgets' in name for name in loaded): raise RuntimeError('No inspectable Qt loader evidence')
    outside=[name for name in loaded if not Path(name).resolve().is_relative_to(appdir.resolve())]
    steps.append(dict(name='bundled-qt-origin',status='failed' if outside else 'passed',loaded=loaded,outside=outside))
    if outside: raise RuntimeError('Qt loaded outside the bundle')
    run('runtime-render',[a.image,'--screenshot',a.output/'runtime.png'])
    for api,fixture in [('current',a.fixtures),('legacy',a.legacy_fixtures)]:
        run('runtime-private-bus-'+api,[sys.executable,ROOT/'scripts/private-bus-run.py',a.image,fixture.resolve(),'--profile-api',api],timeout=180)
    complete=True
except (OSError,subprocess.SubprocessError,RuntimeError) as error:
    steps.append(dict(name='appimage-runtime',status='failed',error=str(error)))
finally:
    (a.output/'report.json').write_text(json.dumps(dict(schema=1,complete=complete,scope='Actual AppImage extraction, Qt origin, rendering and current/legacy private-bus lifetime/recovery. Clean userspace/backend/hardware matrix remains separate.',steps=steps),indent=2)+'\n')
print('REPORT',a.output/'report.json')
raise SystemExit(0 if complete else 1)
