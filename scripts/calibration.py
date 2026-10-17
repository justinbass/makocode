#!/usr/bin/env python3
# Copyright (C) 2026 Justin Alan Bass.
# This file is part of MakoCode. MakoCode is free software: you can
# redistribute it and/or modify it under the terms of the GNU Affero
# General Public License as published by the Free Software Foundation,
# either version 3 of the License, or (at your option) any later version.
# MakoCode is distributed without any warranty; without even the implied
# warranty of merchantability or fitness for a particular purpose. See the
# LICENSE file for the full license text.

"""Build a labeled calibration sheet or measure decode success from its scan."""

import argparse
from pathlib import Path
import subprocess
import sys
import tempfile


FONT = {
    "0": ("01110", "10001", "10011", "10101", "11001", "10001", "01110"),
    "1": ("00100", "01100", "00100", "00100", "00100", "00100", "01110"),
    "2": ("01110", "10001", "00001", "00010", "00100", "01000", "11111"),
    "3": ("11110", "00001", "00001", "01110", "00001", "00001", "11110"),
    "4": ("00010", "00110", "01010", "10010", "11111", "00010", "00010"),
    "5": ("11111", "10000", "10000", "11110", "00001", "00001", "11110"),
    "6": ("01110", "10000", "10000", "11110", "10001", "10001", "01110"),
    "7": ("11111", "00001", "00010", "00100", "01000", "01000", "01000"),
    "8": ("01110", "10001", "10001", "01110", "10001", "10001", "01110"),
    "9": ("01110", "10001", "10001", "01111", "00001", "00001", "01110"),
    ".": ("00000", "00000", "00000", "00000", "00000", "00110", "00110"),
    "x": ("00000", "00000", "10001", "01010", "00100", "01010", "10001"),
}

LABEL_SCALE = 3
LABEL_HEIGHT = 7 * LABEL_SCALE
LABEL_COLUMN_WIDTH = 72
LABEL_GAP = 12
MARGIN = 24
PADDING = 12


def read_p3_ppm(path):
    def tokens_from_file():
        with path.open("rb") as input_file:
            for line in input_file:
                yield from line.partition(b"#")[0].split()

    tokens = iter(tokens_from_file())
    try:
        magic = next(tokens)
        header = [next(tokens), next(tokens), next(tokens)]
    except StopIteration as exc:
        raise ValueError("PPM header is incomplete") from exc
    if magic != b"P3":
        raise ValueError("input must be an ASCII P3 PPM")
    try:
        width, height, max_value = (int(token) for token in header)
    except ValueError as exc:
        raise ValueError("PPM header has invalid dimensions or max value") from exc

    if width <= 0 or height <= 0 or max_value <= 0 or max_value > 65535:
        raise ValueError("PPM dimensions and max value must be positive (max value <= 65535)")
    expected = width * height * 3

    pixels = bytearray()
    for token in tokens:
        if len(pixels) >= expected:
            raise ValueError(f"PPM has more than {expected} color values")
        try:
            channel = int(token)
        except ValueError as exc:
            raise ValueError("PPM contains an invalid color value") from exc
        if channel < 0 or channel > max_value:
            raise ValueError("PPM channel value is outside its declared range")
        pixels.append((channel * 255 + max_value // 2) // max_value)
    if len(pixels) != expected:
        raise ValueError(f"PPM has {len(pixels)} color values; expected {expected}")

    return width, height, pixels


def scaled_dimension(source_dimension, scale_half_steps):
    # scale_half_steps=3 means 1.5x. Round half up to retain the requested size.
    return (source_dimension * scale_half_steps + 1) // 2


def scale_label(scale_half_steps):
    if scale_half_steps % 2 == 0:
        return f"{scale_half_steps // 2}x"
    return f"{scale_half_steps // 2}.5x"


def make_rows(source_width, source_height, page_width, page_height):
    rows = []
    samples = []
    row_y = MARGIN
    scale_half_steps = 2
    usable_right = page_width - MARGIN
    first_barcode_x = MARGIN + LABEL_COLUMN_WIDTH + LABEL_GAP

    while True:
        barcode_width = scaled_dimension(source_width, scale_half_steps)
        barcode_height = scaled_dimension(source_height, scale_half_steps)
        row_height = max(barcode_height, LABEL_HEIGHT)
        if row_y + row_height > page_height - MARGIN:
            break
        if first_barcode_x + barcode_width > usable_right:
            break

        count = (usable_right - first_barcode_x + PADDING) // (barcode_width + PADDING)
        row_samples = []
        for copy_index in range(count):
            x = first_barcode_x + copy_index * (barcode_width + PADDING)
            sample = {
                "index": len(samples),
                "copy": copy_index,
                "x": x,
                "y": row_y + (row_height - barcode_height) // 2,
                "width_px": barcode_width,
                "height_px": barcode_height,
                "scale": scale_half_steps / 2,
            }
            samples.append(sample)
            row_samples.append(sample["index"])

        rows.append({
            "index": len(rows),
            "scale": scale_half_steps / 2,
            "label": scale_label(scale_half_steps),
            "y": row_y,
            "height_px": row_height,
            "sample_indexes": row_samples,
        })
        row_y += row_height + PADDING
        scale_half_steps += 1

    return rows, samples


def write_sheet(path, source_width, source_height, source_pixels,
                page_width, page_height, rows, samples):
    # Row placement is based on scale bands. Keep each band indexed by its top
    # pixel so rendering can paint just the barcodes intersecting each scanline.
    row_samples = []
    for row in rows:
        row_samples.append([samples[index] for index in row["sample_indexes"]])

    with path.open("w", encoding="ascii", newline="\n") as output:
        output.write("P3\n")
        output.write("# MakoCode calibration sheet\n")
        output.write(f"{page_width} {page_height}\n255\n")

        for page_y in range(page_height):
            pixels = bytearray([255]) * (page_width * 3)
            for row, row_items in zip(rows, row_samples):
                barcode_y = row_items[0]["y"] if row_items else row["y"]
                barcode_height = row_items[0]["height_px"] if row_items else 0
                if barcode_height == 0 or not (barcode_y <= page_y < barcode_y + barcode_height):
                    continue

                for sample in row_items:
                    source_y = (page_y - sample["y"]) * source_height // sample["height_px"]
                    source_y = min(source_y, source_height - 1)
                    for dx in range(sample["width_px"]):
                        source_x = dx * source_width // sample["width_px"]
                        source_offset = (source_y * source_width + source_x) * 3
                        page_offset = (sample["x"] + dx) * 3
                        pixels[page_offset:page_offset + 3] = source_pixels[source_offset:source_offset + 3]

            for row in rows:
                if row["y"] <= page_y < row["y"] + row["height_px"]:
                    label_y = row["y"] + (row["height_px"] - LABEL_HEIGHT) // 2
                    if label_y <= page_y < label_y + LABEL_HEIGHT:
                        # Draw only the glyphs that land on this scanline.
                        glyph_row = (page_y - label_y) // LABEL_SCALE
                        for char_index, char in enumerate(row["label"]):
                            char_x = MARGIN + char_index * 6 * LABEL_SCALE
                            for glyph_x, bit in enumerate(FONT[char][glyph_row]):
                                if bit == "1":
                                    for dx in range(LABEL_SCALE):
                                        px = char_x + glyph_x * LABEL_SCALE + dx
                                        offset = (px * 3)
                                        pixels[offset:offset + 3] = b"\x00\x00\x00"

            output.write(" ".join(str(channel) for channel in pixels) + "\n")


def write_sample_crop(path, page_width, page_pixels, sample):
    crop_width = sample["width_px"]
    crop_height = sample["height_px"]
    crop_x = sample["x"]
    crop_y = sample["y"]
    with path.open("w", encoding="ascii", newline="\n") as output:
        output.write(f"P3\n{crop_width} {crop_height}\n255\n")
        for y in range(crop_y, crop_y + crop_height):
            start = (y * page_width + crop_x) * 3
            end = start + crop_width * 3
            output.write(" ".join(str(channel) for channel in page_pixels[start:end]) + "\n")


def map_coordinate(value, scan_size, original_size):
    return (value * scan_size + original_size // 2) // original_size


def map_sample_to_scan(sample, scan_width, scan_height, original_width, original_height):
    mapped = dict(sample)
    mapped["x"] = map_coordinate(sample["x"], scan_width, original_width)
    mapped["y"] = map_coordinate(sample["y"], scan_height, original_height)
    mapped["width_px"] = map_coordinate(sample["width_px"], scan_width, original_width)
    mapped["height_px"] = map_coordinate(sample["height_px"], scan_height, original_height)
    if mapped["width_px"] <= 0 or mapped["height_px"] <= 0:
        raise ValueError("scan resolution is too low to crop every expected barcode")
    return mapped


def analyze_scan(makocode_bin, scan_path, original_width, original_height):
    page_width, page_height, page_pixels = read_p3_ppm(scan_path)
    rows, samples = make_rows(55, 55, original_width, original_height)
    if not rows:
        raise ValueError("original page dimensions are too small to contain a calibration sample")

    results = []
    total_samples = len(samples)
    with tempfile.TemporaryDirectory(prefix="makocode-calibration-decode-") as temporary_directory:
        work_dir = Path(temporary_directory)
        for row_index, row in enumerate(rows, start=1):
            decoded_count = 0
            row_samples = [samples[index] for index in row["sample_indexes"]]
            for original_sample in row_samples:
                sample = map_sample_to_scan(
                    original_sample, page_width, page_height, original_width, original_height
                )
                sample_dir = work_dir / f"sample-{sample['index']:04d}"
                sample_dir.mkdir()
                crop_path = sample_dir / "sample.ppm"
                decode_dir = sample_dir / "decoded"
                decode_dir.mkdir()
                write_sample_crop(crop_path, page_width, page_pixels, sample)

                print(
                    f"Decoding {row['scale']:g}x row {row_index}/{len(rows)}, "
                    f"copy {sample['copy'] + 1}/{len(row_samples)} "
                    f"(sample {sample['index'] + 1}/{total_samples})...",
                    end="",
                    file=sys.stderr,
                    flush=True,
                )
                result = subprocess.run(
                    [str(makocode_bin), "decode", f"--output-dir={decode_dir}", str(crop_path)],
                    cwd=makocode_bin.parent,
                    text=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                )
                decoded = (result.returncode == 0 and any(
                    payload.is_file() and payload.read_bytes() == b"makocode"
                    for payload in decode_dir.rglob("makocode.bin")
                ))
                print(" decoded" if decoded else " failed", file=sys.stderr, flush=True)
                if decoded:
                    decoded_count += 1

            results.append((row, decoded_count, len(row_samples)))
            if decoded_count == len(row_samples):
                print(
                    f"Every copy decoded at {row['scale']:g}x; stopping at this row.",
                    file=sys.stderr,
                    flush=True,
                )
                break

    for row, decoded_count, sample_count in results:
        percent = 100.0 * decoded_count / sample_count if sample_count else 0.0
        print(f"{row['scale']:g}x: {decoded_count}/{sample_count} decoded ({percent:.1f}%)")

    every_copy = next((row for row, count, total in results if count == total), None)
    print("Smallest scale that worked (every copy decoded): "
          + (f"{every_copy['scale']:g}x" if every_copy else "none"))


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)

    default_output = Path(__file__).resolve().with_name("calibration.ppm")
    encode_parser = commands.add_parser("encode", help="create a calibration sheet")
    encode_parser.add_argument("--width", type=int, required=True,
                               help="calibration page width in pixels")
    encode_parser.add_argument("--height", type=int, required=True,
                               help="calibration page height in pixels")
    encode_parser.add_argument("--output", type=Path, default=default_output,
                               help=f"output PPM (default: {default_output})")

    decode_parser = commands.add_parser("decode", help="measure decode success from a scan")
    decode_parser.add_argument("scan", type=Path, metavar="SCAN.ppm",
                               help="scanned calibration sheet as a P3 PPM")
    decode_parser.add_argument("--width", type=int, required=True,
                               help="original calibration page width in pixels")
    decode_parser.add_argument("--height", type=int, required=True,
                               help="original calibration page height in pixels")
    return parser.parse_args(argv)


def encode_source_ppm(makocode_bin, work_dir):
    payload_path = work_dir / "makocode.bin"
    payload_path.write_bytes(b"makocode")
    command = [
        str(makocode_bin),
        "encode",
        f"--input={payload_path.name}",
        "--ecc=0",
        "--page-width=55",
        "--page-height=55",
        "--no-filename",
        "--no-page-count",
        "--prefix=calibration_source",
        f"--output-dir={work_dir}",
    ]
    try:
        subprocess.run(
            command,
            cwd=work_dir,
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except subprocess.CalledProcessError as exc:
        detail = (exc.stderr or exc.stdout or "").strip()
        suffix = f": {detail}" if detail else ""
        raise ValueError(f"makocode encode failed with exit code {exc.returncode}{suffix}") from exc

    source_path = work_dir / "calibration_source.ppm"
    if not source_path.is_file():
        raise ValueError("makocode encode completed without creating calibration_source.ppm")
    return source_path


def main(argv=None):
    args = parse_args(argv)
    repo_root = Path(__file__).resolve().parent.parent
    makocode_bin = repo_root / "makocode"

    if not makocode_bin.is_file():
        raise ValueError(f"makocode executable not found at {makocode_bin}; build it first")

    if args.command == "decode":
        if args.width <= 0 or args.height <= 0:
            raise ValueError("original page width and height must be positive")
        analyze_scan(makocode_bin, args.scan, args.width, args.height)
        return 0

    if args.width <= 0 or args.height <= 0:
        raise ValueError("page width and height must be positive")
    if args.width <= 2 * MARGIN or args.height <= 2 * MARGIN:
        raise ValueError("page dimensions must leave room inside the margins")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="makocode-calibration-") as temporary_directory:
        source_path = encode_source_ppm(makocode_bin, Path(temporary_directory))
        source_width, source_height, source_pixels = read_p3_ppm(source_path)
        rows, samples = make_rows(source_width, source_height, args.width, args.height)
        if not rows:
            raise ValueError("page is too small to fit a labeled sample at 1x")

        write_sheet(
            args.output, source_width, source_height, source_pixels,
            args.width, args.height, rows, samples,
        )

    print(f"Wrote {args.output} ({args.width}x{args.height})")
    print(f"Placed {len(rows)} scales in {len(samples)} samples")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as exc:
        print(f"calibration: {exc}", file=sys.stderr)
        raise SystemExit(2)
