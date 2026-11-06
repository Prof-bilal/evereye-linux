#!/usr/bin/env bash
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
: "${LINUXDEPLOY:?Set LINUXDEPLOY to a verified linuxdeploy executable}"
: "${LINUXDEPLOY_PLUGIN_QT:?Set LINUXDEPLOY_PLUGIN_QT to a verified Qt plugin executable}"
command -v cmake >/dev/null || { echo 'cmake is required' >&2; exit 1; }
LINUXDEPLOY=$(realpath -- "$LINUXDEPLOY")
LINUXDEPLOY_PLUGIN_QT=$(realpath -- "$LINUXDEPLOY_PLUGIN_QT")
[[ -x "$LINUXDEPLOY" && -x "$LINUXDEPLOY_PLUGIN_QT" ]] || { echo 'Both deployment tools must be executable files' >&2; exit 1; }
# Do not silently turn a current Arch binary into an allegedly portable release.
python3 - <<'PY'
import os, platform
kind, version = platform.libc_ver()
if kind != 'glibc' or not version or tuple(map(int, version.split('.')[:2])) > (2,35):
    raise SystemExit('Build portable AppImages on glibc <= 2.35 (for example Ubuntu 22.04). This host is not that baseline.')
PY
output_dir=${EVEREYE_OUTPUT_DIR:-"$project_dir/dist/appimage"}
mkdir -p "$output_dir"
output_dir=$(cd -- "$output_dir" && pwd)
cmake -S "$project_dir" -B "$output_dir/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TESTING=OFF -DEVEREYE_INSTALL_USER_SERVICE=OFF
cmake --build "$output_dir/build" --parallel "${JOBS:-2}"
DESTDIR="$output_dir/AppDir" cmake --install "$output_dir/build"
cp "$LINUXDEPLOY_PLUGIN_QT" "$output_dir/linuxdeploy-plugin-qt-$(uname -m).AppImage"
chmod +x "$output_dir/linuxdeploy-plugin-qt-$(uname -m).AppImage"
# linuxdeploy searches beside its executable and in PATH, not in the working directory.
export PATH="$output_dir:$PATH"
export QMAKE=${QMAKE:-qmake6}
export EXTRA_PLATFORM_PLUGINS="libqwayland-egl.so;libqwayland-generic.so;libqoffscreen.so"
export EXTRA_QT_MODULES="waylandclient;waylandcompositor"
export APPIMAGE_EXTRACT_AND_RUN=1
export OUTPUT="$output_dir/EverEye-$(uname -m).AppImage"
cd "$output_dir"
"$LINUXDEPLOY" --appdir "$output_dir/AppDir" --plugin qt --output appimage
# This also provides a FUSE-independent distribution option with the same payload.
tar -C "$output_dir" -czf "$output_dir/EverEye-$(uname -m)-AppDir.tar.gz" AppDir
