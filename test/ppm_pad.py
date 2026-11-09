#!/usr/bin/env python3
import argparse

from ppm_test_utils import read_ppm, write_ppm

parser = argparse.ArgumentParser()
parser.add_argument("input")
parser.add_argument("output")
parser.add_argument("width", type=int)
parser.add_argument("height", type=int)
parser.add_argument("offset_x", type=int)
parser.add_argument("offset_y", type=int)
args = parser.parse_args()

source_width, source_height, source = read_ppm(args.input)
if args.offset_x < 0 or args.offset_y < 0 or args.offset_x + source_width > args.width or args.offset_y + source_height > args.height:
    raise SystemExit("source image does not fit output canvas")
pixels = bytearray([255]) * (args.width * args.height * 3)
for row in range(source_height):
    source_start = row * source_width * 3
    destination_start = ((row + args.offset_y) * args.width + args.offset_x) * 3
    pixels[destination_start:destination_start + source_width * 3] = source[source_start:source_start + source_width * 3]
write_ppm(args.output, args.width, args.height, pixels)
