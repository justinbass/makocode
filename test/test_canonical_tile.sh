#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname "$0")/.." && pwd -P)
bin=${MAKOCODE_BIN:-"$root/makocode"}
work=$(mktemp -d "$root/test_output/canonical_tile_work.XXXXXX")
harness=${CANONICAL_TILE_HARNESS_BIN:-"$root/test/canonical_tile_harness"}
if [[ ! -x $harness ]]; then
    harness="$work/canonical_tile_harness"
    "${CXX:-c++}" -std=c++17 -Wall -Wextra -pedantic -O2 \
        "$root/test/test_canonical_tile_harness.cpp" -o "$harness"
fi
python3 - "$work/payload.bin" <<'PY'
import random, sys
source = random.Random(42)
with open(sys.argv[1], 'wb') as stream:
    stream.write(bytes(source.randrange(256) for _ in range(65536)))
PY
printf 'small canonical roundtrip\n' > "$work/tiny.bin"
for dimensions in '1000 1000 1 payload.bin'; do
    read -r width height expect_canonical input_name <<<"$dimensions"
    label="${width}x${height}"
    mkdir -p "$work/$label/pages"
    (cd "$work" && "$bin" encode --input="$input_name" --ecc=0 \
        --page-width="$width" --page-height="$height" \
        --prefix=page \
        --output-dir="$work/$label/pages" >/dev/null)
    page=$(find "$work/$label/pages" -maxdepth 1 -type f -name '*.ppm' -print -quit)
    page_count=$(find "$work/$label/pages" -maxdepth 1 -type f -name '*.ppm' -print | wc -l | tr -d '[:space:]')
    if [[ $page_count != 1 || -z $page ]]; then
        echo "canonical tile: expected one page for $label, found $page_count" >&2
        exit 1
    fi
    "$harness" "$page" "$width" "$height" "$expect_canonical"
    mkdir -p "$work/$label/out"
    "$bin" decode --output-dir="$work/$label/out" "$page" >/dev/null
    cmp "$work/$input_name" "$work/$label/out/$input_name"
done

# Confirm the centered tile preserves the requested unknown-error budget
# after adding structural erasure and overlay reserves to the actual parity.
mkdir -p "$work/ecc_budget/pages"
(cd "$work" && "$bin" encode --input=payload.bin --ecc=0.25 \
    --page-width=1200 --page-height=1200 \
    --prefix=budget --output-dir="$work/ecc_budget/pages" >/dev/null)
shopt -s nullglob
budget_pages=("$work"/ecc_budget/pages/*.ppm)
shopt -u nullglob
if [[ ${#budget_pages[@]} -ne 1 ]]; then
    echo "canonical tile: ECC budget case expected one page, found ${#budget_pages[@]}" >&2
    exit 1
fi
"$harness" "${budget_pages[0]}" 1200 1200 1 --check-ecc-budget 25
"$harness" "${budget_pages[0]}" 1200 1200 1 --check-reserve-reject

# A page encoded without overlay reserve must remain visibly distinguishable
# from one carrying deterministic overlay erasures.
mkdir -p "$work/no_overlay_reserve/pages"
(cd "$work" && "$bin" encode --input=payload.bin --ecc=0.25 --no-overlay-reserve \
    --page-width=1000 --page-height=1000 \
    --prefix=no_overlay_reserve --output-dir="$work/no_overlay_reserve/pages" >/dev/null)
no_overlay_page=$(find "$work/no_overlay_reserve/pages" -maxdepth 1 -type f -name '*.ppm' -print -quit)
"$root/test/ppm_transform" overlay-mask --output "$work/overlay_mask.ppm" \
    --circle-color "0 0 0" --background-color "255 255 255" \
    --width 1000 --height 1000
if "$bin" overlay "$no_overlay_page" "$work/overlay_mask.ppm" 0.10 \
    >"$work/no_overlay_merged.ppm" 2>"$work/no_overlay_error.log"; then
    echo "canonical tile: overlay accepted a page without overlay reserve" >&2
    exit 1
fi
if ! rg -q "page was encoded without an overlay parity reserve" "$work/no_overlay_error.log"; then
    echo "canonical tile: no-reserve overlay failure had the wrong reason" >&2
    exit 1
fi

# A non-default marker geometry must produce the same known-erasure map on
# decode that the encoder used when padding structural parity.
mkdir -p "$work/custom_fiducials/pages" "$work/custom_fiducials/out"
(cd "$work" && "$bin" encode --input=payload.bin --ecc=0 \
    --page-width=1000 --page-height=1000 \
    --fiducials=6,30,15 --prefix=custom --output-dir="$work/custom_fiducials/pages" >/dev/null)
shopt -s nullglob
custom_pages=("$work"/custom_fiducials/pages/*.ppm)
shopt -u nullglob
if [[ ${#custom_pages[@]} -ne 1 ]]; then
    echo "canonical tile: custom-fiducial case expected one page, found ${#custom_pages[@]}" >&2
    exit 1
fi
"$harness" "${custom_pages[0]}" 1000 1000 1
"$bin" decode --output-dir="$work/custom_fiducials/out" "${custom_pages[0]}" >/dev/null
cmp "$work/payload.bin" "$work/custom_fiducials/out/payload.bin"

for dimensions in '23 23' '48 48' '49 48' '64 48'; do
    read -r width height <<<"$dimensions"
    "$harness" --capacity "$width" "$height"
done

mkdir -p "$work/80x80/pages" "$work/80x80/out"
(cd "$work" && "$bin" encode --input=tiny.bin --ecc=0 \
    --page-width=80 --page-height=80 \
    --prefix=page \
    --output-dir="$work/80x80/pages" >/dev/null)
page=$(find "$work/80x80/pages" -maxdepth 1 -type f -name '*.ppm' -print -quit)
page_count=$(find "$work/80x80/pages" -maxdepth 1 -type f -name '*.ppm' -print | wc -l | tr -d '[:space:]')
if [[ $page_count != 1 || -z $page ]]; then
    echo "canonical tile: expected one page, found $page_count" >&2
    exit 1
fi
"$harness" "$page" 80 80 1 --check-structural
"$bin" decode --output-dir="$work/80x80/out" "$page" >/dev/null
cmp "$work/tiny.bin" "$work/80x80/out/tiny.bin"
