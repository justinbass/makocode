#!/usr/bin/env python3
import argparse
import math
import random
from pathlib import Path

EPSILON = 1e-9


def read_ppm(path: Path):
    with path.open('r', encoding='ascii') as fh:
        header = fh.readline()
        if not header or header.strip() != 'P3':
            raise SystemExit(f"ppm_transform: {path} is not an ASCII P3 PPM")
        comments = []
        tokens = []
        while len(tokens) < 3:
            line = fh.readline()
            if not line:
                raise SystemExit(f"ppm_transform: {path} truncated header")
            stripped = line.strip()
            if not stripped:
                continue
            if stripped.startswith('#'):
                comments.append(stripped)
                continue
            tokens.extend(stripped.split())
        width = int(tokens[0])
        height = int(tokens[1])
        maxval = int(tokens[2])
        if maxval != 255:
            raise SystemExit(f"ppm_transform: expected maxval 255, got {maxval}")
        pixels = []
        for line in fh:
            stripped = line.strip()
            if not stripped or stripped.startswith('#'):
                continue
            pixels.extend(int(value) for value in stripped.split())
    expected = width * height * 3
    if len(pixels) != expected:
        raise SystemExit(
            f"ppm_transform: pixel count mismatch (wanted {expected}, got {len(pixels)})"
        )
    return comments, width, height, pixels


def write_ppm(path: Path, comments, width, height, pixels):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('w', encoding='ascii') as fh:
        fh.write('P3\n')
        for line in comments:
            fh.write(f"{line}\n")
        fh.write(f"{width} {height}\n")
        fh.write('255\n')
        for idx in range(0, len(pixels), 3):
            fh.write(f"{pixels[idx]} {pixels[idx + 1]} {pixels[idx + 2]}\n")


def nearly_equal(a: float, b: float) -> bool:
    return abs(a - b) < EPSILON


def bilinear_sample(pixels, width, height, fx, fy):
    fx = min(max(fx, 0.0), width - 1.0)
    fy = min(max(fy, 0.0), height - 1.0)
    x0 = int(math.floor(fx))
    y0 = int(math.floor(fy))
    x1 = min(width - 1, x0 + 1)
    y1 = min(height - 1, y0 + 1)
    dx = fx - x0
    dy = fy - y0
    result = [0, 0, 0]
    for channel in range(3):
        idx00 = (y0 * width + x0) * 3 + channel
        idx10 = (y0 * width + x1) * 3 + channel
        idx01 = (y1 * width + x0) * 3 + channel
        idx11 = (y1 * width + x1) * 3 + channel
        top = pixels[idx00] + (pixels[idx10] - pixels[idx00]) * dx
        bottom = pixels[idx01] + (pixels[idx11] - pixels[idx01]) * dx
        value = top + (bottom - top) * dy
        value = min(255.0, max(0.0, value))
        result[channel] = int(round(value))
    return result


def scale_image(pixels, width, height, scale_x, scale_y):
    if nearly_equal(scale_x, 1.0) and nearly_equal(scale_y, 1.0):
        return width, height, pixels[:]
    new_width = max(1, int(round(width * scale_x)))
    new_height = max(1, int(round(height * scale_y)))
    inv_x = 1.0 / scale_x
    inv_y = 1.0 / scale_y
    new_pixels = [255] * (new_width * new_height * 3)
    for row in range(new_height):
        src_y = ((row + 0.5) * inv_y) - 0.5
        for col in range(new_width):
            src_x = ((col + 0.5) * inv_x) - 0.5
            sample = bilinear_sample(pixels, width, height, src_x, src_y)
            base = (row * new_width + col) * 3
            new_pixels[base: base + 3] = sample
    return new_width, new_height, new_pixels


def skew_horizontal(pixels, width, height, skew_amount):
    if nearly_equal(skew_amount, 0.0) or height == 0:
        return width, height, pixels[:]
    if height == 1:
        slope = 0.0
    else:
        slope = skew_amount / (height - 1)
    min_shift = min(0.0, skew_amount)
    max_shift = max(0.0, skew_amount)
    new_width = width + int(math.ceil(max_shift - min_shift))
    new_pixels = [255] * (new_width * height * 3)
    for y in range(height):
        shift = slope * y
        for x in range(width):
            dest_x = int(round(x + shift - min_shift))
            if dest_x < 0 or dest_x >= new_width:
                continue
            src_idx = (y * width + x) * 3
            dest_idx = (y * new_width + dest_x) * 3
            new_pixels[dest_idx:dest_idx + 3] = pixels[src_idx:src_idx + 3]
    return new_width, height, new_pixels


def skew_vertical(pixels, width, height, skew_amount):
    if nearly_equal(skew_amount, 0.0) or width == 0:
        return width, height, pixels[:]
    if width == 1:
        slope = 0.0
    else:
        slope = skew_amount / (width - 1)
    min_shift = min(0.0, skew_amount)
    max_shift = max(0.0, skew_amount)
    new_height = height + int(math.ceil(max_shift - min_shift))
    new_pixels = [255] * (new_height * width * 3)
    for x in range(width):
        shift = slope * x
        for y in range(height):
            dest_y = int(round(y + shift - min_shift))
            if dest_y < 0 or dest_y >= new_height:
                continue
            src_idx = (y * width + x) * 3
            dest_idx = (dest_y * width + x) * 3
            new_pixels[dest_idx:dest_idx + 3] = pixels[src_idx:src_idx + 3]
    return width, new_height, new_pixels


def rotate_image(pixels, width, height, degrees):
    if nearly_equal(degrees, 0.0):
        return width, height, pixels[:]
    radians = math.radians(degrees)
    cos_a = math.cos(radians)
    sin_a = math.sin(radians)
    new_width = int(round(abs(width * cos_a) + abs(height * sin_a))) or 1
    new_height = int(round(abs(width * sin_a) + abs(height * cos_a))) or 1
    new_pixels = [255] * (new_width * new_height * 3)
    cx = (width - 1) / 2.0
    cy = (height - 1) / 2.0
    nx = (new_width - 1) / 2.0
    ny = (new_height - 1) / 2.0
    for y in range(new_height):
        for x in range(new_width):
            rx = x - nx
            ry = y - ny
            src_x = cos_a * rx + sin_a * ry + cx
            src_y = -sin_a * rx + cos_a * ry + cy
            if 0.0 <= src_x <= width - 1 and 0.0 <= src_y <= height - 1:
                sample = bilinear_sample(pixels, width, height, src_x, src_y)
                base = (y * new_width + x) * 3
                new_pixels[base:base + 3] = sample
    return new_width, new_height, new_pixels


def add_border_noise(pixels, width, height, thickness, density, seed):
    if thickness <= 0 or density <= 0:
        return pixels[:]
    density = max(0.0, min(1.0, density))
    rng = random.Random(seed)
    new_pixels = pixels[:]
    for y in range(height):
        for x in range(width):
            if (
                x < thickness or x >= width - thickness or
                y < thickness or y >= height - thickness
            ):
                if rng.random() < density:
                    value = rng.randrange(0, 256)
                    idx = (y * width + x) * 3
                    new_pixels[idx:idx + 3] = [value, value, value]
    return new_pixels


def main():
    parser = argparse.ArgumentParser(description="Apply geometric distortions to a P3 PPM.")
    parser.add_argument('--input', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--scale-x', type=float, default=1.0)
    parser.add_argument('--scale-y', type=float, default=1.0)
    parser.add_argument('--rotate', type=float, default=0.0)
    parser.add_argument('--skew-x', type=float, default=0.0)
    parser.add_argument('--skew-y', type=float, default=0.0)
    parser.add_argument('--border-thickness', type=int, default=0)
    parser.add_argument('--border-density', type=float, default=0.35)
    parser.add_argument('--seed', type=int, default=0)
    args = parser.parse_args()

    comments, width, height, pixels = read_ppm(Path(args.input))

    width, height, pixels = scale_image(pixels, width, height, args.scale_x, args.scale_y)
    width, height, pixels = skew_horizontal(pixels, width, height, args.skew_x)
    width, height, pixels = skew_vertical(pixels, width, height, args.skew_y)
    width, height, pixels = rotate_image(pixels, width, height, args.rotate)
    pixels = add_border_noise(pixels, width, height, args.border_thickness, args.border_density, args.seed)

    write_ppm(Path(args.output), comments, width, height, pixels)


if __name__ == '__main__':
    main()
