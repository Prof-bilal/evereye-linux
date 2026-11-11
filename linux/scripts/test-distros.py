#!/usr/bin/env python3
"""Run actual builds/E2E/package lifecycles in disposable distro containers.

Requires a normal terminal with a working Docker daemon and internet access.
No host package is installed. Reports remain incomplete for any failed target.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import time
import uuid

ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--targets',help='Comma-separated target IDs (default: all native-architecture targets)')
p.add_argument('--output',type=Path,default=ROOT/'test-results/distro-matrix')
p.add_argument('--resume',action='store_true',help='Reuse completed targets in an existing output directory and continue incomplete targets')
a=p.parse_args()
a.output=a.output.resolve(); a.output.mkdir(parents=True,exist_ok=True)
matrix=json.loads((ROOT/'ci/distro-matrix.json').read_text())
def source_fingerprint():
    digest=hashlib.sha256()
    for path in sorted(ROOT.rglob('*')):
        rel=path.relative_to(ROOT)
        if not path.is_file() or any(part in {'dist','test-results','__pycache__','build','.git','.flatpak-builder','.cache'} or part.startswith('build-') for part in rel.parts[:-1]):
            continue
        digest.update(str(rel).encode()+b'\0'+path.read_bytes()+b'\0')
    return digest.hexdigest()
source_sha256=source_fingerprint()
architecture={'x86_64':'linux/amd64','aarch64':'linux/arm64'}.get(platform.machine())
if not architecture: raise SystemExit('No runner architecture configured for '+platform.machine())
targets=[t for t in matrix['targets'] if architecture in t['architectures']]
if a.targets:
    selected=set(a.targets.split(','))
    if selected-set(t['id'] for t in targets): raise SystemExit('Unknown or unsupported target for this native architecture')
    targets=[t for t in targets if t['id'] in selected]
results=[]
if a.resume and (a.output/'report.json').is_file():
    try:
        prior=json.loads((a.output/'report.json').read_text())
        if prior.get('source_sha256') != source_sha256:
            print('RESUME: source fingerprint changed; no prior target is reusable',flush=True)
        else:
            for item in prior.get('targets',[]):
                if item.get('complete') and item.get('target') in {t['id'] for t in targets} and item.get('architecture') == architecture:
                    results.append(item)
        completed={item['target'] for item in results}
        targets=[t for t in targets if t['id'] not in completed]
        print('RESUME: retaining completed targets: '+(', '.join(sorted(completed)) if completed else 'none'),flush=True)
    except (OSError,ValueError,TypeError):
        print('RESUME: existing report is unreadable; starting selected targets afresh',flush=True)
if not targets:
    print('All selected targets are already complete',flush=True)
    raise SystemExit(0)
# Freeze inputs once so concurrent development cannot change a running matrix.
snapshot=a.output/('source-'+uuid.uuid4().hex[:10])
def snapshot_ignore(directory,names):
    ignored=[]
    for name in names:
        path=Path(directory)/name
        if name in {'build','dist','test-results','__pycache__','.git','.flatpak-builder','.cache','result'} or name.startswith('build-') or path.resolve()==a.output:
            ignored.append(name)
    return ignored
shutil.copytree(ROOT,snapshot,ignore=snapshot_ignore,symlinks=True)
try:
    subprocess.run(['docker','info'],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,timeout=20)
except (OSError,subprocess.SubprocessError) as error:
    result=dict(schema=1,complete=False,architecture=architecture,source_sha256=source_sha256,blocker='Docker unavailable: '+str(error),targets=[])
    (a.output/'report.json').write_text(json.dumps(result,indent=2)+'\n')
    raise SystemExit('Docker unavailable. Report: '+str(a.output/'report.json'))
for target in targets:
    directory=a.output/target['id']; directory.mkdir(exist_ok=True)
    name='evereye-test-'+uuid.uuid4().hex[:12]
    image='evereye-matrix:'+target['id']+'-'+architecture.split('/')[-1]
    item=dict(target=target['id'],image_reference=target['image'],architecture=architecture,complete=False)
    results.append(item)
    print('TARGET',target['id'],flush=True)
    try:
        print('  Building dependency image; log: '+str(directory/'image-build.log'),flush=True)
        with (directory/'image-build.log').open('w') as log:
            # BuildKit's embedded resolver intermittently fails to resolve distro
            # archive names while the host resolves them fine; the dependency
            # build is a CI-only container, so borrow the host's resolver.
            result=subprocess.run(['docker','build','--network=host','--platform',architecture,'-t',image,'--build-arg','BASE_IMAGE='+target['image'],'-f',str(ROOT/'ci/containers'/('Dockerfile.'+target['recipe'])),str(ROOT/'ci/containers')],stdout=log,stderr=subprocess.STDOUT,timeout=1800)
        if result.returncode: raise RuntimeError('Container dependency build failed')
        item['image_id']=subprocess.check_output(['docker','image','inspect','--format','{{.Id}}',image],text=True).strip()
        subprocess.run(['docker','run','--detach','--name',name,'--platform',architecture,'--mount','type=bind,src='+str(snapshot)+',dst=/src,readonly',image,'python3','-c','import time; time.sleep(86400)'],check=True,stdout=subprocess.DEVNULL)
        print('  Running E2E; log: '+str(directory/'e2e.log'),flush=True)
        with (directory/'e2e.log').open('w') as log:
            result=subprocess.run(['docker','exec',name,'python3','/src/scripts/test-e2e.py','--build-system','cmake','--output','/results/e2e'],stdout=log,stderr=subprocess.STDOUT,timeout=900)
        item['e2e_returncode']=result.returncode
        # Make compositor diagnostics and the E2E report available before the
        # package's additional real-time expiry test has finished.
        subprocess.run(['docker','cp',name+':/results/.',str(directory)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        # Native installation/removal happens only inside this marked container.
        print('  Testing package lifecycle; log: '+str(directory/'package.log'),flush=True)
        with (directory/'package.log').open('w') as log:
            package=subprocess.run(['docker','exec','-e','EVEREYE_ISOLATED_PACKAGE_TEST=1',name,'python3','/src/ci/test-native-package.py','--format',target['native_package'],'--source','/src','--build','/results/e2e/build','--output','/results/package'],stdout=log,stderr=subprocess.STDOUT,timeout=900)
        item['package_returncode']=package.returncode
        copied=subprocess.run(['docker','cp',name+':/results/.',str(directory)],stdout=subprocess.DEVNULL).returncode==0
        item['complete']=result.returncode==0 and package.returncode==0 and copied
    except (OSError,subprocess.SubprocessError,RuntimeError) as error:
        item['error']=str(error)
        subprocess.run(['docker','cp',name+':/results/.',str(directory)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    finally:
        subprocess.run(['docker','rm','--force',name],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        item['finished_at_utc']=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())
        (a.output/'report.json').write_text(json.dumps(dict(schema=1,complete=all(t['complete'] for t in results) and len(results)==len(targets),scope='Native architecture distro userspace and package lifecycle; universal formats, other architecture and hardware are separate gates',architecture=architecture,source_sha256=source_sha256,targets=results),indent=2)+'\n')
        print('PASS' if item['complete'] else 'INCOMPLETE',target['id'],flush=True)
print('REPORT',a.output/'report.json')
raise SystemExit(0 if results and all(t['complete'] for t in results) else 1)
