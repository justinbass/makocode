# MakoCode🐾

A standard for color barcode printing with high density, designed for long-term archiving on paper or film. Self-contained clean-room implementation with no dependencies, single file.

Stubborn and dense as a Shiba, with no fluff. Lovingly named after my Shiba Inu, Mako 🐕.

Written entirely with codex-gpt-5.

## Usage

```
make
make test
./makocode encode --input=file.txt
# Print, store, scan the output shown above
./makocode decode scan.ppm
```

### Converting Scans

To turn a scanned image into a `.ppm` file for decoding, install ImageMagick (`brew install imagemagick` on macOS, `sudo apt install imagemagick` on Debian/Ubuntu) and run `convert scan.png scan.ppm`.

## Sample Barcodes

Examples of the encoded output for channel colors 1-3:

<img alt="Color 1 barcode" src="images/data_s1_c1_p01_encoded.png">
<img alt="Color 2 barcode" src="images/data_s1_c2_p01_encoded.png">
<img alt="Color 3 barcode" src="images/data_s1_c3_p01_encoded.png">
