# MakoCode🐾

A standard for color barcode printing with high density, designed for long-term archiving on paper or film. Self-contained clean-room implementation with no dependencies, single file.

Stubborn and dense as a Shiba, with no fluff. Lovingly named after my Shiba Inu, Mako 🐕.

Written entirely with codex-gpt-5.

## Usage

```
make
make test
./makocode encode --input=file.txt
./makocode encode --input=file.txt --palette "White Black"
# Print, store, scan the output shown above
./makocode decode scan.ppm
```

Pass `--debug` (e.g., `./makocode --debug encode ...`) if you need the verbose diagnostic logs that were previously always emitted.

## Tests

`make test` now drives the CLI end-to-end through the shell scripts in `scripts/`
(round-trip, payload suite, overlay, CLI output-dir, and decode failure cases).
Artifacts for debugging are written to `test/` using the labels defined in each
script (e.g., `3001_random_payload*`, `2005_payload_gray_100k_*`).

### Converting Scans

To turn a scanned image into a `.ppm` file for decoding, install ImageMagick (`brew install imagemagick` on macOS, `sudo apt install imagemagick` on Debian/Ubuntu) and run `convert scan.png scan.ppm`.

### Custom Palettes (Base-N Mode)

- Pass `--palette "Color ..."` to `encode`/`decode` with 2–16 unique entries selected from the existing names (`White`, `Cyan`, `Magenta`, `Yellow`, `Black`). Examples: `--palette "White Black"` (binary), `--palette "White Cyan Magenta"` (base‑3), `--palette "White Cyan Magenta Yellow Black"` (base‑5). Quote the list so the CLI keeps the whitespace intact.
- The encoder automatically maps each page’s bitstream into base‑N digits (where `N` equals the palette length), pads out every non-reserved pixel, and chooses footer foreground/background colors by measuring palette contrast—no need for fixed White/Black endpoints.
- Palette metadata is embedded per page (via `MAKOCODE_PALETTE`, `MAKOCODE_PALETTE_BASE`, and `MAKOCODE_PAGE_SYMBOLS`), so `decode` normally discovers the palette automatically, but you can still force it with `--palette ...` if desired.

## Sample Barcodes

Examples of the encoded output for channel colors 1-3:

<img alt="Color 1 barcode" src="images/data_s1_c1_p01_encoded.png">
<img alt="Color 2 barcode" src="images/data_s1_c2_p01_encoded.png">
<img alt="Color 3 barcode" src="images/data_s1_c3_p01_encoded.png">
