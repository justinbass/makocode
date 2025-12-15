#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
. "$script_dir/lib/colors.sh"

usage() {
    cat <<'USAGE'
Usage: test_decode_failures.sh [--label NAME] [--case NAME]

  --label NAME    Identifier for log messages (default: decode_failures).
  --case NAME     Which case to run:
                   wrong_depth
                   invalid_magic
                   all_black
                   all_white
                   random_noise
                   footer_data_destroyed
                   footer_valid_data_too_corrupt
                   all (default)
  --help          Show this help message.
USAGE
}

label="decode_failures"
case_name="all"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --label)
            label=${2:-}
            shift 2
            ;;
        --case)
            case_name=${2:-}
            shift 2
            ;;
        --help)
            usage
            exit 0
            ;;
        *)
            echo "test_decode_failures: unknown flag '$1'" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ -z $label ]]; then
    echo "test_decode_failures: --label requires a value" >&2
    exit 1
fi

if [[ -z ${case_name:-} ]]; then
    echo "test_decode_failures: --case requires a value" >&2
    exit 1
fi

format_command() {
    local formatted="" quoted=""
    for arg in "$@"; do
        printf -v quoted '%q' "$arg"
        if [[ -z $formatted ]]; then
            formatted=$quoted
        else
            formatted+=" $quoted"
        fi
    done
    printf '%s' "$formatted"
}

print_makocode_cmd() {
    local phase=$1
    shift
    local label_fmt
    label_fmt=$(mako_format_label "$label")
    printf '%s makocode %s: %s\n' "$label_fmt" "$phase" "$(format_command "$@")"
}

run_expect_failure() {
    local phase=$1
    shift
    print_makocode_cmd "$phase" "$@"
    set +e
    "$@" >/dev/null 2>&1
    local status=$?
    set -e
    if [[ $status -eq 0 ]]; then
        echo "test_decode_failures: decoder unexpectedly succeeded for $phase" >&2
        exit 1
    fi
}

repo_root=$(cd -- "$script_dir/.." && pwd -P)
makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
if [[ ! -x $makocode_bin ]]; then
    echo "test_decode_failures: makocode binary not found at $makocode_bin" >&2
    exit 1
fi

test_dir="$repo_root/test"
mkdir -p "$test_dir"
wrong_depth="$test_dir/${label}_wrong_depth.ppm"
invalid_magic="$test_dir/${label}_invalid_magic.ppm"
all_black="$test_dir/${label}_all_black.ppm"
all_white="$test_dir/${label}_all_white.ppm"
random_noise="$test_dir/${label}_random_noise.ppm"
footer_data_destroyed="$test_dir/${label}_footer_present_data_destroyed.ppm"
footer_valid_data_too_corrupt="$test_dir/${label}_footer_valid_data_too_corrupt.ppm"
work_dir="$test_dir/${label}_work"
payload_path="$work_dir/payload.bin"
rm -f "$wrong_depth" "$invalid_magic" "$all_black" "$all_white" "$random_noise" "$footer_data_destroyed" "$footer_valid_data_too_corrupt"
rm -rf "$work_dir"

run_case_wrong_depth() {
    cat > "$wrong_depth" <<'PPM'
P3
2 2
42
0 0 0  0 0 0
0 0 0  0 0 0
PPM
    run_expect_failure "decode-wrong-depth" "$makocode_bin" decode "$wrong_depth"
}

run_case_invalid_magic() {
    cat > "$invalid_magic" <<'PPM'
P6
2 2
255
PPM
    run_expect_failure "decode-invalid-magic" "$makocode_bin" decode "$invalid_magic"
}

write_solid_ppm() {
    local path=$1
    local width=$2
    local height=$3
    local r=$4
    local g=$5
    local b=$6
    python3 - "$path" "$width" "$height" "$r" "$g" "$b" <<'PY'
import sys
from pathlib import Path

path = Path(sys.argv[1])
width = int(sys.argv[2])
height = int(sys.argv[3])
r = int(sys.argv[4])
g = int(sys.argv[5])
b = int(sys.argv[6])

path.parent.mkdir(parents=True, exist_ok=True)
with path.open("w", encoding="ascii") as fh:
    fh.write("P3\n")
    fh.write(f"{width} {height}\n")
    fh.write("255\n")
    for _ in range(width * height):
        fh.write(f"{r} {g} {b}\n")
PY
}

# 1) Totally black, no barcode present.
run_case_all_black() {
    write_solid_ppm "$all_black" 256 256 0 0 0
    run_expect_failure "decode-all-black" "$makocode_bin" decode "$all_black"
}

# 2) Totally white, no barcode present.
run_case_all_white() {
    write_solid_ppm "$all_white" 256 256 255 255 255
    run_expect_failure "decode-all-white" "$makocode_bin" decode "$all_white"
}

write_noise_ppm() {
    local path=$1
    local width=$2
    local height=$3
    local seed=$4
    python3 - "$path" "$width" "$height" "$seed" <<'PY'
import random
import sys
from pathlib import Path

path = Path(sys.argv[1])
width = int(sys.argv[2])
height = int(sys.argv[3])
seed = int(sys.argv[4])

rng = random.Random(seed)
path.parent.mkdir(parents=True, exist_ok=True)
with path.open("w", encoding="ascii") as fh:
    fh.write("P3\n")
    fh.write(f"{width} {height}\n")
    fh.write("255\n")
    for _ in range(width * height):
        r = rng.randrange(0, 256)
        g = rng.randrange(0, 256)
        b = rng.randrange(0, 256)
        fh.write(f"{r} {g} {b}\n")
PY
}

# 3) Random noise image (deterministic seed).
run_case_random_noise() {
    write_noise_ppm "$random_noise" 256 256 1337
    run_expect_failure "decode-random-noise" "$makocode_bin" decode "$random_noise"
}

# 4) Footer stripe present, but data region destroyed with noise.
run_case_footer_data_destroyed() {
    mkdir -p "$work_dir"
    head -c 64 /dev/urandom > "$payload_path"
    print_makocode_cmd "encode-footer-only" \
        "$makocode_bin" encode \
        "--input=payload.bin" \
        "--page-width=260" \
        "--page-height=260" \
        "--no-filename" \
        "--no-page-count" \
        "--output-dir=$work_dir"
    (
        cd "$work_dir"
        "$makocode_bin" encode \
            --input=payload.bin \
            --page-width=260 \
            --page-height=260 \
            --no-filename \
            --no-page-count \
            --output-dir="$work_dir" \
            >/dev/null
    )

    shopt -s nullglob
    ppm_paths=("$work_dir"/*.ppm)
    shopt -u nullglob
    if [[ ${#ppm_paths[@]} -eq 0 ]]; then
        echo "test_decode_failures: encode produced no PPM pages for footer corruption case" >&2
        exit 1
    fi
    if [[ ${#ppm_paths[@]} -ne 1 ]]; then
        echo "test_decode_failures: footer corruption case expected 1 page, got ${#ppm_paths[@]}" >&2
        exit 1
    fi

    python3 - "${ppm_paths[0]}" "$footer_data_destroyed" <<'PY'
import random
import sys
from pathlib import Path

src = Path(sys.argv[1])
dst = Path(sys.argv[2])

with src.open("r", encoding="ascii") as fh:
    if fh.readline().strip() != "P3":
        raise SystemExit("footer_corrupt: expected P3 PPM")
    tokens = []
    while len(tokens) < 3:
        line = fh.readline()
        if not line:
            raise SystemExit("footer_corrupt: truncated header")
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        tokens.extend(stripped.split())
    width = int(tokens[0])
    height = int(tokens[1])
    maxval = int(tokens[2])
    if maxval != 255:
        raise SystemExit(f"footer_corrupt: expected maxval 255, got {maxval}")
    pixels = []
    for line in fh:
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        pixels.extend(int(value) for value in stripped.split())

expected = width * height * 3
if len(pixels) != expected:
    raise SystemExit(f"footer_corrupt: pixel count mismatch (wanted {expected}, got {len(pixels)})")

# Default palette with no custom palette text -> V3 footer rows are stable:
# payload bytes=23, metadata bytes=34, parity bytes=8, total bytes=42 => rows=6.
footer_height_px = 12
if height <= footer_height_px:
    raise SystemExit(f"footer_corrupt: image too short ({height}px) for footer stripe height {footer_height_px}px")

rng = random.Random(424242)
stripe_top = height - footer_height_px
for y in range(stripe_top):
    row_base = y * width * 3
    for x in range(width):
        idx = row_base + x * 3
        pixels[idx] = rng.randrange(0, 256)
        pixels[idx + 1] = rng.randrange(0, 256)
        pixels[idx + 2] = rng.randrange(0, 256)

dst.parent.mkdir(parents=True, exist_ok=True)
with dst.open("w", encoding="ascii") as fh:
    fh.write("P3\n")
    fh.write(f"{width} {height}\n")
    fh.write("255\n")
    for idx in range(0, len(pixels), 3):
        fh.write(f"{pixels[idx]} {pixels[idx+1]} {pixels[idx+2]}\n")
PY

    run_expect_failure "decode-footer-present-data-destroyed" "$makocode_bin" decode "$footer_data_destroyed"
    rm -rf "$work_dir"
}

run_case_footer_valid_data_too_corrupt() {
    mkdir -p "$work_dir"
    head -c 32768 /dev/urandom > "$payload_path"
    print_makocode_cmd "encode-ecc-overwhelmed" \
        "$makocode_bin" encode \
        "--input=payload.bin" \
        "--ecc=0.25" \
        "--page-width=600" \
        "--page-height=600" \
        "--no-filename" \
        "--no-page-count" \
        "--output-dir=$work_dir"
    (
        cd "$work_dir"
        "$makocode_bin" encode \
            --input=payload.bin \
            --ecc=0.25 \
            --page-width=600 \
            --page-height=600 \
            --no-filename \
            --no-page-count \
            --output-dir="$work_dir" \
            >/dev/null
    )

    shopt -s nullglob
    ppm_paths=("$work_dir"/*.ppm)
    shopt -u nullglob
    if [[ ${#ppm_paths[@]} -eq 0 ]]; then
        echo "test_decode_failures: encode produced no PPM pages for ECC overwhelm case" >&2
        exit 1
    fi
    if [[ ${#ppm_paths[@]} -ne 1 ]]; then
        echo "test_decode_failures: ECC overwhelm case expected 1 page, got ${#ppm_paths[@]}" >&2
        exit 1
    fi

    python3 - "${ppm_paths[0]}" "$footer_valid_data_too_corrupt" <<'PY'
import random
import sys
from pathlib import Path

src = Path(sys.argv[1])
dst = Path(sys.argv[2])

with src.open("r", encoding="ascii") as fh:
    if fh.readline().strip() != "P3":
        raise SystemExit("ecc_overwhelm: expected P3 PPM")
    tokens = []
    while len(tokens) < 3:
        line = fh.readline()
        if not line:
            raise SystemExit("ecc_overwhelm: truncated header")
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        tokens.extend(stripped.split())
    width = int(tokens[0])
    height = int(tokens[1])
    maxval = int(tokens[2])
    if maxval != 255:
        raise SystemExit(f"ecc_overwhelm: expected maxval 255, got {maxval}")
    pixels = []
    for line in fh:
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        pixels.extend(int(value) for value in stripped.split())

expected = width * height * 3
if len(pixels) != expected:
    raise SystemExit(f"ecc_overwhelm: pixel count mismatch (wanted {expected}, got {len(pixels)})")

# Preserve footer stripe by leaving the last 12px untouched (V3 rows=6 => 12px at 2px pitch
# when no footer text is present). Also preserve a generous border/margin so fiducials remain.
footer_height_px = 12
border_keep = 80
data_bottom = height - footer_height_px
if data_bottom <= border_keep + 1:
    raise SystemExit("ecc_overwhelm: image too short for chosen keep margins")

rng = random.Random(20251215)

y0 = border_keep
y1 = max(y0 + 1, data_bottom - border_keep)
x0 = border_keep
x1 = max(x0 + 1, width - border_keep)

for y in range(y0, y1):
    row_base = y * width * 3
    for x in range(x0, x1):
        idx = row_base + x * 3
        pixels[idx] = rng.randrange(0, 256)
        pixels[idx + 1] = rng.randrange(0, 256)
        pixels[idx + 2] = rng.randrange(0, 256)

dst.parent.mkdir(parents=True, exist_ok=True)
with dst.open("w", encoding="ascii") as fh:
    fh.write("P3\n")
    fh.write(f"{width} {height}\n")
    fh.write("255\n")
    for idx in range(0, len(pixels), 3):
        fh.write(f"{pixels[idx]} {pixels[idx+1]} {pixels[idx+2]}\n")
PY

    run_expect_failure "decode-footer-valid-data-too-corrupt" "$makocode_bin" decode "$footer_valid_data_too_corrupt"
    rm -rf "$work_dir"
}

case "${case_name}" in
    wrong_depth)
        run_case_wrong_depth
        ;;
    invalid_magic)
        run_case_invalid_magic
        ;;
    all_black)
        run_case_all_black
        ;;
    all_white)
        run_case_all_white
        ;;
    random_noise)
        run_case_random_noise
        ;;
    footer_data_destroyed)
        run_case_footer_data_destroyed
        ;;
    footer_valid_data_too_corrupt)
        run_case_footer_valid_data_too_corrupt
        ;;
    all)
        run_case_wrong_depth
        run_case_invalid_magic
        run_case_all_black
        run_case_all_white
        run_case_random_noise
        run_case_footer_data_destroyed
        run_case_footer_valid_data_too_corrupt
        ;;
    *)
        echo "test_decode_failures: unknown --case '${case_name}'" >&2
        usage >&2
        exit 1
        ;;
esac

label_fmt=$(mako_format_label "$label")
printf '%s SUCCESS decode failures rejected as expected\n' "$label_fmt"
