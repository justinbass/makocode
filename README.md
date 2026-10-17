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
```

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
parallel with GNU Make, for example `make -j test` to allow up to four cases
at once.

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
