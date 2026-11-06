#!/usr/bin/env python3
"""Install/remove a package ONLY in a disposable matrix container."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import time
import sys
import re

p=argparse.ArgumentParser()
p.add_argument('--format',choices=['DEB','RPM','ARCH','TGZ'],required=True)
p.add_argument('--source',type=Path,required=True)
p.add_argument('--build',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
if os.environ.get('EVEREYE_ISOLATED_PACKAGE_TEST')!='1' or not Path('/.dockerenv').exists() or os.geteuid()!=0:
    raise SystemExit('Package installation tests require the explicitly marked disposable Docker container')
if Path('/usr/bin/evereye').exists():
    raise SystemExit('Refusing to replace an existing installed EverEye')
sys.path.insert(0, str(a.source / 'scripts'))
from evereye_archive import reproducible_archive
version_match = re.search(r'project\(EverEye VERSION ([0-9.]+)', (a.source/'CMakeLists.txt').read_text())
if not version_match:
    raise SystemExit('Cannot determine project version from CMakeLists.txt')
version = version_match.group(1)
a.output.mkdir(parents=True,exist_ok=True)
steps=[]

def run(name,argv,cwd=None,timeout=240):
    print('RUN',name,flush=True)
    with (a.output/(name+'.log')).open('w') as log:
        r=subprocess.run([str(x) for x in argv],cwd=cwd,stdout=log,stderr=subprocess.STDOUT,timeout=timeout)
    steps.append(dict(name=name,returncode=r.returncode,status='passed' if r.returncode==0 else 'failed'))
    print(steps[-1]['status'].upper(),name,flush=True)
    if r.returncode:
        raise RuntimeError(name+' failed; inspect its log')

installed=False
complete=False
try:
    if a.format=='ARCH':
        # makepkg refuses root; use a disposable build user inside the container.
        subprocess.run(['useradd','--create-home','packagebuilder'],check=True)
        archive=a.output/f'evereye-{version}-source.tar.gz'
        with reproducible_archive(archive) as (tar, normalize):
            for path in sorted(a.source.rglob('*')):
                rel=path.relative_to(a.source)
                if path.is_file() and not any(part in {'dist','test-results','__pycache__','build','.git','.flatpak-builder','.cache'} or part.startswith('build-') for part in rel.parts[:-1]):
                    tar.add(path,arcname=f'evereye-{version}/'+str(rel),recursive=False,filter=normalize)
        digest=hashlib.sha256(archive.read_bytes()).hexdigest()
        pkgbuild = (a.source/'packaging/arch/PKGBUILD').read_text().replace('SOURCE_ARCHIVE_SHA256',digest)
        pkgbuild = re.sub(r'^pkgver=.*$', f'pkgver={version}', pkgbuild, count=1, flags=re.MULTILINE)
        (a.output/'PKGBUILD').write_text(pkgbuild)
        subprocess.run(['chown','-R','packagebuilder:packagebuilder',str(a.output)],check=True)
        run('makepkg',['runuser','-u','packagebuilder','--','makepkg','--noconfirm'],cwd=a.output,timeout=360)
        packages=[x for x in a.output.glob('evereye-*.pkg.tar.*') if '-debug-' not in x.name]
        if len(packages)!=1: raise RuntimeError('Expected one Arch package')
        package=packages[0]
        run('install',['pacman','-U','--noconfirm',package])
        uninstall=['pacman','-R','--noconfirm','evereye']
    elif a.format in ('DEB','RPM'):
        run('cpack',['cpack','--config',a.build/'CPackConfig.cmake','-G',a.format],cwd=a.output)
        packages=list(a.output.glob('*.deb' if a.format=='DEB' else '*.rpm'))
        if len(packages)!=1: raise RuntimeError('Expected one package')
        package=packages[0]
        if a.format=='DEB':
            run('install',['dpkg','--install',package]); uninstall=['dpkg','--remove','evereye']
        else:
            # CPack creates an unsigned local RPM. Signature verification is
            # still required for repository packages; this disposable test
            # explicitly installs only the generated local artifact.
            run('install',['rpm','--upgrade','--nosignature',package]); uninstall=['rpm','--erase','evereye']
    else:
        run('cpack',['cpack','--config',a.build/'CPackConfig.cmake','-G','TGZ'],cwd=a.output)
        archives=list(a.output.glob('*.tar.gz'))
        if len(archives)!=1: raise RuntimeError('Expected one TGZ')
        extracted=a.output/'extracted'; extracted.mkdir()
        with tarfile.open(archives[0]) as tar:
            # Only consume the package generated above; reject traversal/links.
            for member in tar.getmembers():
                dest=(extracted/member.name).resolve()
                if not dest.is_relative_to(extracted.resolve()) or member.issym() or member.islnk():
                    raise RuntimeError('Unsafe native archive member')
            tar.extractall(extracted)
        matches=list(extracted.rglob('bin/evereye'))
        if len(matches)!=1: raise RuntimeError('Expected one extracted executable')
        package=archives[0]; binary=matches[0]; uninstall=None
    if a.format!='TGZ': installed=True; binary=Path('/usr/bin/evereye')
    run('installed-version',[binary,'--version'])
    run('installed-protocol',['python3',a.source/'scripts/private-bus-run.py',binary,a.build/'insomnia-fake-services'],timeout=170)
    if installed:
        run('remove',uninstall); installed=False
        if binary.exists(): raise RuntimeError('Executable remained after removal')
    complete=True
except Exception as error:
    steps.append(dict(name='package-lifecycle',status='failed',error=str(error)))
finally:
    if installed:
        subprocess.run(uninstall,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    (a.output/'report.json').write_text(json.dumps(dict(schema=1,complete=complete,format=a.format,steps=steps),indent=2)+'\n')
raise SystemExit(0 if complete else 1)
