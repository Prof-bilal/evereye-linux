# EverEye 0.4.0 public beta

EverEye 0.4.0 is a source-first public beta for Linux. It provides bounded awake sessions, optional lid handling, battery cutoffs, temporary power-profile holds, a native Qt dashboard, tray controls, and a CLI. It does not provide thermal protection, and closed-lid behavior still depends on the desktop, firmware, dock, and hardware.

## What is ready

- The native build and regression suite pass with zero failed or skipped checks.
- A previous matrix completed Ubuntu 22.04, Ubuntu 24.04, Ubuntu 26.04, Debian 12, Debian 13, Rocky 9, Arch, and Alpine/musl x86_64. The exact beta revision must be rerun after the latest metadata and package-gate fixes.
- The source archive and host-linked x86_64 archive are reproducible and have SHA-256 entries in `dist/SHA256SUMS`.
- The landing page production build and its ten desktop/mobile browser tests pass.
- The archive includes the source, tests, packaging recipes, documentation, and MIT license.

## Beta limits

- Fedora and openSUSE package/metadata reruns are pending after the latest package-gate fixes.
- Native ARM64, AppImage, Flatpak, and Nix runtime evidence is not included in the support claim.
- The physical closed-lid check must be repeated on the exact 0.4.0 binary before relying on that behavior.
- A binary archive is linked to the build machine's architecture, libc, and Qt ABI. Build from source when those differ.
- The public beta does not silently install services or change global power policy. `evereye shutdown` should be run before removing an installed copy.

## Beta verification

From a normal Linux terminal:

```bash
cd linux
./scripts/build.sh
./scripts/test.sh
./scripts/test-dbus.sh
./scripts/test-lid.sh
```

For a package or desktop report, record the distribution, desktop/compositor, architecture, Qt version, binary digest, and the result JSON under `linux/test-results/`. Report issues with the smallest reproduction, the output of `evereye doctor`, and whether a tray host is present.

After the matrix and lid checks are complete, run `python3 scripts/audit-beta.py`. It is the release gate for this public beta; the broader `audit-portability.py` remains stricter because it also requires ARM64, AppImage, and Flatpak evidence.

## Release artifacts

- `dist/evereye-0.4.0-source.tar.gz` — source-first beta archive.
- `dist/evereye-0.4.0-linux-x86_64.tar.gz` — host-linked x86_64 archive.
- `dist/PKGBUILD` — generated Arch packaging recipe.
- `dist/SHA256SUMS` — checksums for the generated artifacts.

The landing page currently distributes the source archive only. Do not advertise the host-linked archive as a universal Linux package.
