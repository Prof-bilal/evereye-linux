#!/usr/bin/env bash
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mkdir -p "$project_dir/build-tests" "$project_dir/test-results"
cd "$project_dir/build-tests"
"${QMAKE:-qmake6}" ../tests/tests.pro
make -j"${JOBS:-2}"
cd "$project_dir/test-results"
QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= QT_STYLE_OVERRIDE=Fusion \
    ../build-tests/insomnia-tests -o tests.txt,txt -o tests.xml,junitxml
cat tests.txt
if [[ ${1:-} == --dbus ]]; then
    "$project_dir/scripts/test-dbus.sh"
elif [[ $# -gt 0 ]]; then
    echo 'Usage: test.sh [--dbus]' >&2
    exit 2
fi
