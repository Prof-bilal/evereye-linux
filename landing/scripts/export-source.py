#!/usr/bin/env python3
"""Freeze the current product sources for the landing page's download link."""
from pathlib import Path
import hashlib
import re
import sys

LANDING = Path(__file__).resolve().parents[1]
PRODUCT = LANDING.parent / 'linux'
sys.path.insert(0, str(PRODUCT / 'scripts'))
from evereye_archive import reproducible_archive

version = re.search(r'project\(EverEye VERSION ([0-9.]+)', (PRODUCT / 'CMakeLists.txt').read_text()).group(1)
destination = LANDING / 'public/downloads'
destination.mkdir(parents=True, exist_ok=True)
archive = destination / f'evereye-{version}-source.tar.gz'
with reproducible_archive(archive) as (tar, normalize):
    for path in sorted(PRODUCT.rglob('*')):
        relative = path.relative_to(PRODUCT)
        if not path.is_file() or any(part in {'dist', 'test-results', '__pycache__', 'build', '.git', '.cache', '.flatpak-builder'} or part.startswith('build-') for part in relative.parts[:-1]):
            continue
        tar.add(path, arcname=f'evereye-{version}/{relative}', recursive=False, filter=normalize)
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
(destination / 'SHA256SUMS').write_text(f'{digest}  {archive.name}\n')
(LANDING / 'public/license.txt').write_text((PRODUCT / 'LICENSE').read_text())
print(f'{digest}  {archive.name}')
