#!/usr/bin/env bash

FIDUCIAL_TOLERANCE=17
FIDUCIAL_COUNT_TOLERANCE=0

mako_check_fiducials() {
    local encoded_csv=$1
    local decoded_csv=$2
    local label=${3:-fiducials}

    if [[ ! -f $encoded_csv || ! -f $decoded_csv ]]; then
        printf '%s missing fiducial CSV (%s, %s)\n' "$label" "$encoded_csv" "$decoded_csv" >&2
        return 1
    fi

    awk -F, \
        -v position_tolerance="$FIDUCIAL_TOLERANCE" \
        -v count_tolerance="$FIDUCIAL_COUNT_TOLERANCE" \
        -v label="$label" '
        FNR == 1 { next }
        NR == FNR {
            key = $1 "," $2 "," $3
            encoded_x[key] = $4
            encoded_y[key] = $5
            encoded_count++
            next
        }
        {
            key = $1 "," $2 "," $3
            decoded_x[key] = $4
            decoded_y[key] = $5
            decoded_count++
            if (key in encoded_x) {
                matched_count++
                dx = encoded_x[key] - decoded_x[key]
                dy = encoded_y[key] - decoded_y[key]
                if (dx < 0) dx = -dx
                if (dy < 0) dy = -dy
                if (dx > position_tolerance || dy > position_tolerance) {
                    printf "%s fiducial page=%s row=%s column=%s offset=(%.3f,%.3f) exceeds +/-%.0f\n", \
                        label, $1, $2, $3, dx, dy, position_tolerance
                    failures++
                }
            }
        }
        END {
            count_delta = encoded_count - decoded_count
            if (count_delta < 0) count_delta = -count_delta
            if (count_delta > count_tolerance) {
                printf "%s fiducial count differs: encode=%d decode=%d (allowed difference %d)\n", \
                    label, encoded_count, decoded_count, count_tolerance
                failures++
            }
            minimum_matches = (encoded_count < decoded_count) ? encoded_count : decoded_count
            if (matched_count + count_tolerance < minimum_matches) {
                printf "%s matched only %d of %d comparable fiducials\n", label, matched_count, minimum_matches
                failures++
            }
            if (failures) exit 1
            printf "%s fiducials match: encode=%d decode=%d tolerance=+/-%.0f px\n", \
                label, encoded_count, decoded_count, position_tolerance
        }
    ' "$encoded_csv" "$decoded_csv"
}
