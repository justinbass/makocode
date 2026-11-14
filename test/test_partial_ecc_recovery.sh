#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
repo_root=$(cd -- "$script_dir/.." && pwd -P)
harness_bin=${PARTIAL_ECC_RECOVERY_HARNESS_BIN:-"$repo_root/test/partial_ecc_recovery_harness"}
if [[ ! -x $harness_bin ]]; then
    echo "test_partial_ecc_recovery: harness not found at $harness_bin" >&2
    exit 1
fi
"$harness_bin"
