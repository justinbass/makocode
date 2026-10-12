#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
. "$script_dir/lib/colors.sh"

label="0054_minify_literals"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --label)
            label=${2:-}
            shift 2
            ;;
        --help)
            printf 'Usage: %s [--label NAME]\n' "$(basename "$0")"
            exit 0
            ;;
        *)
            printf 'test_minify_literals: unknown flag %s\n' "$1" >&2
            exit 1
            ;;
    esac
done

if [[ -z $label || $label == *[!a-zA-Z0-9_.-]* ]]; then
    echo 'test_minify_literals: invalid --label' >&2
    exit 1
fi

repo_root=$(cd -- "$script_dir/.." && pwd -P)
makocode_bin="$repo_root/makocode"
if [[ ! -x $makocode_bin ]]; then
    printf 'test_minify_literals: makocode binary not found at %s\n' "$makocode_bin" >&2
    exit 1
fi
compiler=g++
if ! command -v "$compiler" >/dev/null 2>&1; then
    printf 'test_minify_literals: C++ compiler not found: %s\n' "$compiler" >&2
    exit 1
fi

work_dir=$(mktemp -d "/tmp/$label.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT
cat > "$work_dir/makocode.cpp" <<'CPP'
  #define MINIFY_DIRECTIVE_SWITCH 1
#if MINIFY_DIRECTIVE_SWITCH
#define MINIFY_DIRECTIVE_VALUE "preprocessor directive kept"
#else
#error preprocessor directive line was not preserved
#endif

static bool text_matches(const char* actual, const char* expected) {
    while (*actual && *expected && *actual == *expected) {
        ++actual;
        ++expected;
    }
    return *actual == '\0' && *expected == '\0';
}

int main() {
    /* MINIFY_BLOCK_COMMENT_SENTINEL */
    const char* literal = "literal: // /* */ quote: \" preserved"; // MINIFY_LINE_COMMENT_SENTINEL
    const char* expected = "literal: // /* */ quote: \" preserved";
    const char chars[] = {'/', '*', 'x', '*', '/', '\0'};
    const char quote = '\'';
    if (!text_matches(literal, expected)) return 1;
    if (chars[0] != '/' || chars[1] != '*' || chars[3] != '*' || chars[4] != '/') return 2;
    if (quote != '\'') return 3;
    if (!text_matches(MINIFY_DIRECTIVE_VALUE, "preprocessor directive kept")) return 4;
    return 0;
}
CPP

(
    cd "$work_dir"
    "$makocode_bin" minify >/dev/null
)
minified_source="$work_dir/makocode_minified.cpp"
if [[ ! -s $minified_source ]]; then
    echo 'test_minify_literals: minify did not emit makocode_minified.cpp' >&2
    exit 1
fi
if rg -q 'MINIFY_(BLOCK|LINE)_COMMENT_SENTINEL' "$minified_source"; then
    echo 'test_minify_literals: a source comment was retained' >&2
    exit 1
fi
"$compiler" -std=c++17 -Wall -Wextra -pedantic "$minified_source" -o "$work_dir/minified_test"
"$work_dir/minified_test"
label_fmt=$(mako_format_label "$label")
printf '%s SUCCESS minify preserves literals and directives; emitted C++ compiles and runs\n' "$label_fmt"
