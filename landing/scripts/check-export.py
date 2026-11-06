#!/usr/bin/env python3
"""Check generated pages, local actions, assets, and source download provenance."""
from pathlib import Path
from html.parser import HTMLParser
import hashlib
import json
import tarfile

ROOT = Path(__file__).resolve().parents[1]
checks = []

class Page(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids = set()
        self.links = []
        self.h1s = 0
        self.descriptions = 0
    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if 'id' in attrs:
            self.ids.add(attrs['id'])
        if tag == 'h1': self.h1s += 1
        if tag == 'meta' and attrs.get('name') == 'description': self.descriptions += 1
        for attribute in ('href', 'src'):
            if attrs.get(attribute): self.links.append(attrs[attribute])

for route, file in [('/', 'index.html'), ('/guide', 'guide.html')]:
    page = Page()
    page.feed((ROOT / '.next/server/app' / file).read_text())
    assert page.h1s == 1, (route, 'one h1 required')
    assert page.descriptions >= 1, (route, 'description missing')
    for link in page.links:
        if link.startswith('#'):
            assert link[1:] in page.ids, (route, 'missing anchor', link)
        elif link.startswith('/_next/'):
            assert (ROOT / '.next' / link.removeprefix('/_next/').split('?')[0]).is_file(), link
        elif link.startswith('/'):
            path = link.split('#')[0]
            assert path in ('', '/', '/guide') or (ROOT / 'public' / path.lstrip('/')).is_file(), link
    checks.append({'name': 'static-page-' + route, 'passed': True, 'local_links_checked': len(page.links)})

archive = ROOT / 'public/downloads/evereye-0.4.0-source.tar.gz'
checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
assert checksum == (archive.parent / 'SHA256SUMS').read_text().split()[0]
with tarfile.open(archive) as tar:
    members = tar.getmembers()
    assert all(not member.issym() and not member.islnk() and '..' not in Path(member.name).parts for member in members)
    for source in (ROOT.parent / 'linux/src').glob('*'):
        if source.suffix in ('.cpp', '.h'):
            assert tar.extractfile('evereye-0.4.0/src/' + source.name).read() == source.read_bytes(), source
    assert 'evereye-0.4.0/scripts/build.sh' in tar.getnames()
checks.append({'name': 'source-archive-checksum-and-current-cpp', 'passed': True, 'sha256': checksum, 'members': len(members)})
print(json.dumps({'complete': True, 'scope': 'Static build, link, asset and source provenance checks only. No browser interaction claim.', 'checks': checks}, indent=2))
