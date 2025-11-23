#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
repo_root=$(cd -- "$script_dir/.." && pwd -P)
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

test_dir="$repo_root/test"
work_dir="$test_dir/${label}_overlay_work"
payload_path="$test_dir/${label}_overlay_payload.bin"
payload_work="$work_dir/payload.bin"
encoded_path="$test_dir/${label}_overlay_encoded.ppm"
overlay_path="$test_dir/${label}_overlay_mask.ppm"
merged_path="$test_dir/${label}_overlay_merged.ppm"
decoded_path="$test_dir/${label}_overlay_decoded.bin"

cleanup() {
    if [[ -d $work_dir ]]; then
        rm -rf "$work_dir"
    fi
}
trap cleanup EXIT

rm -rf "$work_dir"
mkdir -p "$test_dir" "$work_dir"
rm -f "$payload_path" "$encoded_path" "$overlay_path" "$merged_path" "$decoded_path"

head -c 32768 /dev/urandom > "$payload_path"
cp "$payload_path" "$payload_work"

encode_cmd=(
    "$makocode_bin" encode
    "--input=payload.bin"
    --ecc=1.0
    --page-width=1000
    --page-height=1000
    "--output-dir=$work_dir"
)
if [[ -n "${MAKO_OVERLAY_PALETTE:-}" ]]; then
    encode_cmd+=(--palette "${MAKO_OVERLAY_PALETTE}")
fi
(
    cd "$work_dir"
    "${encode_cmd[@]}"
) >/dev/null

shopt -s nullglob
ppms=("$work_dir"/*.ppm)
shopt -u nullglob
if [[ ${#ppms[@]} -eq 0 ]]; then
    echo "test_overlay_e2e: encode did not produce a ppm page" >&2
    exit 1
fi
mv -f "${ppms[0]}" "$encoded_path"

circle_color="${MAKO_OVERLAY_CIRCLE_COLOR:-0 0 0}"
background_color="${MAKO_OVERLAY_BACKGROUND_COLOR:-255 255 255}"
OVERLAY_PPM="$overlay_path" python3 - "$circle_color" "$background_color" <<'PY'
import os, sys

path = os.environ.get("OVERLAY_PPM")
if not path:
    raise SystemExit("overlay: missing OVERLAY_PPM environment")
w = h = 1000
cx = cy = w // 2
radius = int(w * 0.45)
circle_color = sys.argv[1]
background_color = sys.argv[2]
with open(path, "w", encoding="ascii", newline="\n") as f:
    f.write("P3\n")
    f.write(f"{w} {h}\n")
    f.write("255\n")
    for y in range(h):
        row = []
        for x in range(w):
            if (x - cx) * (x - cx) + (y - cy) * (y - cy) <= radius * radius:
                row.append(circle_color)
            else:
                row.append(background_color)
        f.write(" ".join(row))
        f.write("\n")
PY

fraction="${MAKO_OVERLAY_FRACTION:-0.1}"
"$makocode_bin" overlay "$encoded_path" "$overlay_path" "$fraction" > "$merged_path"

decoded_dir="$work_dir/decoded"
mkdir -p "$decoded_dir"
"$makocode_bin" decode "$merged_path" --output-dir "$decoded_dir"
decoded_source="$decoded_dir/payload.bin"
if [[ ! -f $decoded_source ]]; then
    echo "test_overlay_e2e: decode missing payload.bin" >&2
    exit 1
fi
cmp --silent "$payload_path" "$decoded_source"
mv "$decoded_source" "$decoded_path"

skip_grayscale="${MAKO_OVERLAY_SKIP_GRAYSCALE_CHECK:-0}"
python3 - "$encoded_path" "$merged_path" "$skip_grayscale" <<'PY'
import pathlib, sys

base = pathlib.Path(sys.argv[1])
merged = pathlib.Path(sys.argv[2])
skip_grayscale = bool(int(sys.argv[3]))

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
    if merged_nums[i*3:(i+1)*3] != base_nums[i*3:(i+1)*3]:
        diff += 1
    if not skip_grayscale:
        if not (r == g == b):
            raise SystemExit(f"non-grayscale pixel {r} {g} {b} at index {i}")
        if r not in (0, 255):
            raise SystemExit(f"pixel {r} {g} {b} is not pure black or white at index {i}")

if diff == 0:
    raise SystemExit('overlay did not modify any pixels')

ratio = diff / pixels
print(f"overlay pixels modified: {diff} ({ratio:.6f})")
PY

echo "overlay e2e ok (label=$label, artifacts prefix ${label}_overlay_*)"
