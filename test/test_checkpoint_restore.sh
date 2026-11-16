#!/usr/bin/env bash
# Copyright (C) 2026 Justin Alan Bass.
# SPDX-License-Identifier: AGPL-3.0-or-later; see LICENSE.
# No warranty; see LICENSE.

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
repo_root=$(cd -- "$script_dir/.." && pwd -P)
label="checkpoint_restore"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --label) label=${2:-}; shift 2 ;;
        --help) printf 'Usage: test_checkpoint_restore.sh [--label NAME]\n'; exit 0 ;;
        *) echo "test_checkpoint_restore: unknown flag '$1'" >&2; exit 1 ;;
    esac
done
if [[ -z $label || $label == *[!a-zA-Z0-9_.-]* ]]; then
    echo "test_checkpoint_restore: invalid --label" >&2
    exit 1
fi
makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
if [[ ! -x $makocode_bin ]]; then
    echo "test_checkpoint_restore: makocode binary not found at $makocode_bin" >&2
    exit 1
fi

work_dir=$(mktemp -d "$repo_root/test_output/${label}_checkpoint_work.XXXXXX")
mkdir -p "$work_dir/base_a" "$work_dir/base_b" \
    "$work_dir/current_a" "$work_dir/current_b" "$work_dir/pages"
python3 - "$work_dir" <<'PY'
import os
import sys

root = sys.argv[1]
for relative in ("base_a/old-a.bin", "base_b/old-b.bin"):
    path = os.path.join(root, relative)
    with open(path, "wb") as output:
        output.write(bytes(range(256)) * 20 + os.urandom(173)
                     if relative.startswith("base_a/") else os.urandom(5100))
for source_relative, destination_relative in (
    ("base_a/old-a.bin", "current_a/alpha.bin"),
    ("base_b/old-b.bin", "current_b/beta.bin"),
):
    with open(os.path.join(root, source_relative), "rb") as source:
        with open(os.path.join(root, destination_relative), "wb") as output:
            output.write(source.read())
with open(os.path.join(root, "current_a", "alpha.bin"), "rb") as source:
    alpha = source.read()
with open(os.path.join(root, "current_a", "renamed.bin"), "wb") as output:
    output.write(alpha)
with open(os.path.join(root, "current_b", "new.txt"), "wb") as output:
    output.write(b"new checkpoint content\n")
PY

(
    cd "$work_dir"
    "$makocode_bin" checkpoint create \
        --input current_a --input current_b \
        --base base_a --base base_b \
        --output update.mcp --chunk-size 256
    "$makocode_bin" checkpoint create \
        --input current_a --input current_b \
        --output full.mcp --chunk-size 256
)
delta_size=$(wc -c < "$work_dir/update.mcp" | tr -d '[:space:]')
full_size=$(wc -c < "$work_dir/full.mcp" | tr -d '[:space:]')
if (( delta_size >= full_size )); then
    echo "test_checkpoint_restore: base chunks did not reduce bundle size" >&2
    exit 1
fi

(
    cd "$work_dir"
    "$makocode_bin" encode --input update.mcp --output-dir pages --prefix checkpoint \
        --page-width 800 --page-height 800
)
shopt -s nullglob
pages=("$work_dir/pages"/*.ppm)
shopt -u nullglob
if [[ ${#pages[@]} -eq 0 ]]; then
    echo "test_checkpoint_restore: encode emitted no pages" >&2
    exit 1
fi
mkdir -p "$work_dir/decoded"
"$makocode_bin" decode --output-dir "$work_dir/decoded" "${pages[@]}" >/dev/null
"$makocode_bin" checkpoint restore \
    --input "$work_dir/decoded/update.mcp" \
    --base "$work_dir/base_a" --base "$work_dir/base_b" \
    --output "$work_dir/restored" >/dev/null
for relative in alpha.bin renamed.bin; do
    cmp --silent "$work_dir/current_a/$relative" "$work_dir/restored/current_a/$relative"
done
for relative in beta.bin new.txt; do
    cmp --silent "$work_dir/current_b/$relative" "$work_dir/restored/current_b/$relative"
done

"$makocode_bin" checkpoint restore \
    --input "$work_dir/decoded/update.mcp" \
    --base "$work_dir/base_a" \
    --output "$work_dir/partial" >/dev/null 2>&1
if [[ ! -f "$work_dir/partial/current_b/beta.bin.partial" ||
      ! -f "$work_dir/partial/checkpoint_recovery.txt" ]]; then
    echo "test_checkpoint_restore: missing base data did not produce partial output and a report" >&2
    exit 1
fi
python3 - "$work_dir/partial/current_b/beta.bin.partial" <<'PY'
import sys
with open(sys.argv[1], "rb") as source:
    if any(source.read()):
        raise SystemExit("missing chunk bytes were not represented as zero-filled ranges")
PY
cmp --silent "$work_dir/current_b/new.txt" "$work_dir/partial/current_b/new.txt"

printf '%s SUCCESS checkpoint deduplication and partial restore\n' "$label"
