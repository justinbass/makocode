#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
repo_root=$(cd -- "$script_dir/.." && pwd -P)
. "$script_dir/lib/test_seed.sh"
bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
ppm_transform="$repo_root/test_output/bin/ppm_transform"

if [[ $# -ne 3 || -z $1 || -z $2 || -z $3 ]]; then
    echo "test_overlay_no_metadata: expected --label CASE_ID and a palette" >&2
    exit 1
fi
if [[ $1 != "--label" || -z $2 ]]; then
    echo "test_overlay_no_metadata: expected --label CASE_ID and a palette" >&2
    exit 1
fi
label=$2
shift 2
if [[ $# -ne 1 || -z $1 ]]; then
    echo "test_overlay_no_metadata: palette is required" >&2
    exit 1
fi
palette=$1

mako_test_seed_init "$repo_root"
payload_seed=$(mako_test_case_seed "payload/$label")
work=$(mktemp -d "$repo_root/test_output/${label}_metadata_free_overlay_work.XXXXXX")
mkdir -p "$work/pages"
"$ppm_transform" bytes --output "$work/payload.bin" --size 24 --seed "$payload_seed"
"$ppm_transform" overlay-mask --output "$work/overlay.ppm" \
    --circle-color "128 64 0" --background-color "255 255 255" \
    --width 1000 --height 1000

(
    cd "$work"
    "$bin" encode --input=payload.bin --palette "$palette" \
        --overlay-fraction=0.992156862745098 \
        --overlay-ppm="$work/overlay.ppm" --overlay-pages=all \
        --overlay-ignore-colors White \
        --page-width=1000 --page-height=1000 \
        --metadata-pages=all --no-metadata \
        --fiducial-spacing=24 --no-fiducials \
        --output-dir="$work/pages" --prefix="$label" --debug
) >"$work/encode.log" 2>&1

if ! rg -q 'debug: overlay erasure budget per block=[1-9][0-9]*' "$work/encode.log"; then
    echo "metadata-free overlay: encoder did not apply an overlay budget" >&2
    cat "$work/encode.log" >&2
    exit 1
fi
page="$work/pages/$label.ppm"
if [[ ! -f $page ]]; then
    echo "metadata-free overlay: encoder did not produce its page" >&2
    cat "$work/encode.log" >&2
    exit 1
fi
expected_fiducial_header='page,row,column,x,y,logical_x,logical_y'
fiducial_csv="$work/pages/${label}_encode_fiducials.csv"
if [[ ! -f $fiducial_csv ]] ||
   ! printf '%s\n' "$expected_fiducial_header" | cmp --silent - "$fiducial_csv"; then
    echo "metadata-free overlay: page contains fiducial markers" >&2
    exit 1
fi
python3 - "$page" <<'PY'
import sys

with open(sys.argv[1], "rb") as ppm:
    header = [ppm.readline() for _ in range(3)]
    pixels = ppm.read()
if header != [b"P6\n", b"1000 1000\n", b"255\n"]:
    raise SystemExit(f"metadata-free overlay: unexpected PPM metadata header: {header!r}")
if pixels.count(bytes((128, 64, 0))) < 1000:
    raise SystemExit("metadata-free overlay: high-overlay image was not applied")
PY

"$bin" decode --no-metadata --no-fiducials --palette "$palette" \
    --output-dir="$work/decoded" "$page" >"$work/decode.log" 2>&1 || {
    cat "$work/decode.log" >&2
    exit 1
}
if [[ ! -f $work/decoded/payload.bin ]]; then
    echo "metadata-free overlay: decoder did not produce payload.bin" >&2
    cat "$work/decode.log" >&2
    exit 1
fi
cmp --silent "$work/payload.bin" "$work/decoded/payload.bin"

printf 'metadata-free high-overlay roundtrip ok (label=%s)\n' "$label"
