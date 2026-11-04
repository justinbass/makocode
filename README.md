# MakoCode🐾

A standard for color barcode printing with high density, designed for long-term archiving on paper or film. Self-contained clean-room implementation with no dependencies, single file.

Stubborn and dense as a Shiba, with no fluff. Lovingly named after my Shiba Inu, Mako 🐕, and about as silly as him.

Written entirely with codex-gpt-5.

## License

Copyright © 2026 Justin Alan Bass. MakoCode is licensed under the GNU Affero
General Public License, version 3 or later. See [LICENSE](LICENSE) for the
complete license text.

## Dependencies

Totally standalone for future-proofing, c++ and g++ required for compiling. CLI tests require bash.

## Usage

```
make
make test
./makocode encode --input=file.txt
./makocode encode --input=file.txt --palette "White Black"
# Print, store, scan the output shown above
./makocode decode scan.ppm
# To write one decoded file instead:
./makocode decode --decoded-file scan.ppm
./makocode decode --decoded-stdout scan.ppm
```

Decode restores the original file tree by default. `--decoded-file` writes a timestamped `makocode_decoded_YYYYMMDDHHMMSS.txt` containing page and payload metadata and escaped archive entries. `--decoded-stdout` prints the same content without creating files; it cannot be combined with `--decoded-file` or `--debug`. Archive paths cannot be absolute or contain `..`. Extraction may follow symlinks in the output directory; use a fresh, trusted directory for untrusted input.

The decoded file uses ASCII lines, begins with `MAKOCODE-DECODED`, and ends with `end`. Each `page N` section contains `metadata NAME VALUE` lines and ends with `end-page`. The `payload` section uses the same metadata form and ends with `end-payload`. Byte arrays in metadata are uppercase hexadecimal; numeric fields are decimal. Fields are present only when recovered. Page sections report the recovered metadata fields without the raw tile header or tile bytes.

The `archive MKARCH01` section gives `entries COUNT`, followed by `directory PATH` and `file PATH SIZE` entries in archive order, and ends with `end-archive`. A file's next line is its complete content; `SIZE` is its original byte count. Backslash, nonprintable bytes, and spaces in paths are encoded as `\xHH` with two uppercase hexadecimal digits. Content spaces are literal. All lines end in LF; even an empty file has a content line. Escaping applies to bytes, so UTF-8 and binary contents can be reconstructed exactly. Footer text is rendered in the image and is not recovered into the decoded file.

## Tests

`make test` now drives the CLI end-to-end through the shell scripts in `test/`
(round-trip, payload suite, overlay, CLI output-dir, and decode failure cases).
Test artifacts are written to `test_output/` using the labels defined in each
case registration (e.g., `0002_baseline_8k_ecc_050_*`,
`0015_gray_100k_single_*`).
After a passing run, final round-trip images (transformed where applicable) and
the CLI output image remain in `test_output/` and are linked from `test_output/visual/` with
relative symlinks.

Test cases are registered in `test/test_cases.sh`. Run independent cases in
parallel with GNU Make using `make -j test`. Each completed case prints one
result line; its full output is saved in `test_output/test_logs/`. Use `make test TEST_VERBOSE=1`
to stream full output instead.

Each `make test` run prints its master seed and writes it to `test_output/test_seed.txt`.
Repeat a randomized run with the same seed using:

```
MAKO_TEST_SEED=$(cat test_output/test_seed.txt) make -j test
```

You can also choose a seed explicitly, for example `MAKO_TEST_SEED=12345 make -j test`.
Random payload seeds are derived from the master seed and test label, so each case
gets a stable payload when replayed.

### Coverage

To run the same end-to-end suite with GCC/gcov instrumentation and produce an
HTML coverage report, install `lcov` (which also ships `genhtml`) and run:

```
make coverage
```

This rebuilds `makocode` with `--coverage -O0 -g`, executes every case in
`test/run_test_suite.sh`, and writes `test_output/makocode.info` plus an HTML
dashboard under `test_output/coverage/index.html`.

### Calibration

```
python3 scripts/calibration.py encode --width 2398 --height 2957
```

```
python3 scripts/calibration.py decode scan.ppm
```

The generated sheet has a 4x4 grid of equal-size barcodes. The four columns use
White/Black; White/Cyan/Magenta/Yellow; that palette plus Black; and the
eight-color White/Red/Green/Blue/Yellow/Magenta/Cyan/Black palette. Rows use
1x, 2x, 3x, and 4x pixels per dot, from top to bottom. Every barcode encodes
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

To turn a scanned image into a P3 `.ppm` file for decoding, install ImageMagick (`brew install imagemagick` on macOS, `sudo apt install imagemagick` on Debian/Ubuntu) and run `convert scan.png -compress none scan.ppm`.

### Custom Palettes (Base-N Mode)

- Pass `--palette "Color ..."` to `encode`/`decode` with 2–16 unique entries selected from the existing names (`White`, `Cyan`, `Magenta`, `Yellow`, `Black`). Examples: `--palette "White Black"` (binary), `--palette "White Cyan Magenta"` (base‑3), `--palette "White Cyan Magenta Yellow Black"` (base‑5). Quote the list so the CLI keeps the whitespace intact.
- The encoder automatically maps each page’s bitstream into base‑N digits (where `N` equals the palette length), pads out every non-reserved pixel, and chooses footer foreground/background colors by measuring palette contrast—no need for fixed White/Black endpoints.
- Palette metadata is embedded per page (via `MAKOCODE_PALETTE`, `MAKOCODE_PALETTE_BASE`, and `MAKOCODE_PAGE_SYMBOLS`), so `decode` normally discovers the palette automatically, but you can still force it with `--palette ...` if desired.

## Sample Barcodes

Examples of the encoded output for 2, 4, and 8 colors:

<img alt="Color 1 barcode" src="images/one_color_encoded.png">
<img alt="Color 2 barcode" src="images/two_colors_encoded.png">
<img alt="Color 3 barcode" src="images/three_colors_encoded.png">
