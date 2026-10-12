#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
repo_root=$(cd -- "$script_dir/.." && pwd -P)
. "$script_dir/lib/colors.sh"
. "$script_dir/lib/test_seed.sh"

usage() {
    cat <<'USAGE'
Usage: run_test_suite.sh --prepare | --finalize

  --prepare    Initialize the master seed and clear visual symlinks.
  --finalize   Collect links to final visual artifacts.
USAGE
}

prepare_suite() {
    unset MAKO_TEST_SEED_REPORTED
    mako_test_seed_init "$repo_root"
    rm -rf "$repo_root/test/visual"
}

finalize_suite() {
    local visual_dir="$repo_root/test/visual"
    local visual_ppms=()
    shopt -s nullglob
    visual_ppms+=("$repo_root"/test/*_random_payload_transformed_encoded*.ppm)
    for ppm in "$repo_root"/test/*_random_payload_encoded*.ppm; do
        transformed_ppm=${ppm/_random_payload_encoded/_random_payload_transformed_encoded}
        if [[ ! -f $transformed_ppm ]]; then
            visual_ppms+=("$ppm")
        fi
    done
    visual_ppms+=("$repo_root"/test/*_cli_payload_encoded.ppm)
    visual_ppms+=("$repo_root"/test/*_archive_tree_encoded_*.ppm)
    shopt -u nullglob

    mkdir -p "$visual_dir"
    if ((${#visual_ppms[@]})); then
        for ppm in "${visual_ppms[@]}"; do
            local ppm_name
            ppm_name=$(basename "$ppm")
            ln -s "../$ppm_name" "$visual_dir/$ppm_name"
        done
    fi
}

if [[ $# -ne 1 ]]; then
    usage >&2
    exit 2
fi

case $1 in
    --prepare)
        prepare_suite
        ;;
    --finalize)
        finalize_suite
        ;;
    *)
        usage >&2
        exit 2
        ;;
esac
