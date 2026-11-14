#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
repo_root=$(cd -- "$script_dir/.." && pwd -P)
label=footer_features
if [[ ${1:-} == --label && -n ${2:-} ]]; then
    label=$2
elif [[ -n ${1:-} ]]; then
    label=$1
fi
work_dir=$(mktemp -d "$repo_root/test_output/${label}_work.XXXXXX")
payload="$work_dir/payload.bin"

"$repo_root/test/ppm_transform" bytes --output "$payload" --size 16384 --seed 732881

wrap_dir="$work_dir/wrap"
mkdir -p "$wrap_dir"
cat "$payload" | "$repo_root/makocode" encode --page-width=220 --page-height=180 \
    --footer='a very long title that wraps across multiple footer lines without growing the page' \
    --ecc=fill --output-dir "$wrap_dir" --prefix wrap >/dev/null
shopt -s nullglob
wrap_pages=("$wrap_dir"/*.ppm)
shopt -u nullglob
if [[ ${#wrap_pages[@]} -eq 0 ]]; then
    echo "test_footer_features: wrapped-footer encode produced no pages" >&2
    exit 1
fi
for page in "${wrap_pages[@]}"; do
    read -r wrap_width wrap_height < <(sed -n '2p' "$page")
    if [[ $wrap_width -ne 220 || $wrap_height -ne 180 ]]; then
        echo "test_footer_features: fixed dimensions were not hard limits (${wrap_width}x${wrap_height})" >&2
        exit 1
    fi
done
"$repo_root/makocode" decode "${wrap_pages[@]}" --decoded-stdout 2>/dev/null |
    python3 "$script_dir/check_decoded_stdout.py" --generator "$repo_root/test/ppm_transform" \
        --size 16384 --seed 732881 --label "${label} wrapped footer"
too_short_dir="$work_dir/too_short"
mkdir -p "$too_short_dir"
if printf 'x' | "$repo_root/makocode" encode --page-width=220 --page-height=15 \
       --footer=LIMIT --font-size=2 --output-dir "$too_short_dir" --prefix too_short \
       >"$work_dir/too_short.log" 2>&1; then
    echo "test_footer_features: footer expanded a page beyond its fixed height" >&2
    exit 1
fi
shopt -s nullglob
too_short_pages=("$too_short_dir"/*.ppm)
shopt -u nullglob
if [[ ${#too_short_pages[@]} -ne 0 ]]; then
    echo "test_footer_features: insufficient fixed page height still produced output" >&2
    exit 1
fi

selected_dir="$work_dir/selected"
mkdir -p "$selected_dir"
cat "$payload" | "$repo_root/makocode" encode --page-width=180 --page-height=180 \
    --footer=MARK --footer-pages=1,middle --ecc=fill \
    --output-dir "$selected_dir" --prefix selected >/dev/null
shopt -s nullglob
selected_pages=("$selected_dir"/*.ppm)
shopt -u nullglob
if [[ ${#selected_pages[@]} -lt 3 ]]; then
    echo "test_footer_features: selected-page fixture did not produce enough pages" >&2
    exit 1
fi
python3 - "${selected_pages[@]}" <<'PY'
from pathlib import Path
import sys

pages = [Path(path).read_bytes() for path in sys.argv[1:]]
counts = []
for data in pages:
    header, pixels = data.split(b"\n255\n", 1)
    width, height = map(int, header.splitlines()[-1].split())
    band = pixels[(height - 8) * width * 3:]
    counts.append(sum(band[i] < 128 and band[i + 1] < 128 and band[i + 2] < 128
                      for i in range(0, len(band), 3)))
middle = (len(pages) + 1) // 2
expected = {0, middle - 1}
for index, count in enumerate(counts):
    if (index in expected and count == 0) or (index not in expected and count != 0):
        raise SystemExit(f"footer page selection mismatch at page {index + 1}: {counts}")
PY
"$repo_root/makocode" decode "${selected_pages[@]}" --decoded-stdout 2>/dev/null |
    python3 "$script_dir/check_decoded_stdout.py" --generator "$repo_root/test/ppm_transform" \
        --size 16384 --seed 732881 --label "${label} selected footer"

overlay_dir="$work_dir/overlay"
mkdir -p "$overlay_dir"
overlay_mask="$work_dir/footer_mask.ppm"
python3 - "$overlay_mask" <<'PY'
from pathlib import Path
import sys

width, height = 180, 180
pixels = bytearray([255]) * (width * height * 3)
pixels[-3:] = bytes((255, 0, 255))
Path(sys.argv[1]).write_bytes(f"P6\n{width} {height}\n255\n".encode() + pixels)
PY
cat "$payload" | "$repo_root/makocode" encode --page-width=180 --page-height=180 \
    --footer=MARK --overlay-fraction=0.1 --overlay-ppm "$overlay_mask" --ecc=fill \
    --output-dir "$overlay_dir" --prefix overlay >/dev/null
shopt -s nullglob
overlay_pages=("$overlay_dir"/*.ppm)
shopt -u nullglob
python3 - "${overlay_pages[0]}" <<'PY'
from pathlib import Path
import sys

data = Path(sys.argv[1]).read_bytes()
_, pixels = data.split(b"\n255\n", 1)
if tuple(pixels[-3:]) != (255, 255, 255):
    raise SystemExit("overlay wrote into the footer region")
PY
over_dir="$work_dir/over"
mkdir -p "$over_dir"
cat "$payload" | "$repo_root/makocode" encode --page-width=500 --page-height=500 \
    --footer=OVER --footer-over-barcode --ecc=fill \
    --output-dir "$over_dir" --prefix over >/dev/null
shopt -s nullglob
over_pages=("$over_dir"/*.ppm)
shopt -u nullglob
"$repo_root/makocode" decode "${over_pages[@]}" --decoded-stdout 2>/dev/null |
    python3 "$script_dir/check_decoded_stdout.py" --generator "$repo_root/test/ppm_transform" \
        --size 16384 --seed 732881 --label "${label} over-barcode footer"
"$repo_root/makocode" decode "${over_pages[@]}" --debug \
    --output-dir "$work_dir/over_debug" --decoded-file \
    >"$work_dir/over_decode.log" 2>/dev/null
if ! rg -q 'ECC detected [1-9][0-9]* symbol errors' "$work_dir/over_decode.log"; then
    echo "test_footer_features: over-barcode footer did not damage protected barcode symbols" >&2
    cat "$work_dir/over_decode.log" >&2
    exit 1
fi

weak_dir="$work_dir/weak"
mkdir -p "$weak_dir"
if printf 'footer' | "$repo_root/makocode" encode --page-width=500 --page-height=500 \
       --footer=OVER --footer-over-barcode --ecc=0.01 \
       --output-dir "$weak_dir" --prefix weak >"$work_dir/weak.log" 2>&1; then
    echo "test_footer_features: over-barcode footer accepted insufficient ECC" >&2
    exit 1
fi
if ! rg -q 'ECC cannot cover|footer.*ECC|ECC.*footer' "$work_dir/weak.log"; then
    echo "test_footer_features: insufficient ECC failure did not explain the requirement" >&2
    cat "$work_dir/weak.log" >&2
    exit 1
fi

printf 'footer features e2e ok (%s)\n' "$label"
