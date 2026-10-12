#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
repo_root=$(cd -- "$script_dir/.." && pwd -P)
. "$script_dir/lib/colors.sh"

usage() {
    cat <<'USAGE'
Usage: run_test_case.sh --list | --run CASE_ID

  --list          Print all case IDs in test_cases.sh.
  --run CASE_ID   Run one registered case.
USAGE
}

mode=""
selected_case=""
case $# in
    1)
        if [[ $1 == "--list" ]]; then
            mode="list"
        else
            usage >&2
            exit 2
        fi
        ;;
    2)
        if [[ $1 == "--run" && -n $2 ]]; then
            mode="run"
            selected_case=$2
        else
            usage >&2
            exit 2
        fi
        ;;
    *)
        usage >&2
        exit 2
        ;;
esac

# These values are shared with the footer metadata round-trip case.
footer_charset_title=$' !"#$%&\\\'()*+,-./0123456789:;<=>?@[\\]^_`{|}~ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxy'
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

case_found=0
case_status=0

test_case() {
    local case_id=$1
    local runner=$2
    local description=$3
    shift 3

    if [[ $mode == "list" ]]; then
        printf '%s\n' "$case_id"
        return 0
    fi
    if [[ $case_id != "$selected_case" ]]; then
        return 0
    fi

    case_found=1
    local label_fmt
    label_fmt=$(mako_format_label "$case_id")
    printf '%s %s\n' "$label_fmt" "$description"

    local status=0
    case $runner in
        roundtrip)
            if "$script_dir/run_roundtrip.sh" --label "$case_id" "$@"; then
                status=0
            else
                status=$?
            fi
            ;;
        cli)
            if "$script_dir/test_cli_output_dir.sh" --label "$case_id"; then
                status=0
            else
                status=$?
            fi
            ;;
        decode)
            if "$script_dir/test_decode_failures.sh" --label "$case_id" "$@"; then
                status=0
            else
                status=$?
            fi
            ;;
        script)
            local script_name=$1
            shift
            if [[ $script_name == */* || ! -x "$script_dir/$script_name" ]]; then
                printf '%s invalid test script: %s\n' "$label_fmt" "$script_name" >&2
                status=2
            elif "$script_dir/$script_name" --label "$case_id" "$@"; then
                status=0
            else
                status=$?
            fi
            ;;
        overlay)
            local env_assignment
            for env_assignment in "$@"; do
                if [[ $env_assignment != *=* ]]; then
                    printf '%s invalid overlay environment assignment: %s\n' \
                        "$label_fmt" "$env_assignment" >&2
                    status=2
                    break
                fi
            done
            if [[ $status -eq 0 ]]; then
                if env "$@" "$script_dir/test_overlay_e2e.sh" "$case_id"; then
                    status=0
                else
                    status=$?
                fi
            fi
            ;;
        *)
            printf '%s unknown test runner: %s\n' "$label_fmt" "$runner" >&2
            status=2
            ;;
    esac

    if [[ $status -eq 0 ]]; then
        printf '%s %bPASS%b\n' "$label_fmt" "$MAKO_PASS_COLOR" "$MAKO_RESET_COLOR"
    else
        printf '%s %bFAIL (exit %d)%b\n' \
            "$label_fmt" "$MAKO_FAIL_COLOR" "$status" "$MAKO_RESET_COLOR" >&2
    fi
    case_status=$status
}

if [[ $mode == "run" ]]; then
    seed_file="$repo_root/test/test_seed.txt"
    if [[ ! -f $seed_file ]]; then
        echo "run_test_case: missing suite seed $seed_file; run through make test" >&2
        exit 2
    fi
    MAKO_TEST_SEED=$(<"$seed_file")
    export MAKO_TEST_SEED
    export MAKO_TEST_SEED_REPORTED=1
fi

. "$script_dir/test_cases.sh"

if [[ $mode == "run" ]]; then
    if [[ $case_found -eq 0 ]]; then
        echo "run_test_case: unknown case '$selected_case'" >&2
        exit 2
    fi
    exit "$case_status"
fi
