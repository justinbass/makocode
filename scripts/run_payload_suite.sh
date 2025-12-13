#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd -- "$(dirname "$0")/.." && pwd -P)
suite_bin="$repo_root/scripts/run_roundtrip.sh"
if [[ ! -x $suite_bin ]]; then
    echo "run_payload_suite: missing run_roundtrip.sh" >&2
    exit 1
fi

# Full footer font charset, in `FOOTER_GLYPHS` order (makocode.cpp).
footer_charset_title=$' !"#$%&\\\'()*+,-./0123456789:;<=>?@[\\\\]^_`{|}~ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxy'
footer_charset_font_size=2
footer_charset_filename="random.bin"
footer_charset_page_stub="Page 1/1"
footer_charset_title_len=${#footer_charset_title}
footer_charset_filename_len=${#footer_charset_filename}
footer_charset_page_len=${#footer_charset_page_stub}
footer_charset_text_len=$((footer_charset_title_len + 3 + footer_charset_filename_len + 3 + footer_charset_page_len))
footer_charset_glyph_width=$((5 * footer_charset_font_size))
footer_charset_char_spacing=$((footer_charset_font_size))
footer_charset_text_px=$((footer_charset_text_len * footer_charset_glyph_width + (footer_charset_text_len - 1) * footer_charset_char_spacing))
footer_charset_width=$((footer_charset_text_px + 64))

run_case() {
    local label=$1
    shift
    echo "run_payload_suite: $label"
    "$suite_bin" --label "$label" "$@"
}

# Low ECC fiducial reservation on tiny pages.
run_case 1023_low_ecc \
    --size 256 --ecc 0 --width 64 --height 64 \
    --encode-opt "--no-filename" \
    --encode-opt "--no-page-count"

# Footer title / metadata heavy footer rendering.
run_case 1022_footer_title \
    --size 65536 --ecc 0.25 --width "$footer_charset_width" --height 800 \
    --title "$footer_charset_title" \
    --font-size "$footer_charset_font_size"

# 100 KiB grayscale payload single page baseline.
run_case 2001_payload_gray_100k \
    --size 102400 --ecc 0 --width 1100 --height 1100

# 100 KiB scaled 2.5x on both axes.
run_case 2002_payload_gray_100k_scaled \
    --size 102400 --ecc 0 --width 1100 --height 1100 \
    --scale 2.5

# 100 KiB stretched horizontally (2.6) and vertically (2.4).
run_case 2005_payload_gray_100k_stretch_h26_v24 \
    --size 102400 --ecc 0 --width 1100 --height 1100 \
    --scale-x 2.6 --scale-y 2.4

# 100 KiB stretched vertically (2.6) and horizontally (2.4).
run_case 2006_payload_gray_100k_stretch_h24_v26 \
    --size 102400 --ecc 0 --width 1100 --height 1100 \
    --scale-x 2.4 --scale-y 2.6

# Password-protected + ECC payload.
run_case 2101_password_ecc \
    --size 32768 --ecc 0.5 --width 720 --height 720 \
    --password suite-password

# Multi-page ECC payload to stress pagination.
run_case 2102_multi_page \
    --size 262144 --ecc 0.25 --width 520 --height 520 \
    --multi-page

# Custom palette/base-5 mode.
run_case 2103_palette_base5 \
    --size 16384 --ecc 0.25 --width 640 --height 640 \
    --palette "White Cyan Magenta Yellow Black"

# Rotation + skew distortion on scans.
run_case 2104_border_dirt \
    --size 32768 --ecc 0.25 --width 640 --height 640 \
    --border-thickness 12 --border-density 0.15

echo "run_payload_suite: complete"
