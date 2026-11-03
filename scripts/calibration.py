#!/usr/bin/env python3
# Copyright (C) 2026 Justin Alan Bass.
# This file is part of MakoCode. MakoCode is free software: you can
# redistribute it and/or modify it under the terms of the GNU Affero
# General Public License as published by the Free Software Foundation,
# either version 3 of the License, or (at your option) any later version.
# MakoCode is distributed without any warranty; without even the implied
# warranty of merchantability or fitness for a particular purpose. See the
# LICENSE file for the full license text.

"""Build a calibration sheet or measure decode success from its scan."""

import argparse
import math
from pathlib import Path
import random
import subprocess
import sys
import tempfile


CALIBRATION_MODES = (
    ("BW", ("White", "Black")),
    ("CMYK", ("White", "Cyan", "Magenta", "Yellow")),
    ("CMYKW", ("White", "Cyan", "Magenta", "Yellow", "Black")),
    ("RGBCMYKB", (
        "FFFFFF", "FF0000", "00FF00", "0000FF",
        "FFFF00", "FF00FF", "00FFFF", "000000",
    )),
)
SCALE_DENOMINATORS = (1, 2, 3, 4)
# Keep the encoder's source pages close to the generated barcode bounds. The
# four-color (2-bit) palette needs a little more room than the denser palettes.
SOURCE_PAGE_SIZES = {
    "BW": (123, 123),
    "CMYK": (93, 93),
    "CMYKW": (93, 93),
    "RGBCMYKB": (85, 85),
}
MARGIN = 24
PADDING = 12
FOOTER_GLYPH_WIDTH = 5
FOOTER_GLYPH_HEIGHT = 8
FOOTER_CHAR_SPACING = 1

# The glyphs used by the calibration footer, copied from makocode.cpp's
# default 5x8 footer font.
FOOTER_GLYPHS = {
    " ": ("00000", "00000", "00000", "00000", "00000", "00000", "00000", "00000"),
    ",": ("00000", "00000", "00000", "00000", "00100", "00100", "01000", "00000"),
    ":": ("00000", "00100", "00000", "00000", "00100", "00000", "00000", "00000"),
    "0": ("01110", "10001", "11001", "10101", "10011", "10001", "01110", "00000"),
    "1": ("00100", "01100", "00100", "00100", "00100", "00100", "01110", "00000"),
    "2": ("01110", "10001", "00001", "00010", "00100", "01000", "11111", "00000"),
    "3": ("01110", "10001", "00001", "00110", "00001", "10001", "01110", "00000"),
    "4": ("00010", "00110", "01010", "10010", "11111", "00010", "00010", "00000"),
    "5": ("11111", "10000", "11110", "00001", "00001", "10001", "01110", "00000"),
    "6": ("01110", "10000", "11110", "10001", "10001", "10001", "01110", "00000"),
    "7": ("11111", "00001", "00010", "00100", "01000", "01000", "01000", "00000"),
    "8": ("01110", "10001", "10001", "01110", "10001", "10001", "01110", "00000"),
    "9": ("01110", "10001", "10001", "01111", "00001", "00001", "01110", "00000"),
    "d": ("00001", "00001", "01111", "10001", "10001", "10001", "01111", "00000"),
    "e": ("00000", "00000", "01110", "10001", "11111", "10000", "01110", "00000"),
    "g": ("00000", "01110", "10001", "10001", "01111", "00001", "01110", "00000"),
    "h": ("10000", "10000", "11110", "10001", "10001", "10001", "10001", "00000"),
    "i": ("00100", "00000", "01100", "00100", "00100", "00100", "01110", "00000"),
    "t": ("01000", "01000", "11110", "01000", "01000", "01001", "00110", "00000"),
    "w": ("00000", "00000", "10001", "10001", "10101", "11011", "10001", "00000"),
}

calibration_payload = bytearray(b"makocode")
calibration_prng_state = 0xC0FFEE
for _ in range(1024):
    calibration_prng_state ^= (calibration_prng_state << 13) & 0xFFFFFFFF
    calibration_prng_state ^= calibration_prng_state >> 17
    calibration_prng_state ^= (calibration_prng_state << 5) & 0xFFFFFFFF
    calibration_prng_state &= 0xFFFFFFFF
    calibration_payload.append(calibration_prng_state & 0xFF)
CALIBRATION_PAYLOAD = bytes(calibration_payload)


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


def scaled_dimension(source_dimension, scale_twelfths):
    return (source_dimension * scale_twelfths + 6) // 12


def scale_label(scale_twelfths):
    whole, remainder = divmod(scale_twelfths, 12)
    if remainder == 0:
        return f"{whole}x"
    if remainder == 6:
        return f"{whole}.5x"
    fraction = {3: "1/4", 4: "1/3", 8: "2/3", 9: "3/4"}[remainder]
    return f"{whole}+{fraction}x"


def make_scale_steps(denominator):
    scale_steps = {12, 24, 36, 48}
    for fraction_denominator in range(2, denominator + 1):
        for numerator in range(1, fraction_denominator):
            if math.gcd(numerator, fraction_denominator) != 1:
                continue
            fraction_twelfths = 12 * numerator // fraction_denominator
            for whole in range(1, 4):
                scale_steps.add(whole * 12 + fraction_twelfths)
    return tuple(sorted(scale_steps))


def make_columns(source_sizes, scale_steps, page_width, page_height):
    """Pack samples with repeatable MaxRects best-short-side-fit searches."""
    samples = []
    columns = [{"index": mode_index, "sample_indexes": []}
               for mode_index in range(len(source_sizes))]
    for mode_index, (source_width, source_height) in enumerate(source_sizes):
        scale_order = scale_steps if mode_index % 2 == 0 else tuple(reversed(scale_steps))
        for scale_twelfths in scale_order:
            sample = {
                "index": len(samples),
                "mode": mode_index,
                "x": 0,
                "y": 0,
                "width_px": scaled_dimension(source_width, scale_twelfths),
                "height_px": scaled_dimension(source_height, scale_twelfths),
                "scale_twelfths": scale_twelfths,
                "scale": scale_twelfths / 12,
            }
            samples.append(sample)
            columns[mode_index]["sample_indexes"].append(sample["index"])

    bin_width = page_width - 2 * MARGIN
    bin_height = page_height - 2 * MARGIN - FOOTER_GLYPH_HEIGHT - PADDING
    if bin_width <= 0 or bin_height <= 0:
        return columns, samples, page_width + 1, page_height + 1
    packed_items = [
        (sample["index"], sample["width_px"] + PADDING,
         sample["height_px"] + PADDING)
        for sample in samples
    ]
    best_layout = None
    for seed in range(32):
        ordered_items = packed_items[:]
        random.Random(0xCA1B + seed).shuffle(ordered_items)
        ordered_items.sort(key=lambda item: item[1] * item[2], reverse=True)
        free_rectangles = [(0, 0, bin_width, bin_height)]
        placements = {}
        for sample_index, item_width, item_height in ordered_items:
            best_choice = None
            for free_index, (free_x, free_y, free_width, free_height) in enumerate(free_rectangles):
                if item_width > free_width or item_height > free_height:
                    continue
                remaining_width = free_width - item_width
                remaining_height = free_height - item_height
                score = (min(remaining_width, remaining_height),
                         max(remaining_width, remaining_height), free_y, free_x)
                if best_choice is None or score < best_choice[0]:
                    best_choice = (score, free_index, free_x, free_y)
            if best_choice is None:
                break

            _, _, placed_x, placed_y = best_choice
            placements[sample_index] = (placed_x, placed_y, item_width, item_height)
            split_rectangles = []
            for free_x, free_y, free_width, free_height in free_rectangles:
                if (placed_x >= free_x + free_width or placed_x + item_width <= free_x
                        or placed_y >= free_y + free_height or placed_y + item_height <= free_y):
                    split_rectangles.append((free_x, free_y, free_width, free_height))
                    continue
                if placed_x > free_x:
                    split_rectangles.append((free_x, free_y, placed_x - free_x, free_height))
                if placed_x + item_width < free_x + free_width:
                    split_rectangles.append((placed_x + item_width, free_y,
                                             free_x + free_width - placed_x - item_width,
                                             free_height))
                if placed_y > free_y:
                    split_rectangles.append((free_x, free_y, free_width, placed_y - free_y))
                if placed_y + item_height < free_y + free_height:
                    split_rectangles.append((free_x, placed_y + item_height, free_width,
                                             free_y + free_height - placed_y - item_height))
            free_rectangles = []
            for rectangle_index, rectangle in enumerate(split_rectangles):
                rx, ry, rw, rh = rectangle
                if any(other_index != rectangle_index
                       and rx >= ox and ry >= oy and rx + rw <= ox + ow and ry + rh <= oy + oh
                       for other_index, (ox, oy, ow, oh) in enumerate(split_rectangles)):
                    continue
                free_rectangles.append(rectangle)
        if len(placements) != len(samples):
            continue
        bounds_width = max(x + width for x, _, width, _ in placements.values())
        bounds_height = max(y + height for _, y, _, height in placements.values())
        score = (bounds_width * bounds_height, abs(bounds_width - bounds_height))
        if best_layout is None or score < best_layout[0]:
            best_layout = (score, placements)

    if best_layout is None:
        return columns, samples, page_width + 1, page_height + 1
    for sample in samples:
        placed_x, placed_y, _, _ = best_layout[1][sample["index"]]
        sample["x"] = MARGIN + placed_x + PADDING // 2
        sample["y"] = MARGIN + placed_y + PADDING // 2
    required_width = max(sample["x"] + sample["width_px"] for sample in samples) + MARGIN
    required_height = (max(sample["y"] + sample["height_px"] for sample in samples)
                       + MARGIN + FOOTER_GLYPH_HEIGHT + PADDING)
    return columns, samples, required_width, required_height


def write_p3_pixel_row(output, pixels):
    lines = (
        f"{pixels[offset]} {pixels[offset + 1]} {pixels[offset + 2]}"
        for offset in range(0, len(pixels), 3)
    )
    output.write("\n".join(lines) + "\n")


def write_sheet(path, source_images, page_width, page_height, columns, samples):
    footer_text = f"width: {page_width}, height: {page_height}"
    footer_width = (
        len(footer_text) * FOOTER_GLYPH_WIDTH
        + (len(footer_text) - 1) * FOOTER_CHAR_SPACING
    )
    footer_left = max(0, (page_width - footer_width) // 2)
    footer_top = page_height - FOOTER_GLYPH_HEIGHT

    with path.open("w", encoding="ascii", newline="\n") as output:
        output.write("P3\n")
        output.write(f"{page_width} {page_height}\n255\n")

        for page_y in range(page_height):
            pixels = bytearray([255]) * (page_width * 3)
            for column in columns:
                for sample_index in column["sample_indexes"]:
                    sample = samples[sample_index]
                    if not (sample["y"] <= page_y < sample["y"] + sample["height_px"]):
                        continue
                    source_width, source_height, source_pixels = source_images[sample["mode"]]
                    source_y = (page_y - sample["y"]) * source_height // sample["height_px"]
                    source_y = min(source_y, source_height - 1)
                    for dx in range(sample["width_px"]):
                        source_x = dx * source_width // sample["width_px"]
                        source_offset = (source_y * source_width + source_x) * 3
                        page_offset = (sample["x"] + dx) * 3
                        pixels[page_offset:page_offset + 3] = source_pixels[source_offset:source_offset + 3]

            footer_y = page_y - footer_top
            if 0 <= footer_y < FOOTER_GLYPH_HEIGHT:
                for character_index, character in enumerate(footer_text):
                    glyph_row = FOOTER_GLYPHS[character][footer_y]
                    glyph_left = footer_left + character_index * (
                        FOOTER_GLYPH_WIDTH + FOOTER_CHAR_SPACING
                    )
                    for glyph_x, bit in enumerate(glyph_row):
                        if bit == "1":
                            pixel_offset = (glyph_left + glyph_x) * 3
                            pixels[pixel_offset:pixel_offset + 3] = b"\x00\x00\x00"

            write_p3_pixel_row(output, pixels)


def write_sample_crop(path, page_width, page_height, page_pixels, sample,
                      transform, offset=(0, 0)):
    expected_x, expected_y, observed_x, observed_y, _, angle, scale_x, scale_y = transform
    cosine = math.cos(angle)
    sine = math.sin(angle)
    padding = 2
    crop_width = sample["width_px"] + padding * 2
    crop_height = sample["height_px"] + padding * 2
    with path.open("w", encoding="ascii", newline="\n") as output:
        output.write(f"P3\n{crop_width} {crop_height}\n255\n")
        for crop_y in range(crop_height):
            pixels = bytearray([255]) * (crop_width * 3)
            sample_y = crop_y - padding
            if 0 <= sample_y < sample["height_px"]:
                page_y = sample["y"] + sample_y + 0.5
                for crop_x in range(padding, crop_width - padding):
                    page_x = sample["x"] + crop_x - padding + 0.5
                    dx = page_x - expected_x
                    dy = page_y - expected_y
                    scan_x = (cosine * scale_x * dx - sine * scale_y * dy
                              + observed_x + offset[0])
                    scan_y = (sine * scale_x * dx + cosine * scale_y * dy
                              + observed_y + offset[1])
                    pixel_x = math.floor(scan_x)
                    pixel_y = math.floor(scan_y)
                    if 0 <= pixel_x < page_width and 0 <= pixel_y < page_height:
                        source_offset = (pixel_y * page_width + pixel_x) * 3
                        destination_offset = crop_x * 3
                        pixels[destination_offset:destination_offset + 3] = (
                            page_pixels[source_offset:source_offset + 3]
                        )
            write_p3_pixel_row(output, pixels)


def make_calibration_layout(source_sizes, page_width=None, page_height=None,
                            scale_denominator=None):
    denominators = ((scale_denominator,) if scale_denominator is not None
                    else tuple(reversed(SCALE_DENOMINATORS)))
    for denominator in denominators:
        columns, samples, required_width, required_height = make_columns(
            source_sizes, make_scale_steps(denominator), page_width, page_height,
        )
        if (scale_denominator is not None
                or (required_width <= page_width and required_height <= page_height)):
            return (
                columns, samples, required_width, required_height, denominator,
            )

    _, _, minimum_width, minimum_height = make_columns(
        source_sizes, make_scale_steps(SCALE_DENOMINATORS[0]), page_width, page_height,
    )
    raise ValueError(
        f"page must be at least {minimum_width}x{minimum_height} to fit "
        "the 1x, 2x, 3x, and 4x calibration samples"
    )


def fit_scan_transform(columns, samples, scan_width, scan_height, scan_pixels,
                       initial_angle=None):
    """Fit the scan geometry using its page bounds and calibration marks."""
    # Palette colors produce very different ink profiles, so fit from sample
    # bounds before refining the placement against the artwork.
    expected_x_hist = {}
    expected_y_hist = {}

    def expected_point(x, y):
        x_bin = math.floor(x)
        y_bin = math.floor(y)
        expected_x_hist[x_bin] = expected_x_hist.get(x_bin, 0) + 1
        expected_y_hist[y_bin] = expected_y_hist.get(y_bin, 0) + 1
        return x, y

    def moments(points):
        count = 0
        sum_x = sum_y = sum_xx = sum_yy = sum_xy = 0.0
        for x, y in points:
            count += 1
            sum_x += x
            sum_y += y
            sum_xx += x * x
            sum_yy += y * y
            sum_xy += x * y
        if count == 0:
            return None
        mean_x = sum_x / count
        mean_y = sum_y / count
        return (
            count,
            mean_x,
            mean_y,
            sum_xx / count - mean_x * mean_x,
            sum_yy / count - mean_y * mean_y,
            sum_xy / count - mean_x * mean_y,
        )

    def expected_ink_points():
        for sample in samples:
            for dy in range(sample["height_px"]):
                for dx in range(sample["width_px"]):
                    yield expected_point(sample["x"] + dx + 0.5,
                                         sample["y"] + dy + 0.5)

    expected_ink = moments(expected_ink_points())
    if expected_ink is None:
        raise ValueError("could not find ink in the generated calibration samples")

    # The sample bounds provide a stable large-scale orientation and size
    # estimate. Use the actual ink profiles below only to refine translation.
    expected_weight = sum(sample["width_px"] * sample["height_px"] for sample in samples)
    expected_x = sum(
        sample["width_px"] * sample["height_px"]
        * (sample["x"] + sample["width_px"] / 2)
        for sample in samples
    ) / expected_weight
    expected_y = sum(
        sample["width_px"] * sample["height_px"]
        * (sample["y"] + sample["height_px"] / 2)
        for sample in samples
    ) / expected_weight
    expected_xx = expected_yy = expected_xy = 0.0
    for sample in samples:
        width = sample["width_px"]
        height = sample["height_px"]
        weight = width * height
        dx = sample["x"] + width / 2 - expected_x
        dy = sample["y"] + height / 2 - expected_y
        expected_xx += weight * (dx * dx + width * width / 12)
        expected_yy += weight * (dy * dy + height * height / 12)
        expected_xy += weight * dx * dy
    expected_xx /= expected_weight
    expected_yy /= expected_weight
    expected_xy /= expected_weight

    # Sample at most about 1.5 million scan points. Ink is dark while paper
    # texture and scanner background are much lighter.
    step = max(1, (max(scan_width, scan_height) + 1499) // 1500)
    def observed_ink_points():
        for y in range(step // 2, scan_height, step):
            for x in range(step // 2, scan_width, step):
                offset = (y * scan_width + x) * 3
                red = scan_pixels[offset]
                green = scan_pixels[offset + 1]
                blue = scan_pixels[offset + 2]
                if (red * 299 + green * 587 + blue * 114) // 1000 <= 160:
                    yield x + 0.5, y + 0.5

    observed = moments(observed_ink_points())
    if observed is None or observed[0] < 100:
        raise ValueError("could not find the calibration marks in the scanned image")
    _, observed_x, observed_y, observed_xx, observed_yy, observed_xy = observed

    def principal_axis(xx, yy, xy):
        return 0.5 * math.atan2(2 * xy, xx - yy)

    expected_angle = principal_axis(expected_xx, expected_yy, expected_xy)
    observed_angle = principal_axis(observed_xx, observed_yy, observed_xy)
    angle = observed_angle - expected_angle
    # A principal axis describes a line, so equivalent angles differ by pi.
    angle = (angle + math.pi / 2) % math.pi - math.pi / 2
    if initial_angle is not None:
        angle = initial_angle

    expected_gap = math.hypot(expected_xx - expected_yy, 2 * expected_xy)
    observed_gap = math.hypot(observed_xx - observed_yy, 2 * observed_xy)
    if expected_gap <= 0 or observed_gap <= 0:
        raise ValueError("could not determine the calibration grid orientation")

    expected_major = (expected_xx + expected_yy + expected_gap) / 2
    expected_minor = (expected_xx + expected_yy - expected_gap) / 2
    observed_major = (observed_xx + observed_yy + observed_gap) / 2
    observed_minor = (observed_xx + observed_yy - observed_gap) / 2
    scale = math.sqrt((observed_major / expected_major
                       + observed_minor / expected_minor) / 2)
    if not math.isfinite(scale) or scale <= 0:
        raise ValueError("could not determine the calibration grid scale")

    # The ink distribution can bias the covariance angle differently at each
    # scan resolution. Estimate rotation from the page's repeated horizontal
    # and vertical structure instead.
    model_left = min(MARGIN, min(sample["x"] for sample in samples))
    model_right = max(sample["x"] + sample["width_px"] for sample in samples)
    model_top = min(sample["y"] for sample in samples)
    model_bottom = max(sample["y"] + sample["height_px"] for sample in samples)
    expected_page_width = model_right + MARGIN
    expected_page_height = model_bottom + MARGIN
    cosine = math.cos(angle)
    sine = math.sin(angle)
    abs_cosine = abs(cosine)
    abs_sine = abs(sine)
    coefficient_a = abs_cosine * expected_page_width
    coefficient_b = abs_sine * expected_page_height
    coefficient_c = abs_sine * expected_page_width
    coefficient_d = abs_cosine * expected_page_height
    determinant = coefficient_a * coefficient_d - coefficient_b * coefficient_c
    if abs(determinant) > 1e-9:
        scale_x = (scan_width * coefficient_d - coefficient_b * scan_height) / determinant
        scale_y = (coefficient_a * scan_height - scan_width * coefficient_c) / determinant
        page_scale = math.sqrt(scale_x * scale_y) if scale_x > 0 and scale_y > 0 else 0
        if (0.25 <= scale_x <= 4 and 0.25 <= scale_y <= 4
                and abs(page_scale - scale) <= 0.05 * max(page_scale, scale)):
            return (
                expected_page_width / 2, expected_page_height / 2,
                scan_width / 2, scan_height / 2, page_scale, angle,
                scale_x, scale_y,
            )

    margin = max(20, round(scale * max(model_right - model_left,
                                      model_bottom - model_top) * 0.05))
    initial_cosine = math.cos(angle)
    initial_sine = math.sin(angle)
    projected_corners = []
    for page_x in (model_left, model_right):
        for page_y in (model_top, model_bottom):
            dx = page_x - expected_x
            dy = page_y - expected_y
            projected_corners.append((
                scale * (initial_cosine * dx - initial_sine * dy) + observed_x,
                scale * (initial_sine * dx + initial_cosine * dy) + observed_y,
            ))
    left = min(point[0] for point in projected_corners) - margin
    right = max(point[0] for point in projected_corners) + margin
    top = min(point[1] for point in projected_corners) - margin
    bottom = max(point[1] for point in projected_corners) + margin
    grid_points = [
        (x, y) for x, y in observed_ink_points()
        if left <= x <= right and top <= y <= bottom
    ]
    if len(grid_points) < 100:
        raise ValueError("could not find the calibration grid structure in the scanned image")

    if initial_angle is None:
        angle_center = angle
        best_angle = angle
        best_score = -1
        for tenth_degree in range(-40, 41):
            candidate = angle_center + math.radians(tenth_degree / 10)
            candidate_cosine = math.cos(candidate)
            candidate_sine = math.sin(candidate)
            x_projection = {}
            y_projection = {}
            for x, y in grid_points:
                x_bin = round(candidate_cosine * x + candidate_sine * y)
                y_bin = round(-candidate_sine * x + candidate_cosine * y)
                x_projection[x_bin] = x_projection.get(x_bin, 0) + 1
                y_projection[y_bin] = y_projection.get(y_bin, 0) + 1
            score = (sum(count * count for count in x_projection.values())
                     + sum(count * count for count in y_projection.values()))
            if score > best_score:
                best_angle = candidate
                best_score = score
        angle = best_angle

    # Estimate independent axis scales and translation from the grid bounds.
    # Moment scales expand with dark-ink thickness, which shifts samples far
    # from the grid center, especially at high scan resolutions.
    def robust_bounds(hist):
        threshold = max(1, max(hist.values()) * 0.01)
        positions = [position for position, count in hist.items()
                     if count >= threshold]
        return min(positions), max(positions)

    expected_x_bounds = robust_bounds(expected_x_hist)
    expected_y_bounds = robust_bounds(expected_y_hist)
    observed_x_hist = {}
    observed_y_hist = {}
    cosine = math.cos(angle)
    sine = math.sin(angle)
    for scan_x, scan_y in grid_points:
        x_bin = round(cosine * scan_x + sine * scan_y)
        y_bin = round(-sine * scan_x + cosine * scan_y)
        observed_x_hist[x_bin] = observed_x_hist.get(x_bin, 0) + 1
        observed_y_hist[y_bin] = observed_y_hist.get(y_bin, 0) + 1

    observed_x_bounds = robust_bounds(observed_x_hist)
    observed_y_bounds = robust_bounds(observed_y_hist)
    scale_x = ((observed_x_bounds[1] - observed_x_bounds[0])
               / (expected_x_bounds[1] - expected_x_bounds[0]))
    scale_y = ((observed_y_bounds[1] - observed_y_bounds[0])
               / (expected_y_bounds[1] - expected_y_bounds[0]))
    if (not math.isfinite(scale_x) or not math.isfinite(scale_y)
            or scale_x <= 0 or scale_y <= 0):
        raise ValueError("could not determine the calibration grid bounds")

    expected_x = (expected_x_bounds[0] + expected_x_bounds[1]) / 2
    expected_y = (expected_y_bounds[0] + expected_y_bounds[1]) / 2
    observed_axis_x = (observed_x_bounds[0] + observed_x_bounds[1]) / 2
    observed_axis_y = (observed_y_bounds[0] + observed_y_bounds[1]) / 2
    observed_x = cosine * observed_axis_x - sine * observed_axis_y
    observed_y = sine * observed_axis_x + cosine * observed_axis_y
    reported_scale = math.sqrt(scale_x * scale_y)

    return (expected_x, expected_y, observed_x, observed_y, reported_scale,
            angle, scale_x, scale_y)


def fit_sample_row_offsets(columns, samples, transform, source_images,
                           scan_width, scan_height, scan_pixels):
    """Measure small placement errors against each sample's source bitmap."""
    expected_x, expected_y, observed_x, observed_y, scale, angle, scale_x, scale_y = transform
    cosine = math.cos(angle)
    sine = math.sin(angle)
    templates = []
    for source_width, source_height, source_pixels in source_images:
        template = []
        for source_y in range(source_height):
            for source_x in range(source_width):
                offset = (source_y * source_width + source_x) * 3
                red, green, blue = source_pixels[offset:offset + 3]
                if (red * 299 + green * 587 + blue * 114) // 1000 <= 160:
                    template.append((source_x + 0.5, source_y + 0.5))
        templates.append(template)

    offsets = {}
    for sample in samples:
        source_width, source_height, _ = source_images[sample["mode"]]
        template = templates[sample["mode"]]

        def score_offset(shift_x, shift_y):
            score = 0
            for source_x, source_y in template:
                page_x = sample["x"] + source_x * sample["width_px"] / source_width
                page_y = sample["y"] + source_y * sample["height_px"] / source_height
                dx = page_x - expected_x
                dy = page_y - expected_y
                scan_x = (cosine * scale_x * dx - sine * scale_y * dy
                          + observed_x + shift_x)
                scan_y = (sine * scale_x * dx + cosine * scale_y * dy
                          + observed_y + shift_y)
                pixel_x = math.floor(scan_x)
                pixel_y = math.floor(scan_y)
                if not (0 <= pixel_x < scan_width and 0 <= pixel_y < scan_height):
                    continue
                offset = (pixel_y * scan_width + pixel_x) * 3
                red, green, blue = scan_pixels[offset:offset + 3]
                luma = (red * 299 + green * 587 + blue * 114) // 1000
                score += 128 - luma
            return score

        radius_x = max(2, min(20, round(scale_x * PADDING)))
        radius_y = max(2, min(20, round(scale_y * PADDING)))
        coarse_step = max(1, round(scale * 2))

        best_x = best_y = 0
        best_score = score_offset(0, 0)
        for shift_y in range(-radius_y, radius_y + 1, coarse_step):
            for shift_x in range(-radius_x, radius_x + 1, coarse_step):
                score = score_offset(shift_x, shift_y)
                if score > best_score:
                    best_x, best_y, best_score = shift_x, shift_y, score

        coarse_x, coarse_y = best_x, best_y
        for shift_y in range(coarse_y - coarse_step, coarse_y + coarse_step + 1):
            for shift_x in range(coarse_x - coarse_step, coarse_x + coarse_step + 1):
                if abs(shift_x) > radius_x or abs(shift_y) > radius_y:
                    continue
                score = score_offset(shift_x, shift_y)
                if score > best_score:
                    best_x, best_y, best_score = shift_x, shift_y, score
        offsets[sample["index"]] = (best_x, best_y)
    return offsets


def infer_scale_layout(source_sizes, scan_width, scan_height, scan_pixels):
    step = max(1, (max(scan_width, scan_height) + 1499) // 1500)
    ink_points = []
    for y in range(step // 2, scan_height, step):
        for x in range(step // 2, scan_width, step):
            offset = (y * scan_width + x) * 3
            red, green, blue = scan_pixels[offset:offset + 3]
            luma = (red * 299 + green * 587 + blue * 114) // 1000
            if luma <= 160 or max(red, green, blue) - min(red, green, blue) >= 32:
                ink_points.append((x + 0.5, y + 0.5))
    if len(ink_points) < 100:
        raise ValueError("could not find enough calibration ink to identify the layout")

    def projection_score(angle):
        cosine = math.cos(angle)
        sine = math.sin(angle)
        x_projection = {}
        y_projection = {}
        for x, y in ink_points:
            x_bin = round(cosine * x + sine * y)
            y_bin = round(-sine * x + cosine * y)
            x_projection[x_bin] = x_projection.get(x_bin, 0) + 1
            y_projection[y_bin] = y_projection.get(y_bin, 0) + 1
        return (sum(count * count for count in x_projection.values())
                + sum(count * count for count in y_projection.values()))

    best_angle = 0.0
    best_score = -1
    for tenth_degree in range(-150, 151, 10):
        candidate = math.radians(tenth_degree / 10)
        score = projection_score(candidate)
        if score > best_score:
            best_angle = candidate
            best_score = score
    center_tenth_degree = round(math.degrees(best_angle) * 10)
    for tenth_degree in range(center_tenth_degree - 10, center_tenth_degree + 11):
        candidate = math.radians(tenth_degree / 10)
        score = projection_score(candidate)
        if score > best_score:
            best_angle = candidate
            best_score = score

    cosine = math.cos(best_angle)
    sine = math.sin(best_angle)
    projected_x = []
    projected_y = []
    for x, y in ink_points:
        projected_x.append(cosine * x + sine * y)
        projected_y.append(-sine * x + cosine * y)
    observed_width = max(projected_x) - min(projected_x)
    observed_height = max(projected_y) - min(projected_y)

    candidate_denominators = tuple(reversed(SCALE_DENOMINATORS))
    denominator_scores = {}
    for candidate in candidate_denominators:
        _, _, expected_width, expected_height = make_columns(
            source_sizes, make_scale_steps(candidate), scan_width, scan_height,
        )
        if expected_width > scan_width or expected_height > scan_height:
            continue
        expected_width -= 2 * MARGIN
        expected_height -= 2 * MARGIN
        predicted_height = expected_height * observed_width / expected_width
        denominator_scores[candidate] = abs(
            math.log(max(1.0, observed_height) / max(1.0, predicted_height))
        )
    if not denominator_scores:
        raise ValueError("no supported calibration layout fits the scanned image")
    denominator = min(denominator_scores, key=denominator_scores.get)
    _, _, expected_width, expected_height = make_columns(
        source_sizes, make_scale_steps(denominator), scan_width, scan_height,
    )
    observed_ratio = scan_width / scan_height
    angle_denominator = observed_ratio * expected_width - expected_height
    if abs(angle_denominator) > 1e-9:
        angle_magnitude = math.atan(
            (expected_width - observed_ratio * expected_height)
            / angle_denominator
        )
        if 0 < angle_magnitude <= math.radians(15):
            sine = math.sin(angle_magnitude)
            cosine = math.cos(angle_magnitude)
            predicted_width = expected_width * cosine + expected_height * sine
            predicted_height = expected_height * cosine + expected_width * sine
            page_scale = math.sqrt(
                (scan_width / predicted_width) * (scan_height / predicted_height)
            )
            ink_scale = math.sqrt(
                (observed_width / max(1, expected_width - 2 * MARGIN))
                * (observed_height / max(1, expected_height - 2 * MARGIN))
            )
            if abs(page_scale - ink_scale) <= 0.1 * max(page_scale, ink_scale):
                positive_score = projection_score(angle_magnitude)
                negative_score = projection_score(-angle_magnitude)
                best_angle = (angle_magnitude if positive_score >= negative_score
                              else -angle_magnitude)
    return denominator, best_angle


def fit_calibration_scan(work_dir, columns, samples, source_images,
                         expected_width, expected_height,
                         scan_width, scan_height, scan_pixels,
                         initial_angle=None):
    if scan_width >= expected_width and scan_height >= expected_height:
        reference_path = work_dir / "expected-calibration.ppm"
        write_sheet(
            reference_path, source_images, scan_width, scan_height,
            columns, samples,
        )
        _, _, reference_pixels = read_p3_ppm(reference_path)
        if scan_pixels == reference_pixels:
            center_x = scan_width / 2
            center_y = scan_height / 2
            transform = (center_x, center_y, center_x, center_y, 1.0, 0.0, 1.0, 1.0)
            offsets = {sample["index"]: (0, 0) for sample in samples}
            return transform, offsets, True

    transform = fit_scan_transform(
        columns, samples, scan_width, scan_height, scan_pixels, initial_angle,
    )
    offsets = fit_sample_row_offsets(
        columns, samples, transform, source_images,
        scan_width, scan_height, scan_pixels,
    )
    return transform, offsets, False


def analyze_scan(makocode_bin, scan_path):
    page_width, page_height, page_pixels = read_p3_ppm(scan_path)
    with tempfile.TemporaryDirectory(prefix="makocode-calibration-decode-") as temporary_directory:
        work_dir = Path(temporary_directory)
        source_images = encode_calibration_sources(makocode_bin, work_dir)
        source_sizes = [(image[0], image[1]) for image in source_images]
        scale_denominator, initial_angle = infer_scale_layout(
            source_sizes, page_width, page_height, page_pixels,
        )
        columns, samples, expected_width, expected_height, _ = make_calibration_layout(
            source_sizes, page_width, page_height, scale_denominator=scale_denominator,
        )
        transform, sample_offsets, exact_sheet = fit_calibration_scan(
            work_dir, columns, samples, source_images, expected_width, expected_height,
            page_width, page_height, page_pixels, initial_angle,
        )
        if exact_sheet:
            print(
                f"Input matches the generated sheet with 1/{scale_denominator} subdivisions; "
                "using its exact layout.",
                file=sys.stderr,
            )
        else:
            print(
                f"Detected calibration grid at {transform[4]:.3g}x, "
                f"rotated {math.degrees(transform[5]):.2f} degrees, "
                f"with 1/{scale_denominator} subdivisions.",
                file=sys.stderr,
                flush=True,
            )

        mode_results = {mode_index: [] for mode_index in range(len(CALIBRATION_MODES))}
        smallest_scales = {}
        mode_order = [len(CALIBRATION_MODES) - 1, *range(len(CALIBRATION_MODES) - 1)]
        for mode_index in mode_order:
            mode_samples = sorted(
                (sample for sample in samples if sample["mode"] == mode_index),
                key=lambda sample: sample["scale_twelfths"],
            )
            for original_sample in mode_samples:
                sample_dir = work_dir / f"sample-{original_sample['index']:04d}"
                sample_dir.mkdir()
                crop_path = sample_dir / "sample.ppm"
                decode_dir = sample_dir / "decoded"
                decode_dir.mkdir()
                write_sample_crop(
                    crop_path, page_width, page_height, page_pixels,
                    original_sample, transform,
                    sample_offsets[original_sample["index"]],
                )

                print(
                    f"{CALIBRATION_MODES[mode_index][0]} "
                    f"{scale_label(original_sample['scale_twelfths'])}...",
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
                    payload.is_file() and payload.read_bytes() == CALIBRATION_PAYLOAD
                    for payload in decode_dir.rglob("makocode.bin")
                ))
                mode_results[mode_index].append((original_sample["scale_twelfths"], decoded))
                print(" decoded" if decoded else " failed", file=sys.stderr, flush=True)
                if decoded:
                    smallest_scales[mode_index] = original_sample["scale_twelfths"]
                    break

    for mode_index, (mode_name, _) in enumerate(CALIBRATION_MODES):
        print(f"{mode_name}: " + ", ".join(
            f"{scale_label(scale_twelfths)} {'decoded' if decoded else 'failed'}"
            for scale_twelfths, decoded in mode_results[mode_index]
        ) or "no samples checked")
        print(
            f"Smallest scale that decoded for {mode_name}: "
            f"{scale_label(smallest_scales[mode_index]) if mode_index in smallest_scales else 'none'}"
        )
    preferred_mode = next(
        (mode_index for mode_index in reversed(range(len(CALIBRATION_MODES)))
         if smallest_scales.get(mode_index) == 12),
        None,
    )
    if preferred_mode is None:
        print("Preferred mode at 1x: none")
    else:
        mode_name, palette = CALIBRATION_MODES[preferred_mode]
        print(f"Preferred mode at 1x: {mode_name} ({len(palette)} colors)")
    if smallest_scales:
        best_mode = max(
            smallest_scales,
            key=lambda mode_index: math.log2(len(CALIBRATION_MODES[mode_index][1]))
            / (smallest_scales[mode_index] / 12) ** 2,
        )
        mode_name, palette = CALIBRATION_MODES[best_mode]
        scale_twelfths = smallest_scales[best_mode]
        scale = scale_twelfths / 12
        _, _, _, _, _, _, scan_scale_x, scan_scale_y = transform
        bits_per_pixel = (
            math.log2(len(palette))
            / (scale * scale * scan_scale_x * scan_scale_y)
        )
        bytes_per_page = int(page_width * page_height * bits_per_pixel / 8)
        print("Best case assumes flat data; ignores metadata, fiducials, and other overhead.")
        print(
            f"Bytes per page: {bytes_per_page} "
            f"({mode_name} at {scale_label(scale_twelfths)}, "
            f"{bits_per_pixel:.3g} bits/pixel)"
        )
    else:
        print("Bytes per page: none decoded")


def crop_scan(makocode_bin, scan_path):
    scan_width, scan_height, scan_pixels = read_p3_ppm(scan_path)
    with tempfile.TemporaryDirectory(prefix="makocode-calibration-source-") as temporary_directory:
        source_images = encode_calibration_sources(
            makocode_bin, Path(temporary_directory),
        )
        source_sizes = [(image[0], image[1]) for image in source_images]
        scale_denominator, initial_angle = infer_scale_layout(
            source_sizes, scan_width, scan_height, scan_pixels,
        )
        columns, samples, expected_width, expected_height, _ = make_calibration_layout(
            source_sizes, scan_width, scan_height, scale_denominator=scale_denominator,
        )
        transform, sample_offsets, exact_sheet = fit_calibration_scan(
            Path(temporary_directory), columns, samples, source_images,
            expected_width, expected_height, scan_width, scan_height, scan_pixels,
            initial_angle,
        )
        fit_description = "exact generated layout" if exact_sheet else (
            f"{transform[4]:.3g}x, rotated {math.degrees(transform[5]):.2f} degrees"
        )
        print(f"Bounds fit at {fit_description}")
        adjustments = ", ".join(
            f"{CALIBRATION_MODES[sample['mode']][0]} "
            f"{scale_label(sample['scale_twelfths'])} "
            f"{sample_offsets[sample['index']][0]:+d},"
            f"{sample_offsets[sample['index']][1]:+d}"
            for sample in samples
        )
        print(f"Sample position adjustments in scan pixels (x,y): {adjustments}")

        output_dir = Path.cwd() / "calibration-crops" / scan_path.stem
        output_dir.mkdir(parents=True, exist_ok=True)
        for sample in samples:
            crop_path = output_dir / (
                f"{CALIBRATION_MODES[sample['mode']][0]}-"
                f"{scale_label(sample['scale_twelfths']).replace('+', '-').replace('/', '_')}.ppm"
            )
            write_sample_crop(
                crop_path, scan_width, scan_height, scan_pixels, sample,
                transform, sample_offsets[sample["index"]],
            )
    print(f"Wrote {len(samples)} crops to {output_dir}")


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
    crop_parser = commands.add_parser("crop", help="crop calibration samples without decoding")
    crop_parser.add_argument("scan", type=Path, metavar="SCAN.ppm",
                             help="scanned calibration sheet to crop")
    return parser.parse_args(argv)


def encode_source_ppm(makocode_bin, work_dir, palette, page_width, page_height):
    payload_path = work_dir / "makocode.bin"
    payload_path.write_bytes(CALIBRATION_PAYLOAD)
    command = [
        str(makocode_bin),
        "encode",
        f"--input={payload_path.name}",
        "--ecc=0",
        f"--page-width={page_width}",
        f"--page-height={page_height}",
        "--no-filename",
        "--no-page-count",
        "--prefix=calibration_source",
        f"--output-dir={work_dir}",
        "--palette",
        " ".join(palette),
    ]
    try:
        result = subprocess.run(
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
        detail = (result.stderr or result.stdout or "").strip()
        suffix = f": {detail}" if detail else ""
        raise ValueError(
            "makocode encode completed without creating calibration_source.ppm" + suffix
        )
    return source_path


def encode_calibration_sources(makocode_bin, work_dir):
    source_images = []
    for mode_name, palette in CALIBRATION_MODES:
        mode_dir = work_dir / mode_name.lower()
        mode_dir.mkdir()
        page_width, page_height = SOURCE_PAGE_SIZES[mode_name]
        source_path = encode_source_ppm(
            makocode_bin, mode_dir, palette, page_width, page_height,
        )
        source_images.append(read_p3_ppm(source_path))
    return source_images


def main(argv=None):
    args = parse_args(argv)
    repo_root = Path(__file__).resolve().parent.parent
    makocode_bin = repo_root / "makocode"

    if not makocode_bin.is_file():
        raise ValueError(f"makocode executable not found at {makocode_bin}; build it first")

    if args.command == "crop":
        crop_scan(makocode_bin, args.scan)
        return 0

    if args.command == "decode":
        analyze_scan(makocode_bin, args.scan)
        return 0

    if args.width <= 0 or args.height <= 0:
        raise ValueError("page width and height must be positive")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="makocode-calibration-") as temporary_directory:
        source_images = encode_calibration_sources(makocode_bin, Path(temporary_directory))
        source_sizes = [(image[0], image[1]) for image in source_images]
        columns, samples, _, _, scale_denominator = make_calibration_layout(
            source_sizes, args.width, args.height,
        )
        write_sheet(
            args.output, source_images, args.width, args.height, columns, samples,
        )

    print(f"Wrote {args.output} ({args.width}x{args.height})")
    print(
        f"Placed {len(samples)} samples across {len(CALIBRATION_MODES)} color modes "
        f"using 1/{scale_denominator} scale subdivisions"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as exc:
        print(f"calibration: {exc}", file=sys.stderr)
        raise SystemExit(2)
