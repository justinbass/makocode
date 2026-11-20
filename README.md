# MakoCode🐾

A standard for color barcode printing with high density, designed for long-term archiving on paper or film. Self-contained clean-room implementation with no external codec dependencies.

Stubborn and dense as a Shiba, with no fluff. Lovingly named after my Shiba Inu, Mako 🐕, and about as silly as him.

Written entirely with gpt (totally not vibecoded).

## License

Copyright © 2026 Justin Alan Bass. MakoCode is licensed under the GNU Affero
General Public License, version 3 or later. See [LICENSE](LICENSE) for the
complete license text.

## Dependencies

The application is a standalone C++17 program. Building and running the CLI tests requires a C++17 compiler and GNU Make. The test runner uses POSIX process APIs to invoke the CLI.

### Freestanding integration

Runtime host services are routed through `makocode_platform_*` in
`src/makocode_platform.cpp`. The POSIX implementation is enabled with
`INCLUDES=1`; its OS headers and implementation are excluded with
`INCLUDES=0`. `make freestanding` compiles each translation unit with
`-ffreestanding` and `INCLUDES=0`, without linking a host OS adapter. A target
integration supplies definitions for the platform ABI when it links the
application; select its source with `PLATFORM_SOURCE=path/to/platform.cpp`.
File access, directory traversal, terminal input/output,
environment lookup, allocation, timestamps, and formatting stay behind this
OS-relevant C++ module. The runtime does not invoke shell utilities.

Keeping OS headers and services behind this boundary is part of making the
implementation futureproof and self-contained: a port can provide the
services supported by its environment while codec and archive logic remain
independent of the host filesystem and process environment.

## Usage

The application and test executables are written to `build/bin/`.

```
make
make test
./build/bin/makocode encode --input=file.txt
./build/bin/makocode encode --input=file.txt --palette "White Black"
# Render the minified source at an explicit page size
./build/bin/makocode minify src/makocode.cpp --width 2398 --height 2957
# Print, store, scan the output shown above
./build/bin/makocode decode scan.ppm
# To write one decoded file instead:
./build/bin/makocode decode --decoded-file scan.ppm
./build/bin/makocode decode --decoded-stdout scan.ppm
```

### Incremental checkpoints

`checkpoint create` builds a single checkpoint bundle from the supplied file or directory inputs. Repeat `--base` for previously decoded trees whose chunks may be reused. The bundle contains a full snapshot manifest and only chunk bodies not found in the base data. `--chunk-size` sets the average content-defined chunk target (default `4M`; accepted range `256` bytes through `1G`). Chunk sizes are approximate page targets; normal `encode` compression and layout determine the actual page use.

```sh
./build/bin/makocode checkpoint create --input current --base previous --output current.mcp --chunk-size 4M
./build/bin/makocode encode --input current.mcp --output-dir pages --password 'secret' --random-data="$random"
./build/bin/makocode decode --output-dir decoded --password 'secret' pages/*.ppm
./build/bin/makocode checkpoint restore --input decoded/current.mcp --base previous --output restored
```

Checkpoint bundles are files, so the existing `encode`/`decode` options remain in control of page layout, encryption, ECC, hidden capacity, and filler. A bundle and its decoded bases are plaintext local files; encrypted page output protects the manifest and chunk references. Page count and dimensions remain observable. Checkpoint IDs use SHA-256 and the manifest is inside the encoded payload.

Restore uses every matching chunk it can find in the bundle and supplied bases. If some chunks are unavailable or damaged, it still writes the available data: incomplete files receive a `.partial` suffix, missing byte ranges are zero-filled, and `checkpoint_recovery.txt` lists missing ranges and chunk IDs. For example, checkpoint 2 can be restored with checkpoint 0 even if checkpoint 1 is missing; references available only in checkpoint 1 become missing ranges in the partial output. Empty files and directories are preserved. As with ordinary archive inputs, checkpoint creation supports regular files and directories.

### Archival Notes

Prefer simple, well-documented formats: UTF-8 `.txt` for text, binary P6 `.ppm` for still images, and YUV4MPEG2 `.y4m` for video. PPM and YUV4MPEG2 store uncompressed samples; MakoCode compresses the archive payload with LZMA. Keep a short note with encoding, dimensions, frame rate, and color/chroma settings, and retain checksums separately. Assume archivists can interpret the C++ source and build notes without preserving the compiler's source. Avoid nonstandard file formats unless their complete decoder/codec source or format specification is archived with the data. These choices favor straightforward decoding, though uncompressed media can require substantial space. See the [PPM specification](https://netpbm.sourceforge.net/doc/ppm.html) and [YUV4MPEG2 format notes](https://manpages.debian.org/unstable/mjpegtools/yuv4mpeg.5.en.html).

Decode restores the original file tree by default. `--decoded-file` writes a timestamped `makocode_decoded_YYYYMMDDHHMMSS.txt` containing page and payload metadata and escaped archive entries. `--decoded-stdout` prints the same content without creating files; it cannot be combined with `--decoded-file` or `--debug`. Archive paths cannot be absolute or contain `..`. Extraction may follow symlinks in the output directory; use a fresh, trusted directory for untrusted input.

### Encryption randomness

Password-protected encoding uses an authenticated container with encrypted length descriptors. Its fixed cryptographic settings are built into the program and are not written as a clear header. The container carries a 16-byte salt and 12-byte nonce seed. Encrypted encoding requires `--random-data HEX`: exactly 56 hexadecimal digits (28 bytes). Generate fresh data for every encoding. MakoCode accepts the bytes from you and does not obtain randomness from the operating system.

In version 0, one-password streams derive a key with PBKDF2-HMAC-SHA256 (60,000 iterations and a fixed public salt), then use domain-separated HMAC-SHA256 inputs to seed a ChaCha20 stream that masks the eight-dot field symbols after Reed–Solomon encoding. This masks the fixed bootstrap and payload symbols; the selector seed is derived from the password and a fixed public domain, and is not stored in page metadata. Hidden-volume mode uses separate visible and hidden passwords, so it uses the public selector and relies on the authenticated encrypted container rather than applying one shared symbol mask. In both modes, decoding authenticates encrypted content before decompressing it.

Linux and macOS:

```sh
random=$(od -An -N28 -tx1 /dev/urandom | tr -d ' \n')
./build/bin/makocode encode --input=file.txt --password=... --random-data="$random"
```

Windows PowerShell:

```powershell
$bytes = [byte[]]::new(28)
[Security.Cryptography.RandomNumberGenerator]::Fill($bytes)
$random = [Convert]::ToHexString($bytes)
.\build\bin\makocode.exe encode --input=file.txt --password=... --random-data=$random
```

Diceware can supply the randomness too; these words are entropy input, not the encryption password. Use 18 independently selected words (about 232 bits of entropy), hash their exact space-separated text, and take 56 hex digits (28 bytes). Linux:

```sh
words='word1 word2 word3 word4 word5 word6 word7 word8 word9 word10 word11 word12 word13 word14 word15 word16 word17 word18'
random=$(printf '%s' "$words" | sha256sum | cut -c1-56)
./build/bin/makocode encode --input=file.txt --password=... --random-data="$random"
```

On macOS replace `sha256sum` with `shasum -a 256`. In PowerShell, set `$words` to the 18 words, then run:

```powershell
$sha = [Security.Cryptography.SHA256]::Create()
$digest = $sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($words))
$random = [Convert]::ToHexString($digest).Substring(0, 56)
.\build\bin\makocode.exe encode --input=file.txt --password=... --random-data=$random
```

Use a newly generated value for every encryption. Hashing formats the Diceware entropy into the required bytes; it does not add entropy.

### Hidden-capacity volumes and random page fill

`--hidden 0` is the default and reserves no hidden capacity. `--hidden R` reserves hidden capacity equal to `R` times the visible encrypted body length: `0.5` reserves about one third of the combined data region, and `2` reserves about two thirds. The hidden capacity is a limit; unused bytes at its end contain pseudorandom filler. Hidden mode requires the visible `--password` and `--random-data`, so its visible-body length descriptor is encrypted.

To store a hidden archive, provide both `--hidden-input PATH` and `--hidden-password TEXT`, in addition to the visible `--password` and `--random-data`. The two passwords must differ. Decode the visible or hidden archive by supplying its corresponding password. If no hidden input is supplied, the hidden-capacity region contains random filler only. `--hidden fill` uses the remaining data capacity of one page; `--fill-pages N` extends page fill to `N` physical pages when used with `--hidden fill` or `--ecc fill`. Combining `--hidden fill` with `--ecc fill` is rejected because both would claim the same capacity.

Visible and hidden descriptors and archive bodies are authenticated and encrypted in the data section. Password-protected data begins with only the supplied salt and nonce seed; it has no clear format marker or length. Decoding recognizes a protected container only when a supplied password authenticates a descriptor. Page count and page dimensions remain observable. ECC covers the encoded data section; random physical page fill after the protected stream is outside the RS codewords and is ignored during extraction. Page filling requires fresh `--random-data`, which can be supplied without `--password` when it is used only to seed filler.

The decoded file uses ASCII lines, begins with `MAKOCODE-DECODED`, and ends with `end`. Each `page N` section contains `metadata NAME VALUE` lines and ends with `end-page`. The `payload` section uses the same metadata form and ends with `end-payload`. Byte arrays in metadata are uppercase hexadecimal; numeric fields are decimal. Fields are present only when recovered. Page sections report the recovered metadata fields without the raw tile header or tile bytes.

The `archive` section gives `entries COUNT`, followed by `directory PATH` and `file PATH SIZE` entries in archive order, and ends with `end-archive`. A file's next line is its complete content; `SIZE` is its original byte count. Backslash, nonprintable bytes, and spaces in paths are encoded as `\xHH` with two uppercase hexadecimal digits. Content spaces are literal. All lines end in LF; even an empty file has a content line. Escaping applies to bytes, so UTF-8 and binary contents can be reconstructed exactly. Footer text is rendered in the image and is not recovered into the decoded file.

### Error correction and overlays

Version 0 stores a field symbol as exactly eight palette dots, most-significant dot first. For a palette of `q` colors, the field prime is the largest prime below `q^8`; dot patterns representing values at least that prime are invalid. The Reed–Solomon block length is `N = min(765, p - 1)`, using nonzero evaluation points. Profiles include:

| Palette colors `q` | Largest prime `p < q^8` | Block length `N` |
| ---: | ---: | ---: |
| 2 | 251 | 250 |
| 3 | 6,553 | 765 |
| 4 | 65,521 | 765 |
| 5 | 390,581 | 765 |
| 6 | 1,679,609 | 765 |
| 7 | 5,764,799 | 765 |
| 8 | 16,777,213 | 765 |
| 16 | 4,294,967,291 | 765 |

Compressed bytes, or the authenticated encrypted container, are packed as 96-byte big-endian integers into fixed-width base-`p` symbols. A fixed 48-symbol bootstrap code (`K=32`, `P=16`) protects the source length and payload code layout. Reed–Solomon blocks have `K` data symbols and `P=N-K` parity symbols. `--ecc RATIO` sets the requested parity/data ratio; `--ecc fill` searches layouts for page use. Version remains 0 and old byte-oriented payload encodings are not supported.

`encode --metadata-pages LIST` writes the center metadata tile only on selected pages. `LIST` accepts `all` (the default), page numbers, ranges, `middle`, and `last`; for example, `--metadata-pages=middle` writes the tile on page 2 of a three-page encoding. Pages without a tile still carry payload in the same raster positions. Multi-page decode therefore needs at least one supplied page with readable metadata.

`encode --overlay-fraction F --overlay-ppm overlay.ppm --overlay-pages LIST` selects and replaces whole eight-dot payload symbols. It never selects any of the 48 bootstrap symbols. The per-block overlay erasure count is `O=floor(F*N)`. The correction limit is `S + 2E + O <= P`, where `S` is any other known erasures and `E` is the number of unknown symbol errors. An unreadable or misclassified dot makes an unknown symbol error unless the decoder can confidently identify its symbol as an erasure. At `F=254/255`, the profile uses nearly all parity for overlays and has little or no remaining scan-error margin. `--overlay-ignore-colors` leaves matching selected pixels unchanged; the selected symbols still consume their reserved erasure budget. The selector order is deterministic per block and keyed by the stream password when one password governs the stream; hidden-volume mode uses the public selector because its two volumes use different passwords.

For marker-free single-page use, encode with `--no-metadata --no-fiducials`. Decode with the same `--no-metadata --no-fiducials`, the exact `--palette`, and matching `--page-width`/`--page-height`. The decoder reads the PPM as a one-pixel-per-dot grid from its top-left origin. Crops, rotation, rescaling, and origin shifts are unsupported; without registration marks, an image transformed back to the declared dimensions cannot always be distinguished from the original grid.


## Tests

`tests/makocode_test.cpp` contains the test suite and drives the application
through
its CLI. Run all cases with:

```
make -j4 test
```

The runner uses the job count from `make -jN`; each case prints one result line.
Work files and captured CLI output are kept in separate directories under
`test_output/`. `make clean` removes them. List case IDs with
`./build/bin/makocode_test --list` or run one case with
`./build/bin/makocode_test --case CASE_ID`.
In terminals, `[TEST]` is blue, `[PASS]` is green, and `[FAIL]` is red.

Each run prints a master seed and writes it to
`test_output/test_logs/test_seed.txt`. Repeat a randomized run with the same
seed using:

```
MAKO_TEST_SEED=$(cat test_output/test_logs/test_seed.txt) make -j4 test
```

You can also choose a seed explicitly, for example
`MAKO_TEST_SEED=12345 make -j4 test`. Random fixture seeds are derived from the
master seed and case label.

### Coverage

To run the same end-to-end suite with gcov-compatible instrumentation and
produce an HTML coverage report, install `lcov` (which also ships `genhtml`)
and run:

```
make coverage
```

This rebuilds both executables with `--coverage -O2 -g`, runs the same C++ test
suite, and captures coverage data from `build/bin/`. The LCOV report is written
to `build/coverage/report/makocode.info`, and the HTML dashboard to
`build/coverage/report/html/index.html`.

### Calibration

```
python3 scripts/calibration.py encode --width 2398 --height 2957
```

```
python3 scripts/calibration.py decode scan.ppm
```

The generated sheet has a 4x4 grid of equal-size barcodes. Its four columns use
these ordered palettes: `White Black`; `White Cyan Magenta Yellow`;
`White Cyan Magenta Yellow Black`; and
`White #FF0000 #00FF00 #0000FF Cyan Magenta Yellow Black`. Rows use 1x, 2x, 3x,
and 4x pixels per dot, from top to bottom. Every barcode encodes
`makocode` followed by deterministic padding sized to fill its available page
capacity. The minimum page size is 1186x1200; larger pages enlarge every
barcode equally. Higher-capacity palettes receive more padding bytes, so the
8-color mode fills the same visual area as the 1-bit mode.

Decode checks all 16 barcodes and reports the smallest pixels-per-dot setting
that decodes for each mode. Its maximum bytes-per-page estimate extrapolates
the densest successful mode and scale across the full page as flat data,
adjusted for scan scale, and ignores metadata, fiducials, and other overhead.
The PPM footer prints `width: N, height: N` in the default 5x8 footer font.

Handy linux printing command:

```
lpstat -p
lp -d HL-L3220CDW -o ppi=600 -o natural-scaling=100 -o sides=one-sided calibration.ppm
```

### Converting Scans

To turn a scanned image into a P6 `.ppm` file for decoding, install ImageMagick (`brew install imagemagick` on macOS, `sudo apt install imagemagick` on Debian/Ubuntu) and run `convert scan.png -depth 8 -compress none scan.ppm`.

### Custom Palettes (Field-Symbol Mode)

- Pass `--palette "Color ..."` to `encode`/`decode` with 2–16 unique ordered entries using the built-in names (`White`, `Cyan`, `Magenta`, `Yellow`, `Black`) or `#RRGGBB` values. Examples: `--palette "White Black"` (2 colors), `--palette "White Cyan Magenta"` (base‑3), `--palette "White Cyan Magenta Yellow Black"` (base‑5). Quote the list so the CLI keeps the whitespace intact.
- Every palette, including non-powers of two, uses eight-dot base-`q` symbols from the version-zero field profile above. The encoder fills complete symbol groups and chooses footer foreground/background colors from palette contrast.
- Palette metadata is embedded per page (via `MAKOCODE_PALETTE`, `MAKOCODE_PALETTE_BASE`, and `MAKOCODE_PAGE_SYMBOLS`), so `decode` normally discovers the palette automatically, but you can still force it with `--palette ...` if desired.
- Use `decode --recover-palette` when the embedded palette row is damaged. Recovery first tries the common ordered palettes for 2, 4, 5, and 8 colors, using colors sampled from the whole page to account for scan shifts. It then tries image-derived palette clusters and their orderings before exhaustive RGB candidates. `--recover-limit N` limits ordered palette candidates (default `4`); increase it to continue beyond the common palettes. If you already know the palette, pass the encoder-style ordered list with `--palette` to try it first.

### Standard Palette Order and Metadata Marker

The standard marker colors are fixed RGB values. For the listed palettes, bit 0 is White; bit 1 is Black for 2/5/8 colors and Yellow for 4 colors:

| Palette size | Ordered palette | Marker 0 → 1 |
| --- | --- | --- |
| 2 colors | White, Black | White → Black |
| 4 colors | White, Cyan, Magenta, Yellow | White → Yellow |
| 5 colors | White, Cyan, Magenta, Yellow, Black | White → Black |
| 8 colors | White, Red (`#FF0000`), Green (`#00FF00`), Blue (`#0000FF`), Cyan, Magenta, Yellow, Black | White → Black |

The metadata tile has a 4×8 marker (32 pixels) immediately below its palette row. It stores the `D1 BF 3F 45` bits directly: zero selects White and one selects the listed marker color. The decoder finds those colors in the stored palette row, so the marker remains Black even when the default q2 payload palette is ordered Black/White. For other palette sizes without these standard colors, the marker uses the first and last palette entries. Page payload symbols use the full ordered palette according to the page's encoding mode.

Inactive cells throughout the 48×48 metadata footprint are filled with deterministic palette noise, except for a one-cell light border around active metadata. Inactive noise stays outside payload ranks and does not change capacity or ordering.

The marker encodes the ASCII low nibbles for `MA KO CO DE`, with spaces used only for visual grouping. Read the eight letters in order, most-significant bit first within each nibble, across each row from left to right. The nibbles are `D 1 B F 3 F 4 5`, giving these four rows:

```text
11010001
10111111
00111111
01000101
```

The complete marker bit pattern is `D1 BF 3F 45`. It represents the ASCII low nibbles, not the full ASCII bytes.

## Sample Barcodes

The payload `makocode` encoded by regression fixtures 0001–0004. Fixture 0001 is the compact, marker-free q2 sample; fixture 0130 separately covers default q2 ECC and its metadata marker. Fixtures 0002–0004 show the metadata marker using the maximum palette color. Each PNG is a preview linked to the copied PPM output:

<table>
  <tr><th>2 colors</th><th>4 colors</th><th>5 colors</th><th>8 colors</th></tr>
  <tr>
    <td><a href="images/0001_makocode_2_colors.ppm"><img alt="0001: compact marker-free makocode encoding with 2 colors" src="images/0001_makocode_2_colors.png" width="192" style="image-rendering: pixelated; image-rendering: crisp-edges;"></a></td>
    <td><a href="images/0002_makocode_4_colors.ppm"><img alt="0002: makocode encoded with 4 colors" src="images/0002_makocode_4_colors.png" width="192" style="image-rendering: pixelated; image-rendering: crisp-edges;"></a></td>
    <td><a href="images/0003_makocode_5_colors.ppm"><img alt="0003: makocode encoded with 5 colors" src="images/0003_makocode_5_colors.png" width="192" style="image-rendering: pixelated; image-rendering: crisp-edges;"></a></td>
    <td><a href="images/0004_makocode_8_colors.ppm"><img alt="0004: makocode encoded with 8 colors" src="images/0004_makocode_8_colors.png" width="192" style="image-rendering: pixelated; image-rendering: crisp-edges;"></a></td>
  </tr>
</table>
