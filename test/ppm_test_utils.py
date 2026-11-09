"""Small binary PPM helpers shared by the Python scan fixtures."""

from pathlib import Path


def read_ppm(path):
    data = Path(path).read_bytes()
    lines = data.splitlines(keepends=True)
    if not lines or lines[0].strip() != b"P6":
        raise ValueError("expected P6 PPM")
    values = []
    offset = len(lines[0])
    line_index = 1
    while len(values) < 3:
        if line_index >= len(lines):
            raise ValueError("incomplete P6 PPM header")
        line = lines[line_index]
        line_index += 1
        offset += len(line)
        values.extend(line.partition(b"#")[0].split())
    width, height, maximum = map(int, values[:3])
    if len(values) != 3 or maximum != 255:
        raise ValueError("invalid P6 PPM header")
    pixels = data[offset:]
    if len(pixels) != width * height * 3:
        raise ValueError("invalid P6 PPM pixel data")
    return width, height, list(pixels)


def write_ppm(path, width, height, pixels):
    pixels = bytes(pixels)
    if len(pixels) != width * height * 3:
        raise ValueError("pixel count does not match dimensions")
    Path(path).write_bytes(f"P6\n{width} {height}\n255\n".encode("ascii") + pixels)


def write_gray_ppm(path, width, height, pixels):
    write_ppm(path, width, height, (channel for value in pixels for channel in (value, value, value)))
