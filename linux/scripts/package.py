#!/usr/bin/env python3
"""Create local source and native binary archives, with SHA-256 manifests."""
from pathlib import Path
import hashlib
import json
import platform
import subprocess
from evereye_archive import reproducible_archive

root = Path(__file__).resolve().parents[1]
dist = root / "dist"
dist.mkdir(exist_ok=True)
version = "0.4.0"
source_name = f"evereye-{version}"
binary_name = f"evereye-{version}-linux-{platform.machine()}"
binary = root / "build/evereye"
assert binary.is_file(), "Run scripts/build.sh first"
excluded = {"build", "build-tests", "build-fake-services", "dist", "test-results", "__pycache__", ".git", ".flatpak-builder", ".cache",
            "node_modules", ".next", "out", "playwright-report", "test-results"}
source_archive = dist / f"{source_name}-source.tar.gz"
with reproducible_archive(source_archive) as (archive, normalize):
    for path in sorted(root.rglob("*")):
        relative = path.relative_to(root)
        if not path.is_file() or any(part in excluded or part.startswith("build-") for part in relative.parts[:-1]):
            continue
        archive.add(path, arcname=f"{source_name}/{relative}", recursive=False, filter=normalize)
source_hash = hashlib.sha256(source_archive.read_bytes()).hexdigest()
(dist / "PKGBUILD").write_text((root / "packaging/arch/PKGBUILD").read_text().replace("SOURCE_ARCHIVE_SHA256", source_hash))
binary_archive = dist / f"{binary_name}.tar.gz"
with reproducible_archive(binary_archive) as (archive, normalize):
    archive.add(binary, arcname=f"{binary_name}/evereye", filter=normalize)
    for name in ["README.md", "LICENSE", "THIRD_PARTY_NOTICES.md", "assets/io.github.ProfBilal.EverEye.svg", "packaging/io.github.ProfBilal.EverEye.desktop", *[str(p.relative_to(root)) for p in sorted((root / "docs").glob("*.md"))]]:
        archive.add(root / name, arcname=f"{binary_name}/{name}", filter=normalize)
    archive.add(root / "scripts/install-user.sh", arcname=f"{binary_name}/install-user.sh", filter=normalize)
    archive.add(root / "scripts/uninstall-user.sh", arcname=f"{binary_name}/uninstall-user.sh", filter=normalize)
    web = root.parent / "mobile/out"
    if (web / "index.html").is_file():
        for path in sorted(web.rglob("*")):
            if path.is_file():
                archive.add(path, arcname=f"{binary_name}/web/{path.relative_to(web)}", filter=normalize)
manifest = []
for path in [source_archive, binary_archive, dist / "PKGBUILD"]:
    manifest.append(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}")
(dist / "SHA256SUMS").write_text("\n".join(manifest) + "\n")
(dist / "build-info.json").write_text(json.dumps({
    "version": version,
    "architecture": platform.machine(),
    "compiler": subprocess.check_output(["c++", "--version"], text=True).splitlines()[0],
    "qt": subprocess.check_output(["pkg-config", "--modversion", "Qt6Core"], text=True).strip(),
    "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
    "validation_record": "docs/VALIDATION.md",
    "private_dbus_evidence": "docs/DBUS-INTEGRATION-RESULT.md",
    "closed_lid_workload_evidence": "docs/LIVE-DESKTOP-RESULT.md",
    "broader_hardware_validation_complete": False,
    "cross_distro_validation_complete": False,
    "artifact_kind": "host-linked native archive, not a universal bundle",
    "libc": platform.libc_ver(),
    "portable_packaging_plan": "docs/PORTABILITY-PLAN.md",
}, indent=2) + "\n")
print("\n".join(manifest))
