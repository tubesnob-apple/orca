#!/usr/bin/env python3
"""
Validate an OMF library produced by MakeLib.

Usage:
    python3 check_library.py <library_path> <expected_module_segments>

Parses the library, counts non-dictionary OMF segments, and exits 0 if the
count matches the expectation, or 1 on any failure.

OMF library structure (written by MakeLib's Update()):
  Segment 1: dictionary   KIND = 8
  Segment 2+: module code/data segments copied verbatim from input .a files

The bug being tested causes Update() to write the dictionary but copy zero
bytes from the work file, so only segment 1 is present when two or more
modules are added in a single MakeLib invocation.
"""

import os
import struct
import sys

DICT_KIND = 8  # KIND value written by Update() for the library dictionary


def parse_library(path: str):
    """
    Walk an OMF file and return a list of (kind, blkcnt) per segment.
    Returns None on parse error.
    """
    segments = []
    with open(path, "rb") as f:
        seg_index = 0
        while True:
            # Read the fixed 42-byte header that GetDiskSeg() also reads
            hdr = f.read(42)
            if len(hdr) == 0:
                break  # clean EOF
            if len(hdr) < 42:
                print(f"  ERROR: truncated header in segment {seg_index} ({len(hdr)} bytes)")
                return None

            numsex = hdr[32]
            if numsex:  # big-endian
                blkcnt = struct.unpack_from(">I", hdr, 0)[0]
                kind   = struct.unpack_from(">H", hdr, 20)[0]
            else:        # little-endian (normal for IIgs)
                blkcnt = struct.unpack_from("<I", hdr, 0)[0]
                kind   = struct.unpack_from("<H", hdr, 20)[0]

            if blkcnt < 42:
                print(f"  ERROR: segment {seg_index} BLKCNT={blkcnt} < 42")
                return None

            tail = f.read(blkcnt - 42)
            if len(tail) < blkcnt - 42:
                print(f"  ERROR: segment {seg_index} truncated body "
                      f"(expected {blkcnt-42} bytes, got {len(tail)})")
                return None

            segments.append((kind, blkcnt))
            seg_index += 1

    return segments


def main() -> None:
    if len(sys.argv) != 3:
        print(f"Usage: {os.path.basename(sys.argv[0])} <library> <expected_module_segments>")
        sys.exit(1)

    lib_path = sys.argv[1]
    try:
        expected = int(sys.argv[2])
    except ValueError:
        print(f"ERROR: expected_module_segments must be an integer, got {sys.argv[2]!r}")
        sys.exit(1)

    if not os.path.exists(lib_path):
        print(f"  ERROR: library not found: {lib_path}")
        sys.exit(1)

    file_size = os.path.getsize(lib_path)
    segments = parse_library(lib_path)
    if segments is None:
        sys.exit(1)

    dict_segs  = [(k, s) for k, s in segments if k == DICT_KIND]
    module_segs = [(k, s) for k, s in segments if k != DICT_KIND]

    print(f"  file size      : {file_size} bytes")
    print(f"  total segments : {len(segments)}")
    print(f"  dictionary     : {len(dict_segs)}")
    print(f"  module segs    : {len(module_segs)}  (expected: {expected})")

    ok = True
    if len(dict_segs) != 1:
        print(f"  FAIL: expected 1 dictionary segment, got {len(dict_segs)}")
        ok = False
    if len(module_segs) != expected:
        print(f"  FAIL: expected {expected} module segment(s), got {len(module_segs)}")
        ok = False

    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
