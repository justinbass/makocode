#!/usr/bin/env bash

mako_compare_payload_with_tolerance() {
    local expected_path=$1
    local actual_path=$2
    local tolerance_percent=$3
    local label=${4:-payload}

    if [[ ! -f $expected_path || ! -f $actual_path ]]; then
        printf '%s missing payload (%s, %s)\n' "$label" "$expected_path" "$actual_path" >&2
        return 1
    fi

    local expected_size actual_size common_mismatches size_difference mismatch_count denominator
    expected_size=$(wc -c < "$expected_path" | tr -d '[:space:]')
    actual_size=$(wc -c < "$actual_path" | tr -d '[:space:]')
    common_mismatches=$({ cmp -l "$expected_path" "$actual_path" 2>/dev/null || true; } | awk 'END { print NR + 0 }')
    if (( expected_size > actual_size )); then
        size_difference=$((expected_size - actual_size))
    else
        size_difference=$((actual_size - expected_size))
    fi
    mismatch_count=$((common_mismatches + size_difference))
    if (( expected_size > actual_size )); then
        denominator=$expected_size
    else
        denominator=$actual_size
    fi
    if (( denominator == 0 )); then
        denominator=1
    fi

    awk -v mismatches="$mismatch_count" -v total="$denominator" \
        -v tolerance="$tolerance_percent" -v label="$label" '
        BEGIN {
            percent = 100 * mismatches / total
            printf "%s payload differs in %d/%d bytes (%.3f%%; tolerance %.3f%%)\n", \
                label, mismatches, total, percent, tolerance
            if (percent > tolerance) exit 1
        }
    '
}
