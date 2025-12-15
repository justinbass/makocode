#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
repo_root=$(cd -- "$script_dir/.." && pwd -P)
makocode_bin="$repo_root/makocode"
ppm_transform_bin="$repo_root/scripts/ppm_transform"

if [[ ! -x "$makocode_bin" ]]; then
    echo "test_overlay_e2e: makocode binary not found at $makocode_bin" >&2
    exit 1
fi
if [[ ! -x "$ppm_transform_bin" ]]; then
    echo "test_overlay_e2e: ppm_transform helper not found at $ppm_transform_bin" >&2
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

ecc="${MAKO_OVERLAY_ECC:-1.0}"
ignore_colors="${MAKO_OVERLAY_IGNORE_COLORS:-}"
ecc_target="${MAKO_OVERLAY_ECC_TARGET:-}"

cleanup() {
    local exit_code=${1:-0}
    if [[ $exit_code -ne 0 ]]; then
        return
    fi
    if [[ -d $work_dir ]]; then
        rm -rf "$work_dir"
    fi
}
on_exit() {
    local exit_code=$?
    cleanup "$exit_code"
}
trap 'on_exit' EXIT

rm -rf "$work_dir"
mkdir -p "$test_dir" "$work_dir"
rm -f "$payload_path" "$encoded_path" "$overlay_path" "$merged_path" "$decoded_path"

head -c 32768 /dev/urandom > "$payload_path"
cp "$payload_path" "$payload_work"

encode_cmd=(
    "$makocode_bin" encode
    "--input=payload.bin"
    "--ecc=$ecc"
    --page-width=1000
    --page-height=1000
    "--output-dir=$work_dir"
)
if [[ -n "${MAKO_OVERLAY_PALETTE:-}" ]]; then
    encode_cmd+=(--palette "${MAKO_OVERLAY_PALETTE}")
fi
if [[ -n "${MAKO_OVERLAY_ENCODE_OPTS:-}" ]]; then
    read -r -a encode_extra <<< "$MAKO_OVERLAY_ENCODE_OPTS"
    if [[ ${#encode_extra[@]} -gt 0 ]]; then
        encode_cmd+=("${encode_extra[@]}")
    fi
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
"$ppm_transform_bin" overlay-mask \
    --output "$overlay_path" \
    --circle-color "$circle_color" \
    --background-color "$background_color" \
    --width 1000 \
    --height 1000

fraction="${MAKO_OVERLAY_FRACTION:-0.1}"
overlay_cmd=("$makocode_bin" overlay)
if [[ -n "$ignore_colors" ]]; then
    overlay_cmd+=(--ignore-colors "$ignore_colors")
fi
if [[ -n "$ecc_target" ]]; then
    overlay_cmd+=(--overlay-ecc-target "$ecc_target")
fi
overlay_cmd+=("$encoded_path" "$overlay_path" "$fraction")
"${overlay_cmd[@]}" > "$merged_path"

# Preserve footer stripe rows from the original to avoid damaging the barcode.
"$ppm_transform_bin" copy-footer-rows --encoded "$encoded_path" --merged "$merged_path"

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
"$ppm_transform_bin" overlay-check --base "$encoded_path" --merged "$merged_path" --skip-grayscale "$skip_grayscale"

echo "overlay e2e ok (label=$label, artifacts prefix ${label}_overlay_*)"
