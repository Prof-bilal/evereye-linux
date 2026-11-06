#!/usr/bin/env bash
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
command -v flatpak-builder >/dev/null || { echo 'flatpak-builder is required' >&2; exit 1; }
command -v flatpak >/dev/null || { echo 'flatpak is required' >&2; exit 1; }
output_dir=${EVEREYE_OUTPUT_DIR:-"$project_dir/dist/flatpak"}
mkdir -p "$output_dir"
# Installs no host application. The SDK/runtime must already be present.
flatpak-builder --force-clean --state-dir="$output_dir/state" --repo="$output_dir/repo" \
    "$output_dir/build" "$project_dir/packaging/flatpak/io.github.ProfBilal.EverEye.json"
flatpak build-bundle "$output_dir/repo" "$output_dir/EverEye-$(uname -m).flatpak" io.github.ProfBilal.EverEye
