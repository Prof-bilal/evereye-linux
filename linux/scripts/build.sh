#!/usr/bin/env bash
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mkdir -p "$project_dir/build"
cd "$project_dir/build"
"${QMAKE:-qmake6}" ../evereye.pro
make -j"${JOBS:-2}"
./evereye --version

# Keep existing workspace commands usable during the rename.
ln -sfn evereye insomnia-linux
