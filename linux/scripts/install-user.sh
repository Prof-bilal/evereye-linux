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
desktop_tmp=$(mktemp)
gateway_tmp=$(mktemp)
trap 'rm -f "$desktop_tmp" "$gateway_tmp"' EXIT

if [[ ! -x "$binary" ]]; then
  echo "EverEye binary not found or not executable: $binary" >&2
  echo "Build it first with: $project_dir/scripts/build.sh" >&2
  exit 1
fi

install -Dm755 "$binary" "$bin_dir/evereye"
install -Dm644 "$project_dir/assets/io.github.ProfBilal.EverEye.svg" \
  "$icon_dir/io.github.ProfBilal.EverEye.svg"

# Ship the mobile PWA when it has been built, so `evereye gateway` serves it
# from the same origin. Release archives place it at <archive>/web.
web_source=""
for candidate in "$project_dir/web" "$project_dir/../mobile/out"; do
  if [[ -f "$candidate/index.html" ]]; then web_source="$candidate"; break; fi
done
if [[ -n "$web_source" ]]; then
  install -d "$prefix/share/evereye/web"
  cp -R "$web_source/." "$prefix/share/evereye/web/"
  echo "Web controls: $prefix/share/evereye/web"
fi

if [[ "${EVEREYE_INSTALL_GATEWAY:-0}" == "1" ]]; then
  unit_dir="$prefix/lib/systemd/user"
  sed -e "s|^ExecStart=.*|ExecStart=$bin_dir/evereye gateway|" \
    "$project_dir/packaging/systemd/evereye-gateway.service" > "$gateway_tmp"
  install -Dm644 "$gateway_tmp" "$unit_dir/evereye-gateway.service"
  echo "Gateway unit installed. Enable it explicitly: systemctl --user enable --now evereye-gateway"
fi
sed -e "s|^Exec=.*|Exec=$bin_dir/evereye|" \
  -e "s|^TryExec=.*|TryExec=$bin_dir/evereye|" \
  "$project_dir/packaging/io.github.ProfBilal.EverEye.desktop" > "$desktop_tmp"
install -Dm644 "$desktop_tmp" "$app_dir/io.github.ProfBilal.EverEye.desktop"

if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database "$app_dir" >/dev/null 2>&1 || true
fi

echo "EverEye installed for this user. Open it from your Applications menu."
echo "Binary: $bin_dir/evereye"
