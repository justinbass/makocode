#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd -- "$(dirname "$0")/.." && pwd -P)
makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
if [[ ! -x $makocode_bin ]]; then
    echo "test_decode_failures: makocode binary not found at $makocode_bin" >&2
    exit 1
fi

mkdir -p "$repo_root/test"
tmp_dir=$(mktemp -d "$repo_root/test/decode_fail_tmp.XXXXXX")
cleanup() {
    if [[ -d $tmp_dir ]]; then
        rm -rf "$tmp_dir"
    fi
}
trap cleanup EXIT

wrong_depth="$tmp_dir/wrong_depth.ppm"
cat > "$wrong_depth" <<'PPM'
P3
2 2
42
0 0 0  0 0 0
0 0 0  0 0 0
PPM

invalid_magic="$tmp_dir/invalid_magic.ppm"
cat > "$invalid_magic" <<'PPM'
P6
2 2
255
PPM

if "$makocode_bin" decode "$wrong_depth" >/dev/null 2>&1; then
    echo "test_decode_failures: decoder accepted wrong maxval" >&2
    exit 1
fi
if "$makocode_bin" decode "$invalid_magic" >/dev/null 2>&1; then
    echo "test_decode_failures: decoder accepted invalid magic" >&2
    exit 1
fi

echo "test_decode_failures: expected failures observed"
