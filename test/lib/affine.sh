#!/usr/bin/env bash

mako_check_affine() {
    local csv_path=$1
    local expected_scale=$2
    local expected_rotation=$3
    local tolerance_percent=$4
    local expected_pages=$5
    local label=${6:-affine}

    if [[ ! -f $csv_path ]]; then
        printf '%s missing affine CSV (%s)\n' "$label" "$csv_path" >&2
        return 1
    fi

    awk -F, \
        -v expected_scale="$expected_scale" \
        -v expected_rotation="$expected_rotation" \
        -v tolerance_percent="$tolerance_percent" \
        -v expected_pages="$expected_pages" \
        -v label="$label" '
        FNR == 1 { next }
        {
            rows++
            if ($2 != 1) {
                printf "%s page=%s has no affine estimate\n", label, $1
                failures++
                next
            }
            scale_error = $3 - expected_scale
            if (scale_error < 0) scale_error = -scale_error
            scale_error = 100 * scale_error / expected_scale
            rotation_error = $4 - expected_rotation
            if (rotation_error < 0) rotation_error = -rotation_error
            rotation_base = expected_rotation
            if (rotation_base < 0) rotation_base = -rotation_base
            rotation_error = 100 * rotation_error / rotation_base
            printf "%s page=%s affine scale=%.6f (%.3f%% error), rotation=%.6f deg (%.3f%% error)\n", \
                label, $1, $3, scale_error, $4, rotation_error
            if (scale_error > tolerance_percent || rotation_error > tolerance_percent) {
                printf "%s page=%s exceeds affine tolerance of %.3f%%\n", \
                    label, $1, tolerance_percent
                failures++
            }
        }
        END {
            if (rows != expected_pages) {
                printf "%s affine CSV has %d page rows; expected %d\n", label, rows, expected_pages
                failures++
            }
            if (failures) exit 1
            printf "%s affine estimates within %.3f%% tolerance for %d page(s)\n", \
                label, tolerance_percent, rows
        }
    ' "$csv_path"
}
