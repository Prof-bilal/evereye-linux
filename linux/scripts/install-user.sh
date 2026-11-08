#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
if [[ -f "$script_dir/packaging/io.github.ProfBilal.EverEye.desktop" ]]; then
  project_dir="$script_dir"
else
  project_dir=$(cd -- "$script_dir/.." && pwd)
fi
binary=${EVEREYE_BINARY:-$project_dir/build/evereye}
if [[ ! -x "$binary" && -x "$project_dir/evereye" ]]; then
  binary="$project_dir/evereye"
fi
prefix=${EVEREYE_PREFIX:-"$HOME/.local"}
bin_dir="$prefix/bin"
app_dir="$prefix/share/applications"
icon_dir="$prefix/share/icons/hicolor/scalable/apps"

if [[ ! -x "$binary" ]]; then
  echo "EverEye binary not found or not executable: $binary" >&2
  echo "Build it first with: $project_dir/scripts/build.sh" >&2
  exit 1
fi

install -Dm755 "$binary" "$bin_dir/evereye"
install -Dm644 "$project_dir/assets/io.github.ProfBilal.EverEye.svg" \
  "$icon_dir/io.github.ProfBilal.EverEye.svg"
desktop_tmp=$(mktemp)
trap 'rm -f "$desktop_tmp"' EXIT
sed -e "s|^Exec=.*|Exec=$bin_dir/evereye|" \
  -e "s|^TryExec=.*|TryExec=$bin_dir/evereye|" \
  "$project_dir/packaging/io.github.ProfBilal.EverEye.desktop" > "$desktop_tmp"
install -Dm644 "$desktop_tmp" "$app_dir/io.github.ProfBilal.EverEye.desktop"

if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database "$app_dir" >/dev/null 2>&1 || true
fi

echo "EverEye installed for this user. Open it from your Applications menu."
echo "Binary: $bin_dir/evereye"
