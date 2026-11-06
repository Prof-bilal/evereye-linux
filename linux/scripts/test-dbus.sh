#!/usr/bin/env bash
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
"$project_dir/scripts/build.sh"
for api in current legacy; do
    fixture_dir="$project_dir/build-fake-services-$api"
    mkdir -p "$fixture_dir"
    cd "$fixture_dir"
    if [[ $api == legacy ]]; then
        "${QMAKE:-qmake6}" "$project_dir/tests/fake-services.pro" CONFIG+=legacy_profiles
        fixture="$fixture_dir/insomnia-fake-services-legacy"
    else
        "${QMAKE:-qmake6}" "$project_dir/tests/fake-services.pro"
        fixture="$fixture_dir/insomnia-fake-services"
    fi
    make -j"${JOBS:-2}"
    # The runner redirects the system bus only inside this private session.
    python3 "$project_dir/scripts/private-bus-run.py" "$project_dir/build/evereye" "$fixture" --profile-api "$api"
done
