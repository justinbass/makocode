#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd -- "$(dirname "$0")/.." && pwd -P)
makocode_bin="$repo_root/makocode"

if [[ ! -x "$makocode_bin" ]]; then
    echo "test_overlay_e2e: makocode binary not found at $makocode_bin" >&2
    exit 1
fi

label="${1:-3004}"
if [[ -z "$label" ]]; then
    echo "test_overlay_e2e: label may not be empty" >&2
    exit 1
fi

mkdir -p "$repo_root/test"
tmp_dir=$(mktemp -d "$repo_root/test/${label}_overlay_tmp.XXXXXX")
trap 'rm -rf "$tmp_dir"' EXIT

payload="$tmp_dir/payload.bin"
head -c 32768 /dev/urandom > "$payload"

(
    cd "$tmp_dir"
    "$makocode_bin" encode --input payload.bin --ecc=1.0 --page-width=1000 --page-height=1000 > /dev/null
)

base_ppm=$(cd "$tmp_dir" && ls -1 *.ppm | head -n 1 || true)
if [[ -z "$base_ppm" ]]; then
    echo "test_overlay_e2e: encode did not produce a ppm page" >&2
    exit 1
fi

overlay="$tmp_dir/circle_overlay.ppm"
OVERLAY_PPM="$overlay" python3 - <<'PY'
import math
import os

path = os.environ["OVERLAY_PPM"]
w = h = 1000
cx = cy = w // 2
radius = int(w * 0.45)
white = "255 255 255"
black = "0 0 0"
with open(path, "w", encoding="ascii", newline="\n") as f:
    f.write("P3\n")
    f.write("# MAKOCODE_COLOR_CHANNELS 3\n")
    f.write(f"{w} {h}\n")
    f.write("255\n")
    for y in range(h):
        row = []
        for x in range(w):
            if (x - cx) * (x - cx) + (y - cy) * (y - cy) <= radius * radius:
                row.append(black)
            else:
                row.append(white)
        f.write(" ".join(row))
        f.write("\n")
PY

fraction=0.2
merged="$tmp_dir/merged.ppm"
"$makocode_bin" overlay "$tmp_dir/$base_ppm" "$overlay" "$fraction" > "$merged"

"$makocode_bin" decode "$merged" --output-dir "$tmp_dir/decoded"
cmp --silent "$payload" "$tmp_dir/decoded/payload.bin"

payload_dst="$repo_root/test/${label}_overlay_payload.bin"
encoded_dst="$repo_root/test/${label}_overlay_encoded.ppm"
overlay_dst="$repo_root/test/${label}_overlay_mask.ppm"
merged_dst="$repo_root/test/${label}_overlay_merged.ppm"
decoded_dst="$repo_root/test/${label}_overlay_decoded.bin"

rm -f "$payload_dst" "$encoded_dst" "$overlay_dst" "$merged_dst" "$decoded_dst"
mv "$payload" "$payload_dst"
mv "$tmp_dir/$base_ppm" "$encoded_dst"
mv "$overlay" "$overlay_dst"
mv "$merged" "$merged_dst"
mv "$tmp_dir/decoded/payload.bin" "$decoded_dst"

python3 - "$encoded_dst" "$merged_dst" <<'PY'
import pathlib, sys

base = pathlib.Path(sys.argv[1])
merged = pathlib.Path(sys.argv[2])

def read_ppm(path):
    tokens = []
    with path.open('r', encoding='ascii') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            tokens.extend(line.split())
    if not tokens or tokens[0] != 'P3':
        raise SystemExit(f"{path} missing P3 header")
    idx = 1
    width = int(tokens[idx]); idx += 1
    height = int(tokens[idx]); idx += 1
    maxval = int(tokens[idx]); idx += 1
    nums = list(map(int, tokens[idx:]))
    if len(nums) != width * height * 3:
        raise SystemExit(f"{path} pixel count mismatch")
    return width, height, nums

w, h, base_nums = read_ppm(base)
w2, h2, merged_nums = read_ppm(merged)
if (w, h) != (w2, h2):
    raise SystemExit('dimension mismatch')

pixels = w * h
diff = 0
for i in range(pixels):
    r, g, b = merged_nums[i*3:(i+1)*3]
    if not (r == g == b):
        raise SystemExit(f"non-grayscale pixel {r} {g} {b} at index {i}")
    if r not in (0, 255):
        raise SystemExit(f"pixel {r} {g} {b} is not pure black or white at index {i}")
    if merged_nums[i*3:(i+1)*3] != base_nums[i*3:(i+1)*3]:
        diff += 1

if diff == 0:
    raise SystemExit('overlay did not modify any pixels')

ratio = diff / pixels
print(f"overlay pixels modified: {diff} ({ratio:.6f})")
PY
echo "overlay e2e ok (label=$label, artifacts prefix ${label}_overlay_*)"
