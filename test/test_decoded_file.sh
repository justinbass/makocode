#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname "$0")/.." && pwd -P)
bin=${MAKOCODE_BIN:-"$root/makocode"}
label=${2:-decoded_file}
work="$root/test_output/${label}_work"
rm -rf "$work"
mkdir -p "$work/input/empty" "$work/input/sub" "$work/pages" "$work/out"
printf 'hello\nback\\slash\000\377' > "$work/input/sub/a b"
: > "$work/input/blank"
(cd "$work" && "$bin" encode --input=input --page-width=200 --page-height=200 --no-filename --no-page-count --ecc=0.5 --password=secret --prefix=page --output-dir=pages >/dev/null)
"$bin" decode --decoded-file --password=secret --output-dir="$work/out" "$work"/pages/*.ppm >/dev/null
python3 - "$work" <<'PY'
import pathlib, re, sys
work = pathlib.Path(sys.argv[1])
files = list((work / 'out').iterdir())
assert len(files) == 1 and re.fullmatch(r'makocode_decoded_\d{14}\.txt', files[0].name)
lines = files[0].read_text('ascii').splitlines()
assert lines[0] == 'MAKOCODE-DECODED' and lines[-2:] == ['end-archive', 'end']
assert 'metadata encryption_version 1' in lines and 'metadata ecc_version 1' in lines
assert 'metadata compression LZMA' in lines and 'metadata tile_schema 1' in lines
for field, size in [('encryption_salt', 32), ('encryption_nonce', 24), ('encryption_tag', 32), ('lzma_properties', 10)]:
    matches = [line.split(' ', 2)[2] for line in lines if line.startswith('metadata ' + field + ' ')]
    assert len(matches) == 1 and re.fullmatch('[0-9A-F]{%d}' % size, matches[0])
assert 'directory input/empty' in lines and 'file input/blank 0' in lines
line = lines.index('file input/sub/a\\x20b 18')
assert lines[line+1] == 'hello\\x0Aback\\x5Cslash\\x00\\xFF'
assert lines[lines.index('file input/blank 0')+1] == ''
PY
mkdir -p "$work/stdin_out"
cat "$work"/pages/*.ppm | "$bin" decode --decoded-file --password=secret --output-dir="$work/stdin_out" >/dev/null
cmp "$work"/out/*.txt "$work"/stdin_out/*.txt
if "$bin" decode --decoded-file --password=secret --output-dir="$work/input/blank" "$work"/pages/*.ppm >/dev/null 2>&1; then
    echo 'decoded file unexpectedly written to a regular file' >&2
    exit 1
fi
mkdir -p "$work/plain_pages" "$work/plain_out"
python3 - "$work/plain.bin" <<'PY'
import random, sys
random.seed(42)
open(sys.argv[1], 'wb').write(bytes(random.randrange(256) for _ in range(3000)))
PY
(cd "$work" && "$bin" encode --input=plain.bin --page-width=160 --page-height=160 --no-filename --no-page-count --ecc=0 --prefix=page --output-dir=plain_pages >/dev/null)
"$bin" decode --decoded-file --output-dir="$work/plain_out" "$work"/plain_pages/*.ppm >/dev/null
python3 - "$work" <<'PY'
import pathlib, re, sys
work = pathlib.Path(sys.argv[1])
pages = list((work / 'plain_pages').glob('*.ppm'))
assert len(pages) > 1
lines = next((work / 'plain_out').iterdir()).read_text('ascii').splitlines()
assert sum(line.startswith('page ') for line in lines) == len(pages)
assert 'metadata ecc_enabled 0' in lines
assert not any(line.startswith('metadata encryption_') for line in lines)
content = lines[lines.index('file plain.bin 3000') + 1]
restored = bytearray()
i = 0
while i < len(content):
    if content[i] == '\\':
        assert content[i+1] == 'x'
        restored.append(int(content[i+2:i+4], 16))
        i += 4
    else:
        restored.append(ord(content[i]))
        i += 1
assert restored == (work / 'plain.bin').read_bytes()
PY
mkdir -p "$work/collision" "$work/write_failure"
python3 - "$work/collision" <<'PY'
import datetime, pathlib, sys
out = pathlib.Path(sys.argv[1])
now = datetime.datetime.now()
for offset in range(-5, 31):
    name = (now + datetime.timedelta(seconds=offset)).strftime('makocode_decoded_%Y%m%d%H%M%S.txt')
    (out / name).write_text('existing')
PY
if "$bin" decode --decoded-file --output-dir="$work/collision" "$work"/plain_pages/*.ppm >/dev/null 2>&1; then
    echo 'decoded file unexpectedly overwrote existing output' >&2
    exit 1
fi
if (trap '' XFSZ; ulimit -f 1; "$bin" decode --decoded-file --output-dir="$work/write_failure" "$work"/plain_pages/*.ppm >/dev/null 2>&1); then
    echo 'decoded file unexpectedly succeeded with a file size limit' >&2
    exit 1
fi
if compgen -G "$work/write_failure/*" >/dev/null; then
    echo 'failed decoded output was not removed' >&2
    exit 1
fi
