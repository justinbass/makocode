# Copyright (C) 2026 Justin Alan Bass.
# This file is part of MakoCode. MakoCode is free software: you can
# redistribute it and/or modify it under the terms of the GNU Affero
# General Public License as published by the Free Software Foundation,
# either version 3 of the License, or (at your option) any later version.
# MakoCode is distributed without any warranty; without even the implied
# warranty of merchantability or fitness for a particular purpose. See
# the LICENSE file for the full license text.
#
# Case registry consumed by run_test_case.sh. Keep case IDs unique: they also
# namespace the artifacts written under test_output/.

test_case 0000_tiny_0 roundtrip "1 byte payload literal '0' baseline" \
    --size 1 --ecc 0 --width 55 --height 55 \
    --encode-opt "--no-filename" \
    --encode-opt "--no-page-count" \
    --payload-literal "0"

test_case 0001_tiny_makocode roundtrip "8 byte payload literal 'makocode' baseline" \
    --size 8 --ecc 0 --width 55 --height 55 \
    --encode-opt "--no-filename" \
    --encode-opt "--no-page-count" \
    --payload-literal "makocode"

test_case 0002_baseline_8k_ecc_050 roundtrip "8 KiB payload, ECC 0.5 baseline" \
    --size 8192 --ecc 0.5 --width 500 --height 500

test_case 0003_baseline_8k_ecc_000 roundtrip "8 KiB payload, ECC disabled baseline" \
    --size 8192 --ecc 0 --width 500 --height 500

test_case 0004_baseline_multi_page_ecc_100 roundtrip "128 KiB payload, ECC 1.0 multi-page" \
    --size 131072 --ecc 1.0 --width 700 --height 700 --multi-page

test_case 0005_ecc_fill_moderate roundtrip "ECC fill saturates moderate payload" \
    --size 12000 --ecc 0.5 --width 420 --height 420 --encode-opt "--ecc-fill"

test_case 0006_ecc_fill_sparse_large_page roundtrip "ECC fill small payload on large page" \
    --size 1024 --ecc 0.5 --width 1400 --height 1800 --encode-opt "--ecc-fill"

test_case 0007_palette_white_black roundtrip "Two-color palette stress" \
    --size 8192 --ecc 0.25 --width 360 --height 360 --palette "White Black"

test_case 0008_palette_cmy roundtrip "Three-color palette stress" \
    --size 8192 --ecc 0.25 --width 420 --height 420 --palette "White Cyan Magenta"

test_case 0009_palette_cmyk roundtrip "Four-color palette stress" \
    --size 8192 --ecc 0.25 --width 480 --height 480 --palette "White Cyan Magenta Yellow"

test_case 0010_palette_cmykw roundtrip "Five-color palette stress" \
    --size 8192 --ecc 0.25 --width 480 --height 480 --palette "White Cyan Magenta Yellow Black"

test_case 0011_palette_hex_octet roundtrip "Expanded hex palette stress" \
    --size 8192 --ecc 0.25 --width 480 --height 480 \
    --palette "FFFFFF FF0000 00FF00 0000FF FFFF00 FF00FF 00FFFF 000000"

test_case 0012_large_canvas_cmykw roundtrip "Large canvas CMYKW palette" \
    --size 8192 --ecc 0.25 --width 1000 --height 1000 \
    --palette "White Cyan Magenta Yellow Black"

test_case 0013_low_ecc_fiducial roundtrip "Low ECC fiducial reservation, tiny pages" \
    --size 256 --ecc 0 --width 200 --height 64 \
    --encode-opt "--no-filename" \
    --encode-opt "--no-page-count"

test_case 0014_footer_metadata roundtrip "Footer metadata rendering stress" \
    --size 65536 --ecc 0.25 --width "$footer_charset_width" --height 800 \
    --title "$footer_charset_title" \
    --font-size "$footer_charset_font_size"

test_case 0015_gray_100k_single roundtrip "100 KiB grayscale single page baseline" \
    --size 102400 --ecc 0 --width 1100 --height 1100

test_case 0016_gray_100k_scaled roundtrip "100 KiB grayscale scaled 2.5x" \
    --size 102400 --ecc 0 --width 1100 --height 1100 --scale 2.5

test_case 0017_gray_100k_stretch_h26_v24 roundtrip "Grayscale stretch horizontal 2.6 / vertical 2.4" \
    --size 102400 --ecc 0 --width 1100 --height 1100 \
    --scale-x 2.6 --scale-y 2.4

test_case 0018_gray_100k_stretch_h24_v26 roundtrip "Grayscale stretch horizontal 2.4 / vertical 2.6" \
    --size 102400 --ecc 0 --width 1100 --height 1100 \
    --scale-x 2.4 --scale-y 2.6

test_case 0019_gray_scale_303_blur roundtrip "Grayscale upsample 3.03x (blur/no-rotation stress)" \
    --size 65536 --ecc 0.25 --width 900 --height 900 \
    --scale 3.03

test_case 0020_gray_rotate_seeded roundtrip "Grayscale rotation with deterministic seed" \
    --size 65536 --ecc 0.25 --width 900 --height 900 \
    --scale 3.0 --rotate 1 --transform-seed 1337

test_case 0021_gray_skew_xy_seeded roundtrip "Grayscale horizontal and vertical skew with deterministic seed" \
    --size 65536 --ecc 0.25 --width 900 --height 900 \
    --scale 3.0 --skew-x 5 --skew-y -5 --transform-seed 1337

test_case 0022_gray_skew_xy_reverse_seeded roundtrip "Grayscale horizontal and vertical skew in reverse directions" \
    --size 65536 --ecc 0.25 --width 900 --height 900 \
    --scale 3.0 --skew-x -5 --skew-y 5 --transform-seed 7331

test_case 0023_gray_rotate_skew_seeded roundtrip "Grayscale rotation combined with horizontal and vertical skew" \
    --size 65536 --ecc 0.25 --width 900 --height 900 \
    --scale 3.0 --rotate 1 --skew-x 5 --skew-y -5 --transform-seed 1337

test_case 0024_password_ecc roundtrip "Password-protected payload with ECC" \
    --size 32768 --ecc 0.5 --width 720 --height 720 --password suite-password

test_case 0025_ecc_multi_page_massive roundtrip "262 KiB ECC multi-page stress" \
    --size 262144 --ecc 0.25 --width 520 --height 520 --multi-page

test_case 0026_multi_page_distorted roundtrip "Multi-page payload with scale and rotation distortions" \
    --size 32768 --ecc 0.5 --width 620 --height 620 --multi-page \
    --scale 2.10 --rotate 1 --transform-seed 7331

test_case 0027_palette_base5_custom roundtrip "Custom palette/base-5 mode" \
    --size 16384 --ecc 0.25 --width 640 --height 640 \
    --palette "White Cyan Magenta Yellow Black"

test_case 0028_border_dirt_dense roundtrip "Dense border-noise stress with deterministic seed" \
    --size 32768 --ecc 0.25 --width 640 --height 640 \
    --border-thickness 12 --border-density 0.15 --transform-seed 8675309

test_case 0029_cli_output_dir cli "CLI respects explicit output directory"

test_case 0030_decode_wrong_depth decode "Decoder rejects malformed PPM (wrong maxval)" \
    --case wrong_depth

test_case 0031_decode_invalid_magic decode "Decoder rejects malformed PPM (bad magic)" \
    --case invalid_magic

test_case 0032_decode_all_black decode "Decoder rejects all-black image (no barcode)" \
    --case all_black

test_case 0033_decode_all_white decode "Decoder rejects all-white image (no barcode)" \
    --case all_white

test_case 0034_decode_random_noise decode "Decoder rejects random noise image" \
    --case random_noise

test_case 0035_decode_footer_data_destroyed decode "Decoder rejects page with footer but destroyed data region" \
    --case footer_data_destroyed

test_case 0036_decode_footer_valid_data_too_corrupt decode "Decoder rejects page with valid footer but ECC-overwhelmed data region" \
    --case footer_valid_data_too_corrupt

test_case 0037_palette_auto_discovery script "Decoder auto-discovers palette metadata" \
    test_palette_metadata.sh --mode auto

test_case 0038_palette_wrong_rejected script "Decoder rejects forced wrong palette" \
    test_palette_metadata.sh --mode wrong

test_case 0039_password_failures script "Decoder enforces password requirement" \
    test_password_failures.sh

test_case 0040_header_copy_corruption script "Header RS repairs and ECC metadata fallback" \
    test_header_copy_corruption.sh

test_case 0041_palette_white_black_blot_black roundtrip "High-ECC black blot recovery on White/Black page" \
    --size 4096 --ecc 8.0 --width 600 --height 600 --palette "White Black" \
    --ink-blot-radius 180 --ink-blot-color black

test_case 0042_palette_white_black_blot_white roundtrip "High-ECC white blot recovery on White/Black page" \
    --size 4096 --ecc 8.0 --width 600 --height 600 --palette "White Black" \
    --ink-blot-radius 180 --ink-blot-color white

test_case 0043_overlay_e2e overlay "Overlay CLI merges masks and decodes" \
    MAKO_OVERLAY_FRACTION=0.35 MAKO_OVERLAY_ENCODE_OPTS=--ecc-fill

test_case 0044_overlay_palette_cmyy overlay "Overlay CLI respects CMY+Yellow palette with yellow mask" \
    MAKO_OVERLAY_FRACTION=0.10 \
    MAKO_OVERLAY_ENCODE_OPTS=--ecc-fill \
    "MAKO_OVERLAY_PALETTE=White Cyan Magenta Yellow" \
    "MAKO_OVERLAY_CIRCLE_COLOR=255 255 0" \
    MAKO_OVERLAY_SKIP_GRAYSCALE_CHECK=1

test_case 0045_overlay_bw_base_color_cmwy overlay "Black/white base with CMYW circle overlay" \
    MAKO_OVERLAY_FRACTION=0.05 \
    MAKO_OVERLAY_ENCODE_OPTS=--ecc-fill \
    "MAKO_OVERLAY_PALETTE=White Black" \
    MAKO_OVERLAY_MASK_PALETTE_BASE=4 \
    "MAKO_OVERLAY_MASK_PALETTE_TEXT=White Cyan Magenta Yellow" \
    MAKO_OVERLAY_BACKGROUND_COLOR="0 0 0" \
    "MAKO_OVERLAY_CIRCLE_COLORS=0 255 255;255 0 255;255 255 0;255 255 255" \
    MAKO_OVERLAY_SKIP_GRAYSCALE_CHECK=1 \
    "MAKO_OVERLAY_ALLOWED_COLORS=0 0 0;255 255 255;0 255 255;255 0 255;255 255 0"

test_case 0046_overlay_cmy_base_bw overlay "CMYW base with black/white circle overlay" \
    MAKO_OVERLAY_FRACTION=0.1 \
    MAKO_OVERLAY_ENCODE_OPTS=--ecc-fill \
    "MAKO_OVERLAY_PALETTE=White Cyan Magenta Yellow" \
    MAKO_OVERLAY_SKIP_GRAYSCALE_CHECK=1 \
    "MAKO_OVERLAY_ALLOWED_COLORS=0 0 0;255 255 255;0 255 255;255 0 255;255 255 0"

test_case 0047_overlay_e2e_ignore_colors overlay "Overlay CLI skips white background when merging" \
    MAKO_OVERLAY_FRACTION=0.35 \
    MAKO_OVERLAY_ENCODE_OPTS=--ecc-fill \
    MAKO_OVERLAY_IGNORE_COLORS=White

test_case 0048_overlay_bw_ecc_target_025 overlay "Black/white circle overlay obeys ECC target 0.25" \
    MAKO_OVERLAY_FRACTION=0.1 \
    MAKO_OVERLAY_ENCODE_OPTS=--ecc-fill \
    "MAKO_OVERLAY_PALETTE=White Black" \
    MAKO_OVERLAY_CIRCLE_COLOR="0 0 0" \
    MAKO_OVERLAY_BACKGROUND_COLOR="255 255 255" \
    "MAKO_OVERLAY_ALLOWED_COLORS=0 0 0;255 255 255" \
    MAKO_OVERLAY_SKIP_GRAYSCALE_CHECK=1

test_case 0049_overlay_bw_ecc_target_050 overlay "Black/white circle overlay obeys ECC target 0.5" \
    MAKO_OVERLAY_FRACTION=0.1 \
    MAKO_OVERLAY_ENCODE_OPTS=--ecc-fill \
    "MAKO_OVERLAY_PALETTE=White Black" \
    MAKO_OVERLAY_CIRCLE_COLOR="0 0 0" \
    MAKO_OVERLAY_BACKGROUND_COLOR="255 255 255" \
    "MAKO_OVERLAY_ALLOWED_COLORS=0 0 0;255 255 255" \
    MAKO_OVERLAY_SKIP_GRAYSCALE_CHECK=1

test_case 0050_overlay_ecc_target_gray_base_color overlay "Grayscale base with color overlay exercises pixel candidate counting" \
    MAKO_OVERLAY_ECC_TARGET=0.25 \
    MAKO_OVERLAY_FRACTION=0.1 \
    "MAKO_OVERLAY_ENCODE_OPTS=--ecc-fill --no-filename --no-page-count" \
    "MAKO_OVERLAY_CIRCLE_COLOR=255 0 0" \
    "MAKO_OVERLAY_BACKGROUND_COLOR=0 255 0" \
    MAKO_OVERLAY_SKIP_GRAYSCALE_CHECK=1

test_case 0051_paper_offwhite_bw roundtrip "Slightly off-white, splotchy paper tint (BW)" \
    --size 8192 --ecc 0.5 --width 600 --height 600 --palette "White Black" \
    --transform-seed 424242 \
    --paper-color "FFF3D6" --paper-alpha 0.105 \
    --paper-splotch-alpha 0.15 --paper-splotch-shade 0.06 --paper-splotch-px 96

test_case 0052_paper_offwhite_cmyk roundtrip "Slightly off-white, splotchy paper tint (CMYK)" \
    --size 8192 --ecc 0.5 --width 640 --height 640 --palette "White Cyan Magenta Yellow" \
    --transform-seed 424242 \
    --paper-color "FFF3D6" --paper-alpha 0.105 \
    --paper-splotch-alpha 0.15 --paper-splotch-shade 0.06 --paper-splotch-px 96

test_case 0053_paper_offwhite_cmykwrbg roundtrip "Slightly off-white, splotchy paper tint (CMYKWRBG)" \
    --size 8192 --ecc 0.5 --width 720 --height 720 \
    --palette "FFFFFF 00FFFF FF00FF FFFF00 000000 FF0000 0000FF 00FF00" \
    --transform-seed 424242 \
    --paper-color "FFF3D6" --paper-alpha 0.105 \
    --paper-splotch-alpha 0.15 --paper-splotch-shade 0.06 --paper-splotch-px 96

test_case 0054_archive_tree_roundtrip script "Nested and empty archive directories round-trip" \
    test_archive_tree_roundtrip.sh

test_case 0055_cli_help script "CLI usage and command help smoke test" \
    test_cli_help.sh

test_case 0056_minify_literals script "Minify preserves literals and directives and emits compilable C++" \
    test_minify_literals.sh

test_case 0057_decode_stdin script "Decoder reads one PPM page from stdin" \
    test_decode_stdin.sh

test_case 0058_cli_decoder_paths script "Decoder paths and archive traversal rejection" \
    test_cli_decoder_paths.sh

test_case 0059_prefix_ends_encoded script "--prefix ending in _encoded keeps its filename and fiducials" \
    test_prefix_ends_encoded.sh

test_case 0060_overlay_ecc_target script "Overlay ECC target happy path" \
    test_overlay_ecc_target.sh

test_case 0061_archive_symlink_roundtrip script "Directory symlink target decodes as a regular file" \
    test_archive_symlink_roundtrip.sh

test_case 0062_compact_page script "Compact single-page output happy path" \
    test_compact_page.sh

test_case 0063_fiducials script "Custom fiducial dimensions happy path" \
    test_fiducials.sh

test_case 0064_default_encode_naming script "Default encode output uses a timestamped filename" \
    test_default_encode_naming.sh

test_case 0065_overlay_ecc_target_mixed script "Mixed-color overlay ECC target filters candidates and copies raw pixels" \
    test_overlay_ecc_target_mixed.sh

test_case 0066_decode_raw_fallback script "Raw decode fallback is gated and writes recovered payload bytes" \
    test_decode_raw_fallback.sh

test_case 0067_ppm_transform_cli script "ppm_transform help and fatal error paths" \
    test_ppm_transform_cli.sh

test_case 0068_decode_parse_retry script "Generic decoder parse failure retries once, then fails" \
    test_decode_parse_retry.sh

test_case 0069_decode_scale_318 script "Decode a 55x55 page rendered at 3.18 pixels per module" \
    test_decode_scale_318.sh

test_case 0070_decode_scale_2071 roundtrip "Fractional affine scan scale 2.071x" \
    --size 4096 --ecc 0.25 --width 620 --height 620 \
    --scale 2.071 --transform-seed 2071

test_case 0071_decode_scale_2701 roundtrip "Fractional affine scan scale 2.701x" \
    --size 4096 --ecc 0.25 --width 620 --height 620 \
    --scale 2.701 --transform-seed 2701

test_case 0072_decode_scale_4701 roundtrip "Fractional affine scan scale 4.701x" \
    --size 4096 --ecc 0.25 --width 620 --height 620 \
    --scale 4.701 --transform-seed 4701

test_case 0073_tiny_palette_2_white_checker roundtrip "Two-color tiny page with diagonal white damage at 2.701x" \
    --size 8 --ecc 0 --width 55 --height 55 \
    --encode-opt "--no-filename" \
    --encode-opt "--no-page-count" \
    --palette "White Black" \
    --payload-literal "makocode" \
    --scale 2.701 --white-checker --transform-seed 2701

test_case 0074_tiny_palette_4_white_checker roundtrip "Four-color tiny page with diagonal white damage at 2.701x" \
    --size 8 --ecc 0 --width 55 --height 55 \
    --encode-opt "--no-filename" \
    --encode-opt "--no-page-count" \
    --palette "White Cyan Magenta Yellow" \
    --payload-literal "makocode" \
    --scale 2.701 --white-checker --transform-seed 2701

test_case 0075_tiny_palette_8_white_checker roundtrip "Eight-color 8-byte payload with diagonal white damage at 2.701x" \
    --size 8 --ecc 0.25 --width 480 --height 480 \
    --encode-opt "--no-filename" \
    --encode-opt "--no-page-count" \
    --palette "FFFFFF FF0000 00FF00 0000FF FFFF00 FF00FF 00FFFF 000000" \
    --payload-literal "makocode" \
    --scale 2.701 --white-checker --transform-seed 2701
