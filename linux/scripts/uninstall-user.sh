#!/usr/bin/env bash
set -euo pipefail

prefix=${EVEREYE_PREFIX:-"$HOME/.local"}
rm -f "$prefix/bin/evereye" \
  "$prefix/share/applications/io.github.ProfBilal.EverEye.desktop" \
  "$prefix/share/icons/hicolor/scalable/apps/io.github.ProfBilal.EverEye.svg"

if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database "$prefix/share/applications" >/dev/null 2>&1 || true
fi

echo "EverEye user installation removed."
