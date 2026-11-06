#!/usr/bin/env python3
"""Build, stage, and exercise EverEye without touching live power services.

A complete result requires the real private-bus and X11/Wayland gates. Missing
prerequisites are recorded as blocked, never silently treated as passes.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-system', choices=['auto', 'cmake', 'qmake'], default='auto')
parser.add_argument('--output', type=Path, default=ROOT / 'test-results' / 'e2e')
parser.add_argument('--skip-dbus', action='store_true', help='Record private-bus gates as skipped (result stays incomplete)')
parser.add_argument('--skip-desktop', action='store_true', help='Record backend gates as skipped (result stays incomplete)')
args = parser.parse_args()
OUT = args.output.resolve()
OUT.mkdir(parents=True, exist_ok=True)
BUILD = OUT / 'build'
BUILD.mkdir(exist_ok=True)
ENV = os.environ.copy()
ENV.update(QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='', QT_STYLE_OVERRIDE='Fusion')
STAGES = []

def source_fingerprint():
    digest=hashlib.sha256()
    for path in sorted(ROOT.rglob('*')):
        rel=path.relative_to(ROOT)
        if not path.is_file() or any(part in {'dist','test-results','__pycache__','build','.git','.flatpak-builder','.cache'} or part.startswith('build-') for part in rel.parts[:-1]):
            continue
        if rel.parts[0]=='docs': continue
        digest.update(str(rel).encode()+b'\0'+path.read_bytes()+b'\0')
    return digest.hexdigest()



def command(name, argv, *, cwd=ROOT, env=None, timeout=240):
    started = time.monotonic()
    log = OUT / (name + '.log')
    print('RUN', name, flush=True)
    try:
        with log.open('w') as stream:
            result = subprocess.run([str(x) for x in argv], cwd=cwd, env=env or ENV,
                                    stdout=stream, stderr=subprocess.STDOUT, timeout=timeout)
        ok = result.returncode == 0
        stage = dict(name=name, status='passed' if ok else 'failed', returncode=result.returncode)
    except (OSError, subprocess.TimeoutExpired) as error:
        stage = dict(name=name, status='blocked' if isinstance(error, FileNotFoundError) else 'failed', error=str(error))
    stage.update(log=str(log), elapsed_seconds=round(time.monotonic()-started, 2))
    STAGES.append(stage)
    print(stage['status'].upper(), name, flush=True)
    return stage['status'] == 'passed'


def blocked(name, reason, skipped=False):
    STAGES.append(dict(name=name, status='skipped' if skipped else 'blocked', error=reason))
    print(STAGES[-1]['status'].upper(), name, reason, flush=True)


def socket_available():
    try:
        with tempfile.TemporaryDirectory(prefix='evereye-socket-', dir=OUT) as temp:
            with socket.socket(socket.AF_UNIX) as connection:
                connection.bind(temp+'/probe')
        return True
    except OSError as error:
        blocked('private-socket-prerequisite', str(error))
        return False


def build():
    system = args.build_system
    if system == 'auto':
        system = 'cmake' if shutil.which('cmake') else 'qmake'
    if system == 'cmake':
        if not command('configure', ['cmake', '-S', ROOT, '-B', BUILD, '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_INSTALL_PREFIX=/usr', '-DBUILD_TESTING=ON']):
            return None
        if not command('build', ['cmake', '--build', BUILD, '--parallel', os.environ.get('JOBS', '2')]):
            return None
        stage_env = ENV | {'DESTDIR': str(OUT / 'stage')}
        installed = command('install-stage', ['cmake', '--install', BUILD], env=stage_env)
        return (BUILD / 'evereye', BUILD / 'insomnia-tests', BUILD / 'insomnia-fake-services', BUILD / 'insomnia-fake-services-legacy') if installed else None
    qmake = os.environ.get('QMAKE') or shutil.which('qmake6') or shutil.which('qmake-qt6')
    if not qmake:
        blocked('configure', 'Qt6 qmake is unavailable')
        return None
    components = [('app', ROOT/'evereye.pro', []), ('tests', ROOT/'tests/tests.pro', []),
                  ('fixtures', ROOT/'tests/fake-services.pro', []),
                  ('fixtures-legacy', ROOT/'tests/fake-services.pro', ['CONFIG+=legacy_profiles'])]
    for name, project, options in components:
        directory = BUILD / name
        directory.mkdir(exist_ok=True)
        if not command('configure-'+name, [qmake, project, *options], cwd=directory):
            return None
        if not command('build-'+name, ['make', '-j'+os.environ.get('JOBS', '2')], cwd=directory):
            return None
    if not command('install-stage', ['make', 'INSTALL_ROOT='+str(OUT/'stage'), 'install'], cwd=BUILD/'app'):
        return None
    return (BUILD/'app/evereye', BUILD/'tests/insomnia-tests', BUILD/'fixtures/insomnia-fake-services', BUILD/'fixtures-legacy/insomnia-fake-services-legacy')


def run_private(binary, fixture, api, kernel=False):
    env=ENV | {'EVEREYE_SUPERVISOR':'kernel-timer'} if kernel else ENV
    return command('private-bus-'+api+('-kernel' if kernel else ''), [sys.executable, ROOT/'scripts/private-bus-run.py', binary, fixture, '--profile-api', api], env=env, timeout=170)


def desktops(binary):
    if args.skip_desktop:
        blocked('x11', 'Explicitly skipped', True)
        blocked('wayland', 'Explicitly skipped', True)
        return
    x11_env = ENV | {'QT_QPA_PLATFORM': 'xcb'}
    if shutil.which('xvfb-run'):
        command('x11', ['xvfb-run', '-a', str(binary), '--screenshot', OUT/'x11.png'], env=x11_env, timeout=25)
    elif shutil.which('Xvfb'):
        # Some distributions ship Xvfb without the Debian-only xvfb-run
        # wrapper. Exercise the same isolated display contract directly.
        display = ':97'
        server = subprocess.Popen(['Xvfb', display, '-screen', '0', '1280x900x24', '-nolisten', 'tcp'], env=ENV, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            x11_env['DISPLAY'] = display
            time.sleep(0.5)
            command('x11', [binary, '--screenshot', OUT/'x11.png'], env=x11_env, timeout=25)
        finally:
            if server.poll() is None:
                server.terminate()
                try: server.wait(timeout=3)
                except subprocess.TimeoutExpired: server.kill(); server.wait()
    else:
        blocked('x11', 'Xvfb is required')
    if not shutil.which('weston'):
        blocked('wayland', 'weston headless compositor is required')
        return
    with tempfile.TemporaryDirectory(prefix='evereye-wayland-', dir=OUT) as runtime:
        os.chmod(runtime, 0o700)
        env = ENV | {'XDG_RUNTIME_DIR': runtime, 'WAYLAND_DISPLAY': 'evereye-test', 'QT_QPA_PLATFORM': 'wayland'}
        with (OUT/'weston.log').open('w') as log:
            compositor = subprocess.Popen(['weston', '--backend=headless-backend.so', '--socket=evereye-test', '--idle-time=0'], env=env, stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic()+10
                while not (Path(runtime)/'evereye-test').exists() and compositor.poll() is None and time.monotonic()<deadline:
                    time.sleep(0.1)
                if not (Path(runtime)/'evereye-test').exists():
                    blocked('wayland', 'Headless compositor failed to become ready; inspect weston.log')
                else:
                    command('wayland', [binary, '--screenshot', OUT/'wayland.png'], env=env, timeout=25)
            finally:
                if compositor.poll() is None:
                    compositor.terminate()
                    try:
                        compositor.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        compositor.kill()
                        compositor.wait()


def validate_install():
    stage = OUT/'stage/usr'
    required = ['bin/evereye', 'share/applications/io.github.ProfBilal.EverEye.desktop',
                'share/icons/hicolor/scalable/apps/io.github.ProfBilal.EverEye.svg',
                'share/metainfo/io.github.ProfBilal.EverEye.metainfo.xml', 'lib/systemd/user/evereye.service']
    missing = [path for path in required if not (stage/path).is_file()]
    STAGES.append(dict(name='installed-files', status='failed' if missing else 'passed', missing=missing))
    if not missing:
        command('installed-version', [stage/'bin/evereye', '--version'])
        command('installed-render', [stage/'bin/evereye', '--screenshot', OUT/'installed.png'])
    if shutil.which('desktop-file-validate'):
        command('desktop-metadata', ['desktop-file-validate', stage/required[1]])
    else:
        blocked('desktop-metadata', 'desktop-file-validate missing')
    validator = shutil.which('appstreamcli') or shutil.which('appstream-util')
    if validator:
        cmd = [validator, 'validate', '--no-net', str(stage/required[3])] if Path(validator).name == 'appstreamcli' else [validator, 'validate-relax', str(stage/required[3])]
        command('appstream-metadata', cmd)
    else:
        blocked('appstream-metadata', 'AppStream validator missing')


def main():
    source_at_start=source_fingerprint()
    executables = build()
    if executables:
        binary, tests, current, legacy = executables
        command('version', [binary, '--version'])
        command('regression', [tests, '-o', OUT/'regression.txt,txt', '-o', OUT/'regression.xml,junitxml'], cwd=OUT, timeout=180)
        command('offscreen-render', [binary, '--screenshot', OUT/'offscreen.png'])
        validate_install()
        if args.skip_dbus:
            for api in ['current', 'legacy']:
                blocked('private-bus-'+api, 'Explicitly skipped', True)
        elif socket_available():
            # Exercise the actual staged executable, not only the build-tree one.
            for api, fixture in [('current', current), ('legacy', legacy)]:
                run_private(OUT/'stage/usr/bin/evereye', fixture, api)
            run_private(OUT/'stage/usr/bin/evereye', current, 'current', kernel=True)
        else:
            for api in ['current', 'legacy']:
                blocked('private-bus-'+api, 'Private Unix sockets unavailable')
        desktops(OUT/'stage/usr/bin/evereye')
    else:
        blocked('remaining-e2e', 'Build prerequisites failed')
    source_at_end=source_fingerprint()
    if source_at_start!=source_at_end:
        blocked('source-stability','Source changed during this run; rerun against a stable tree')
    complete = bool(STAGES) and all(stage['status']=='passed' for stage in STAGES)
    report = dict(schema=1, complete=complete, scope='One userspace: build, staged install, unit/process/UI regression, current/legacy private D-Bus, X11/Wayland launch. No universal-package or physical-lid claim.',
                  source_sha256=source_at_start, source_sha256_after=source_at_end,
                  binary_sha256=hashlib.sha256(executables[0].read_bytes()).hexdigest() if executables else None,
                  architecture=os.uname().machine, kernel=os.uname().release,
                  os_release=Path('/etc/os-release').read_text() if Path('/etc/os-release').exists() else None,
                  stages=STAGES)
    (OUT/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print('REPORT', OUT/'report.json', flush=True)
    return 0 if complete else 1


if __name__ == '__main__':
    try:
        sys.exit(main())
    except Exception as error:
        blocked('runner', repr(error))
        (OUT/'report.json').write_text(json.dumps(dict(schema=1, complete=False, stages=STAGES), indent=2)+'\n')
        raise
