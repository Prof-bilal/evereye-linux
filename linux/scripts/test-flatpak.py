#!/usr/bin/env python3
"""Install and test a bundle in an isolated Flatpak user installation.

Requires Flatpak and internet/runtime access. Does not replace an app in the
normal user installation or delete its app data. Fixture journals use only
explicit temporary test directories, through test-only filesystem grants.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import uuid

ROOT=Path(__file__).resolve().parents[1]
APP='io.github.ProfBilal.EverEye'
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('bundle',type=Path)
p.add_argument('--fixtures',type=Path,required=True)
p.add_argument('--legacy-fixtures',type=Path,required=True)
p.add_argument('--output',type=Path,default=ROOT/'test-results/flatpak-runtime')
a=p.parse_args()
a.bundle=a.bundle.resolve(); a.output=a.output.resolve(); a.output.mkdir(parents=True,exist_ok=True)
installation=a.output/('installation-'+uuid.uuid4().hex[:10])
env=os.environ | {'FLATPAK_USER_DIR':str(installation),'QT_QPA_PLATFORM':'offscreen','QT_QPA_PLATFORMTHEME':'','QT_STYLE_OVERRIDE':'Fusion'}
steps=[]; installed=False; complete=False

def run(name,argv,timeout=600):
    with (a.output/(name+'.log')).open('w') as log:
        result=subprocess.run([str(x) for x in argv],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=timeout)
    steps.append(dict(name=name,status='passed' if result.returncode==0 else 'failed',returncode=result.returncode))
    if result.returncode: raise RuntimeError(name+' failed')

try:
    run('isolated-remote',['flatpak','--user','remote-add','--if-not-exists','flathub','https://flathub.org/repo/flathub.flatpakrepo'])
    runtime=json.loads((ROOT/'packaging/flatpak/io.github.ProfBilal.EverEye.json').read_text())
    run('runtime',['flatpak','--user','install','--assumeyes','flathub',runtime['runtime']+'//'+runtime['runtime-version']],timeout=1800)
    run('bundle-install',['flatpak','--user','install','--assumeyes',a.bundle]); installed=True
    wrapper=a.output/'evereye-flatpak-test'
    wrapper.write_text('''#!/usr/bin/env python3
import os,sys
cmd=['flatpak','--user','run','--env=QT_QPA_PLATFORM=offscreen','--env=QT_QPA_PLATFORMTHEME=','--env=QT_STYLE_OVERRIDE=Fusion']
# Keep private fixture journals away from the user's normal EverEye state.
if 'EVEREYE_PRIVATE_BUS_PARENT' in os.environ and os.environ.get('XDG_STATE_HOME'):
    state=os.environ['XDG_STATE_HOME']
    cmd+=['--filesystem='+state+':rw','--env=XDG_STATE_HOME='+state]
cmd+=['io.github.ProfBilal.EverEye',*sys.argv[1:]]
os.execvpe(cmd[0],cmd,os.environ)
''')
    wrapper.chmod(0o700)
    run('version',[wrapper,'--version'])
    with (a.output/'preview.png').open('wb') as image, (a.output/'render.log').open('w') as log:
        render=subprocess.run(['flatpak','--user','run','--env=QT_QPA_PLATFORM=offscreen','--command=sh',APP,'-c','evereye --screenshot /tmp/evereye-test-preview.png && cat /tmp/evereye-test-preview.png'],env=env,stdout=image,stderr=log,timeout=30)
    good=render.returncode==0 and (a.output/'preview.png').read_bytes().startswith(b'\x89PNG\r\n\x1a\n')
    steps.append(dict(name='runtime-render',status='passed' if good else 'failed'))
    if not good: raise RuntimeError('Flatpak preview failed')
    for api,fixture in [('current',a.fixtures),('legacy',a.legacy_fixtures)]:
        run('private-bus-'+api,[sys.executable,ROOT/'scripts/private-bus-run.py',wrapper,fixture.resolve(),'--profile-api',api],timeout=240)
    run('bundle-remove',['flatpak','--user','uninstall','--assumeyes',APP]); installed=False
    complete=True
except (OSError,subprocess.SubprocessError,RuntimeError) as error:
    steps.append(dict(name='flatpak-runtime',status='failed',error=str(error)))
finally:
    if installed:
        subprocess.run(['flatpak','--user','uninstall','--assumeyes',APP],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    (a.output/'report.json').write_text(json.dumps(dict(schema=1,complete=complete,installation=str(installation),scope='Isolated bundle install/render/current+legacy private service permissions and process lifecycle/removal. X11/Wayland, other distro userspaces and hardware remain separate gates.',steps=steps),indent=2)+'\n')
print('REPORT',a.output/'report.json')
raise SystemExit(0 if complete else 1)
