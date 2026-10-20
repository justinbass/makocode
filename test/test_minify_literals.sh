#!/usr/bin/env bash
# Copyright (C) 2026 Justin Alan Bass.
# This file is part of MakoCode. MakoCode is free software: you can
# redistribute it and/or modify it under the terms of the GNU Affero
# General Public License as published by the Free Software Foundation,
# either version 3 of the License, or (at your option) any later version.
# MakoCode is distributed without any warranty; without even the implied
# warranty of merchantability or fitness for a particular purpose. See
# the LICENSE file for the full license text.
#
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
. "$script_dir/lib/colors.sh"

label="0056_minify_literals"
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

if [[ ${COVERAGE:-0} == 1 ]]; then
    mkdir -p "$repo_root/test_output"
    work_dir=$(mktemp -d "$repo_root/test_output/${label}_minify.XXXXXX")
    trap 'status=$?; if [[ $status -ne 0 ]]; then rm -rf "$work_dir"; fi' EXIT
else
    work_dir=$(mktemp -d "/tmp/$label.XXXXXX")
    trap 'rm -rf "$work_dir"' EXIT
fi
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

static  const int before_directive = 0;
#define MINIFY_LATE_VALUE 7
static const int after_directive = MINIFY_LATE_VALUE;

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
    if (before_directive != 0 || after_directive != 7) return 5;
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
if rg -q '  ' "$minified_source"; then
    echo 'test_minify_literals: repeated whitespace was retained' >&2
    exit 1
fi
"$compiler" -std=c++17 -Wall -Wextra -pedantic "$minified_source" -o "$work_dir/minified_test"
"$work_dir/minified_test"
empty_source_dir="$work_dir/empty_source"
mkdir -p "$empty_source_dir"
: > "$empty_source_dir/makocode.cpp"
(
    cd "$empty_source_dir"
    "$makocode_bin" minify >/dev/null
)
if [[ ! -f "$empty_source_dir/makocode_minified.cpp" ||
      -s "$empty_source_dir/makocode_minified.cpp" ]]; then
    echo 'test_minify_literals: empty source did not produce empty minified output' >&2
    exit 1
fi
control_whitespace_dir="$work_dir/control_whitespace"
mkdir -p "$control_whitespace_dir"
printf 'int\fvalue;\vint\tother;\n' > "$control_whitespace_dir/makocode.cpp"
(
    cd "$control_whitespace_dir"
    "$makocode_bin" minify >/dev/null
)
if [[ $(cat "$control_whitespace_dir/makocode_minified.cpp") != 'int value; int other;' ]] ||
   LC_ALL=C grep -q $'[\t\f\v]' "$control_whitespace_dir/makocode_minified.cpp"; then
    echo 'test_minify_literals: control whitespace was not normalized' >&2
    exit 1
fi
char_trailing_escape_dir="$work_dir/char_trailing_escape"
mkdir -p "$char_trailing_escape_dir"
printf '%s' "'abc" > "$char_trailing_escape_dir/makocode.cpp"
printf '\\' >> "$char_trailing_escape_dir/makocode.cpp"
(
    cd "$char_trailing_escape_dir"
    "$makocode_bin" minify >/dev/null
)
if ! cmp -s "$char_trailing_escape_dir/makocode.cpp" \
    "$char_trailing_escape_dir/makocode_minified.cpp"; then
    echo 'test_minify_literals: trailing character-literal escape was not preserved' >&2
    exit 1
fi
string_trailing_escape_dir="$work_dir/string_trailing_escape"
mkdir -p "$string_trailing_escape_dir"
printf '%s' '"abc' > "$string_trailing_escape_dir/makocode.cpp"
printf '\\' >> "$string_trailing_escape_dir/makocode.cpp"
(
    cd "$string_trailing_escape_dir"
    "$makocode_bin" minify >/dev/null
)
if ! cmp -s "$string_trailing_escape_dir/makocode.cpp" \
    "$string_trailing_escape_dir/makocode_minified.cpp"; then
    echo 'test_minify_literals: trailing string-literal escape was not preserved' >&2
    exit 1
fi
invalid_args_output="$work_dir/invalid_arguments_output.txt"
if "$makocode_bin" minify unexpected >"$invalid_args_output" 2>&1; then
    invalid_args_status=0
else
    invalid_args_status=$?
fi
if [[ $invalid_args_status -ne 1 ]] ||
   ! grep -Fq 'minify: this command does not accept arguments' "$invalid_args_output"; then
    echo 'test_minify_literals: minify argument rejection was not reported as expected' >&2
    cat "$invalid_args_output" >&2
    exit 1
fi
crlf_dir="$work_dir/crlf"
mkdir -p "$crlf_dir"
while IFS= read -r line || [[ -n $line ]]; do
    printf '%s\r\n' "$line"
done < "$work_dir/makocode.cpp" > "$crlf_dir/makocode.cpp"
(
    cd "$crlf_dir"
    "$makocode_bin" minify >/dev/null
)
crlf_minified_source="$crlf_dir/makocode_minified.cpp"
if LC_ALL=C grep -q $'\r' "$crlf_minified_source"; then
    echo 'test_minify_literals: CRLF input left carriage returns in the output' >&2
    exit 1
fi
"$compiler" -std=c++17 -Wall -Wextra -pedantic "$crlf_minified_source" \
    -o "$work_dir/crlf_minified_test"
"$work_dir/crlf_minified_test"
write_failure_dir="$work_dir/write_failure"
mkdir -p "$write_failure_dir/makocode_minified.cpp"
cp "$work_dir/makocode.cpp" "$write_failure_dir/makocode.cpp"
write_failure_output="$work_dir/write_failure_output.txt"
write_failure_status=0
if (cd "$write_failure_dir" && "$makocode_bin" minify) >"$write_failure_output" 2>&1; then
    write_failure_status=0
else
    write_failure_status=$?
fi
if [[ $write_failure_status -ne 1 ]] ||
   ! grep -Fq 'minify: failed to write makocode_minified.cpp' "$write_failure_output"; then
    echo 'test_minify_literals: minify write failure was not reported as expected' >&2
    cat "$write_failure_output" >&2
    exit 1
fi
allocation_failure_dir="$work_dir/allocation_failure"
mkdir -p "$allocation_failure_dir"
printf 'int value;\n' > "$allocation_failure_dir/makocode.cpp"
if [[ ${COVERAGE:-0} == 1 ]]; then
    allocation_harness_flags=(-std=c++17 -Wall -Wextra -pedantic -O0 -g --coverage)
else
    allocation_harness_flags=(-std=c++17 -Wall -Wextra -pedantic -O2)
fi
allocation_harness="$work_dir/minify_allocation_failure_harness"
"$compiler" "${allocation_harness_flags[@]}" \
    "$script_dir/test_minify_allocation_failure_harness.cpp" -o "$allocation_harness"
read_failure_dir="$work_dir/read_failure"
mkdir -p "$read_failure_dir"
read_failure_output="$work_dir/read_failure_output.txt"
if (cd "$read_failure_dir" && "$allocation_harness" 0) \
    >"$read_failure_output" 2>&1; then
    read_failure_status=0
else
    read_failure_status=$?
fi
if [[ $read_failure_status -ne 0 ]] ||
   ! grep -Fq 'minify: failed to read makocode.cpp' "$read_failure_output"; then
    echo 'test_minify_literals: missing source file was not reported as expected' >&2
    cat "$read_failure_output" >&2
    exit 1
fi
read_error_dir="$work_dir/read_error"
mkdir -p "$read_error_dir/makocode.cpp"
read_error_output="$work_dir/read_error_output.txt"
if (cd "$read_error_dir" && "$makocode_bin" minify) >"$read_error_output" 2>&1; then
    read_error_status=0
else
    read_error_status=$?
fi
if [[ $read_error_status -ne 1 ]] ||
   ! grep -Fq 'minify: failed to read makocode.cpp' "$read_error_output"; then
    echo 'test_minify_literals: source read failure was not reported as expected' >&2
    cat "$read_error_output" >&2
    exit 1
fi
read_allocation_failure_output="$work_dir/read_allocation_failure_output.txt"
if (cd "$allocation_failure_dir" && "$allocation_harness" 1) \
    >"$read_allocation_failure_output" 2>&1; then
    read_allocation_failure_status=0
else
    read_allocation_failure_status=$?
fi
if [[ $read_allocation_failure_status -ne 0 ]] ||
   ! grep -Fq 'minify: failed to read makocode.cpp' "$read_allocation_failure_output"; then
    echo 'test_minify_literals: source allocation failure was not reported as expected' >&2
    cat "$read_allocation_failure_output" >&2
    exit 1
fi
allocation_failure_output="$work_dir/allocation_failure_output.txt"
if (cd "$allocation_failure_dir" && "$allocation_harness") \
    >"$allocation_failure_output" 2>&1; then
    allocation_failure_status=0
else
    allocation_failure_status=$?
fi
if [[ $allocation_failure_status -ne 0 ]] ||
   ! grep -Fq 'minify: failed to compact whitespace' "$allocation_failure_output"; then
    echo 'test_minify_literals: minify allocation failure was not reported as expected' >&2
    cat "$allocation_failure_output" >&2
    exit 1
fi
strip_failure_output="$work_dir/strip_allocation_failure_output.txt"
if (cd "$allocation_failure_dir" && "$allocation_harness" 2) \
    >"$strip_failure_output" 2>&1; then
    strip_failure_status=0
else
    strip_failure_status=$?
fi
if [[ $strip_failure_status -ne 0 ]] ||
   ! grep -Fq 'minify: failed to strip comments' "$strip_failure_output"; then
    echo 'test_minify_literals: comment-stripping allocation failure was not reported as expected' >&2
    cat "$strip_failure_output" >&2
    exit 1
fi
char_literal_failure_dir="$work_dir/char_literal_allocation_failure"
mkdir -p "$char_literal_failure_dir"
printf '%064d' 0 > "$char_literal_failure_dir/makocode.cpp"
printf "'" >> "$char_literal_failure_dir/makocode.cpp"
char_literal_failure_output="$work_dir/char_literal_allocation_failure_output.txt"
if (cd "$char_literal_failure_dir" && "$allocation_harness" 3) \
    >"$char_literal_failure_output" 2>&1; then
    char_literal_failure_status=0
else
    char_literal_failure_status=$?
fi
if [[ $char_literal_failure_status -ne 0 ]] ||
   ! grep -Fq 'minify: failed to strip comments' "$char_literal_failure_output"; then
    echo 'test_minify_literals: character-literal allocation failure was not reported as expected' >&2
    cat "$char_literal_failure_output" >&2
    exit 1
fi
char_escape_failure_dir="$work_dir/char_escape_allocation_failure"
mkdir -p "$char_escape_failure_dir"
printf '%062d' 0 > "$char_escape_failure_dir/makocode.cpp"
printf '%s' "'\\x" >> "$char_escape_failure_dir/makocode.cpp"
char_escape_failure_output="$work_dir/char_escape_allocation_failure_output.txt"
if (cd "$char_escape_failure_dir" && "$allocation_harness" 3) \
    >"$char_escape_failure_output" 2>&1; then
    char_escape_failure_status=0
else
    char_escape_failure_status=$?
fi
if [[ $char_escape_failure_status -ne 0 ]] ||
   ! grep -Fq 'minify: failed to strip comments' "$char_escape_failure_output"; then
    echo 'test_minify_literals: character escape allocation failure was not reported as expected' >&2
    cat "$char_escape_failure_output" >&2
    exit 1
fi
char_content_failure_dir="$work_dir/char_content_allocation_failure"
mkdir -p "$char_content_failure_dir"
printf '%063d' 0 > "$char_content_failure_dir/makocode.cpp"
printf '%s' "'x" >> "$char_content_failure_dir/makocode.cpp"
char_content_failure_output="$work_dir/char_content_allocation_failure_output.txt"
if (cd "$char_content_failure_dir" && "$allocation_harness" 3) \
    >"$char_content_failure_output" 2>&1; then
    char_content_failure_status=0
else
    char_content_failure_status=$?
fi
if [[ $char_content_failure_status -ne 0 ]] ||
   ! grep -Fq 'minify: failed to strip comments' "$char_content_failure_output"; then
    echo 'test_minify_literals: character content allocation failure was not reported as expected' >&2
    cat "$char_content_failure_output" >&2
    exit 1
fi
string_literal_failure_dir="$work_dir/string_literal_allocation_failure"
mkdir -p "$string_literal_failure_dir"
printf '%064d' 0 > "$string_literal_failure_dir/makocode.cpp"
printf '"' >> "$string_literal_failure_dir/makocode.cpp"
string_literal_failure_output="$work_dir/string_literal_allocation_failure_output.txt"
if (cd "$string_literal_failure_dir" && "$allocation_harness" 3) \
    >"$string_literal_failure_output" 2>&1; then
    string_literal_failure_status=0
else
    string_literal_failure_status=$?
fi
if [[ $string_literal_failure_status -ne 0 ]] ||
   ! grep -Fq 'minify: failed to strip comments' "$string_literal_failure_output"; then
    echo 'test_minify_literals: string-literal allocation failure was not reported as expected' >&2
    cat "$string_literal_failure_output" >&2
    exit 1
fi
string_escape_failure_dir="$work_dir/string_escape_allocation_failure"
mkdir -p "$string_escape_failure_dir"
printf '%062d' 0 > "$string_escape_failure_dir/makocode.cpp"
printf '%s' '"\x' >> "$string_escape_failure_dir/makocode.cpp"
string_escape_failure_output="$work_dir/string_escape_allocation_failure_output.txt"
if (cd "$string_escape_failure_dir" && "$allocation_harness" 3) \
    >"$string_escape_failure_output" 2>&1; then
    string_escape_failure_status=0
else
    string_escape_failure_status=$?
fi
if [[ $string_escape_failure_status -ne 0 ]] ||
   ! grep -Fq 'minify: failed to strip comments' "$string_escape_failure_output"; then
    echo 'test_minify_literals: string escape allocation failure was not reported as expected' >&2
    cat "$string_escape_failure_output" >&2
    exit 1
fi
string_content_failure_dir="$work_dir/string_content_allocation_failure"
mkdir -p "$string_content_failure_dir"
printf '%063d' 0 > "$string_content_failure_dir/makocode.cpp"
printf '%s' '"x' >> "$string_content_failure_dir/makocode.cpp"
string_content_failure_output="$work_dir/string_content_allocation_failure_output.txt"
if (cd "$string_content_failure_dir" && "$allocation_harness" 3) \
    >"$string_content_failure_output" 2>&1; then
    string_content_failure_status=0
else
    string_content_failure_status=$?
fi
if [[ $string_content_failure_status -ne 0 ]] ||
   ! grep -Fq 'minify: failed to strip comments' "$string_content_failure_output"; then
    echo 'test_minify_literals: string content allocation failure was not reported as expected' >&2
    cat "$string_content_failure_output" >&2
    exit 1
fi
block_comment_newline_failure_dir="$work_dir/block_comment_newline_allocation_failure"
mkdir -p "$block_comment_newline_failure_dir"
printf '%064d' 0 > "$block_comment_newline_failure_dir/makocode.cpp"
printf '%s\n' '/*' >> "$block_comment_newline_failure_dir/makocode.cpp"
block_comment_newline_failure_output="$work_dir/block_comment_newline_allocation_failure_output.txt"
if (cd "$block_comment_newline_failure_dir" && "$allocation_harness" 3) \
    >"$block_comment_newline_failure_output" 2>&1; then
    block_comment_newline_failure_status=0
else
    block_comment_newline_failure_status=$?
fi
if [[ $block_comment_newline_failure_status -ne 0 ]] ||
   ! grep -Fq 'minify: failed to strip comments' "$block_comment_newline_failure_output"; then
    echo 'test_minify_literals: block-comment newline allocation failure was not reported as expected' >&2
    cat "$block_comment_newline_failure_output" >&2
    exit 1
fi
line_comment_newline_failure_dir="$work_dir/line_comment_newline_allocation_failure"
mkdir -p "$line_comment_newline_failure_dir"
printf '%064d' 0 > "$line_comment_newline_failure_dir/makocode.cpp"
printf '%s\n' '//' >> "$line_comment_newline_failure_dir/makocode.cpp"
line_comment_newline_failure_output="$work_dir/line_comment_newline_allocation_failure_output.txt"
if (cd "$line_comment_newline_failure_dir" && "$allocation_harness" 3) \
    >"$line_comment_newline_failure_output" 2>&1; then
    line_comment_newline_failure_status=0
else
    line_comment_newline_failure_status=$?
fi
if [[ $line_comment_newline_failure_status -ne 0 ]] ||
   ! grep -Fq 'minify: failed to strip comments' "$line_comment_newline_failure_output"; then
    echo 'test_minify_literals: line-comment newline allocation failure was not reported as expected' >&2
    cat "$line_comment_newline_failure_output" >&2
    exit 1
fi
whitespace_failure_dir="$work_dir/whitespace_allocation_failure"
mkdir -p "$whitespace_failure_dir"
printf '%064d ' 0 > "$whitespace_failure_dir/makocode.cpp"
whitespace_failure_output="$work_dir/whitespace_allocation_failure_output.txt"
if (cd "$whitespace_failure_dir" && "$allocation_harness" 5) \
    >"$whitespace_failure_output" 2>&1; then
    whitespace_failure_status=0
else
    whitespace_failure_status=$?
fi
if [[ $whitespace_failure_status -ne 0 ]] ||
   ! grep -Fq 'minify: failed to compact whitespace' "$whitespace_failure_output"; then
    echo 'test_minify_literals: minify whitespace allocation failure was not reported as expected' >&2
    cat "$whitespace_failure_output" >&2
    exit 1
fi
newline_failure_dir="$work_dir/newline_allocation_failure"
mkdir -p "$newline_failure_dir"
printf '%064d\n' 0 > "$newline_failure_dir/makocode.cpp"
newline_failure_output="$work_dir/newline_allocation_failure_output.txt"
if (cd "$newline_failure_dir" && "$allocation_harness" 5) \
    >"$newline_failure_output" 2>&1; then
    newline_failure_status=0
else
    newline_failure_status=$?
fi
if [[ $newline_failure_status -ne 0 ]] ||
   ! grep -Fq 'minify: failed to compact whitespace' "$newline_failure_output"; then
    echo 'test_minify_literals: minify newline allocation failure was not reported as expected' >&2
    cat "$newline_failure_output" >&2
    exit 1
fi
directive_newline_failure_dir="$work_dir/directive_newline_allocation_failure"
mkdir -p "$directive_newline_failure_dir"
printf '#%063d\n' 0 > "$directive_newline_failure_dir/makocode.cpp"
directive_newline_failure_output="$work_dir/directive_newline_allocation_failure_output.txt"
if (cd "$directive_newline_failure_dir" && "$allocation_harness" 5) \
    >"$directive_newline_failure_output" 2>&1; then
    directive_newline_failure_status=0
else
    directive_newline_failure_status=$?
fi
if [[ $directive_newline_failure_status -ne 0 ]] ||
   ! grep -Fq 'minify: failed to compact whitespace' "$directive_newline_failure_output"; then
    echo 'test_minify_literals: preprocessor newline allocation failure was not reported as expected' >&2
    cat "$directive_newline_failure_output" >&2
    exit 1
fi
directive_failure_dir="$work_dir/directive_allocation_failure"
mkdir -p "$directive_failure_dir"
printf '#define VALUE 1\n' > "$directive_failure_dir/makocode.cpp"
directive_failure_output="$work_dir/directive_allocation_failure_output.txt"
if (cd "$directive_failure_dir" && "$allocation_harness") \
    >"$directive_failure_output" 2>&1; then
    directive_failure_status=0
else
    directive_failure_status=$?
fi
if [[ $directive_failure_status -ne 0 ]] ||
   ! grep -Fq 'minify: failed to compact whitespace' "$directive_failure_output"; then
    echo 'test_minify_literals: preprocessor allocation failure was not reported as expected' >&2
    cat "$directive_failure_output" >&2
    exit 1
fi
label_fmt=$(mako_format_label "$label")
printf '%s SUCCESS minify preserves literals/directives and reports output/allocation failures\n' "$label_fmt"
