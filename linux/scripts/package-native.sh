#!/usr/bin/env bash
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
format=${1:-}
case "$format" in DEB|RPM|TGZ) ;; *) echo 'Usage: package-native.sh DEB|RPM|TGZ' >&2; exit 2;; esac
command -v cmake >/dev/null || { echo 'cmake is required' >&2; exit 1; }
command -v cpack >/dev/null || { echo 'cpack is required' >&2; exit 1; }
output_dir=${EVEREYE_OUTPUT_DIR:-"$project_dir/dist/native-$format"}
mkdir -p "$output_dir"
cmake -S "$project_dir" -B "$output_dir/build" -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build "$output_dir/build" --parallel "${JOBS:-2}"
cd "$output_dir"
cpack --config "$output_dir/build/CPackConfig.cmake" -G "$format"
