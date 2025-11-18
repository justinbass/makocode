#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd -- "$(dirname "$0")/.." && pwd -P)
makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
if [[ ! -x $makocode_bin ]]; then
    echo "test_cli_output_dir: makocode binary not found at $makocode_bin" >&2
    exit 1
fi

mkdir -p "$repo_root/test"
tmp_dir=$(mktemp -d "$repo_root/test/output_dir_cli_tmp.XXXXXX")
payload="$tmp_dir/cli_payload.bin"
cleanup() {
    if [[ -d $tmp_dir ]]; then
        rm -rf "$tmp_dir"
    fi
}
trap cleanup EXIT

printf '%s' "encode-decode-cli test payload" > "$payload"
(
    cd "$tmp_dir"
    "$makocode_bin" encode \
        --input "$(basename "$payload")" \
        --ecc=0.5 \
        --page-width=100 \
        --page-height=100 \
        --no-filename \
        --no-page-count >/dev/null
)

shopt -s nullglob
ppms=("$tmp_dir"/*.ppm)
shopt -u nullglob
if [[ ${#ppms[@]} -eq 0 ]]; then
    echo "test_cli_output_dir: encode did not emit a PPM" >&2
    exit 1
fi
ppm_path="${ppms[0]}"
decode_dir="$tmp_dir/decoded"
mkdir -p "$decode_dir"
"$makocode_bin" decode "$ppm_path" --output-dir "$decode_dir" >/dev/null

decoded_payload="$decode_dir/cli_payload.bin"
if [[ ! -f $decoded_payload ]]; then
    echo "test_cli_output_dir: decode missing cli_payload.bin" >&2
    exit 1
fi
cmp --silent "$payload" "$decoded_payload"

mv "$payload" "$repo_root/test/3011_cli_payload.bin"
mv "$ppm_path" "$repo_root/test/3011_cli_payload_encoded.ppm"
mv "$decoded_payload" "$repo_root/test/3011_cli_payload_decoded.bin"

echo "test_cli_output_dir: ok"
