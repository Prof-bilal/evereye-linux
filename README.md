# EverEye / OpenEye workspace

EverEye, the native Linux edition is in [linux/](linux/README.md). It provides timed awake sessions, lid-switch inhibition, battery cutoff, optional power-saver holds, a Qt window/tray, CLI controls, crash recovery, and an independent deadline supervisor.

Run the locally built application:

```bash
/home/abullah/Projects/OpenEye/linux/build/evereye
```

Build and test from source:

```bash
cd linux
./scripts/build.sh
./scripts/test.sh
./scripts/test-dbus.sh
```

EverEye 0.4.0 is being prepared as a public beta. The native Linux build, landing page, source archive, and browser checks are ready; the release remains explicitly beta because the exact release revision still needs a fresh distro matrix and physical desktop check, while ARM64 and universal bundles remain outside the support claim. See the [native validation record](linux/docs/VALIDATION.md) and [beta release notes](linux/docs/BETA-RELEASE.md).

| Location | Contents |
|---|---|
| [Linux README](linux/README.md) | Usage, build, CLI, recovery and packaging |
| [Architecture](linux/docs/ARCHITECTURE.md) | Process ownership, D-Bus boundary and state rules |
| [Validation](linux/docs/VALIDATION.md) | Test evidence, limitations and hardware checklist |
| `linux/dist/` | Native/source archives, checksums, and generated Arch PKGBUILD |
| [Research](research/INSOMNIA-LINUX-RESEARCH.md) | macOS architecture audit and Linux component comparison |
| [macOS reference](Insomnia/README.md) | Hash-verified upstream text snapshot |
| [Source manifest](insomnia-source-manifest.json) | Upstream commit/file provenance and acquisition limitations |

The macOS reference is a connector-retrieved text snapshot because terminal GitHub DNS access failed. It has no Git history and omits the two binary app icons. The Linux edition is implemented independently against Linux services; deferred macOS integrations are listed explicitly in its README.
