#!/usr/bin/env python3
"""Check the escaped report produced by `makocode decode --decoded-stdout`."""

import argparse
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument("--generator", required=True)
expected = parser.add_mutually_exclusive_group(required=True)
expected.add_argument("--size", type=int)
expected.add_argument("--literal")
parser.add_argument("--seed", type=int)
parser.add_argument("--tolerance-percent", type=float, default=0.0)
parser.add_argument("--label", default="decoded stdout")
args = parser.parse_args()

if args.size is not None:
    if args.size < 0 or args.seed is None or args.seed < 0:
        parser.error("--size requires a non-negative --seed")
    expected = subprocess.run(
        [args.generator, "bytes", "--output", "-", "--size", str(args.size), "--seed", str(args.seed)],
        check=True,
        stdout=subprocess.PIPE,
    ).stdout
else:
    expected = args.literal.encode("utf-8")

if args.tolerance_percent < 0:
    parser.error("--tolerance-percent must be non-negative")

report = sys.stdin.buffer.read().splitlines()
if not report or report[0] != b"MAKOCODE-DECODED":
    raise SystemExit(f"{args.label}: missing MAKOCODE-DECODED header")
stream_lines = [index for index, line in enumerate(report) if line.startswith(b"stream ")]
if len(stream_lines) != 1:
    raise SystemExit(f"{args.label}: expected one decoded stream, found {len(stream_lines)}")
stream_index = stream_lines[0]
try:
    stream_size = int(report[stream_index][len(b"stream "):])
    encoded = report[stream_index + 1]
except (ValueError, IndexError):
    raise SystemExit(f"{args.label}: malformed decoded stream entry")
if b"end-archive" not in report or report[-1] != b"end":
    raise SystemExit(f"{args.label}: incomplete decoded stdout report")

actual_buffer = bytearray()
index = 0
while index < len(encoded):
    value = encoded[index]
    if value == 92:
        if index + 3 >= len(encoded) or encoded[index + 1] != ord("x"):
            raise SystemExit(f"{args.label}: malformed escape in decoded payload")
        try:
            actual_buffer.append(int(encoded[index + 2:index + 4], 16))
        except ValueError:
            raise SystemExit(f"{args.label}: malformed hex escape in decoded payload")
        index += 4
    elif 32 <= value <= 126:
        actual_buffer.append(value)
        index += 1
    else:
        raise SystemExit(f"{args.label}: unexpected non-printable byte in decoded report")
actual = bytes(actual_buffer)
if len(actual) != stream_size:
    raise SystemExit(f"{args.label}: reported {stream_size} bytes but decoded {len(actual)}")

common = min(len(expected), len(actual))
mismatches = sum(left != right for left, right in zip(expected[:common], actual[:common]))
mismatches += abs(len(expected) - len(actual))
denominator = max(len(expected), len(actual), 1)
percent = 100.0 * mismatches / denominator
if percent > args.tolerance_percent:
    raise SystemExit(
        f"{args.label}: payload differs in {mismatches}/{denominator} bytes "
        f"({percent:.3f}%; tolerance {args.tolerance_percent:.3f}%)"
    )
print(f"{args.label}: payload matches within {percent:.3f}%")
