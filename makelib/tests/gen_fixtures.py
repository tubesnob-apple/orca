#!/usr/bin/env python3
"""
Generate minimal valid OMF 2.0 object module files for MakeLib testing.

Creates two fixture files in the given output directory:
  fixa.a  —  single code segment named "FIXA"
  fixb.a  —  single code segment named "FIXB"

Sets com.apple.FinderInfo on each file to mark it as ProDOS OBJ type
(0xB1), which MakeLib checks via GetFileInfoGS before accepting any input.

OMF 2.0 segment header layout used here (matches real ORCA/C output):

  Offset  Size  Field     Value / Notes
  ------  ----  -----     -------
   0-3     4    BLKCNT    total segment length, little-endian
   4-7     4    RESSPC    0
   8-11    4    LENGTH    0 (no assembled output)
  12       1    undef     0
  13       1    LABLEN    0 (variable-length labels)
  14       1    NUMLEN    4
  15       1    VERSION   2 (OMF 2.0)
  16-19    4    BANKSIZE  0x00010000
  20-21    2    KIND      0 (code)
  22-23    2    undef     0
  24-27    4    ORG       0
  28-31    4    ALIGN     0
  32       1    NUMSEX    0 (little-endian)
  33       1    undef     0
  34-35    2    SEGNUM    segment number
  36-39    4    ENTRY     0
  40-41    2    DISPNAME  offset to LOADNAME (= 48)
  42-43    2    DISPDATA  offset to segment body
  44-47    4    reserved  0
  48-57   10    LOADNAME  segment name, space-padded to 10 bytes
  58       1    SEGNAME   length byte
  59+      N    SEGNAME   segment name bytes
   +0      1    END       0x00 opcode

MakeLib's AddSeg reads the public symbol name from DISPNAME+10 (i.e. offset
58), so DISPNAME must point to LOADNAME (offset 48) and SEGNAME immediately
follows.
"""

import os
import sys
import struct
import subprocess


def make_omf_segment(seg_name: str, seg_num: int = 1) -> bytes:
    name_bytes = seg_name.upper().encode("ascii")
    loadname = name_bytes.ljust(10)[:10]
    segname_len = len(name_bytes)

    dispname = 48
    dispdata = dispname + 10 + 1 + segname_len  # past LOADNAME + SEGNAME
    blkcnt = dispdata + 1                        # +1 for END opcode (0x00)

    seg = b""
    seg += struct.pack("<I", blkcnt)        # BLKCNT
    seg += b"\x00" * 4                      # RESSPC
    seg += b"\x00" * 4                      # LENGTH
    seg += b"\x00"                           # undef
    seg += b"\x00"                           # LABLEN
    seg += b"\x04"                           # NUMLEN
    seg += b"\x02"                           # VERSION (OMF 2.0)
    seg += struct.pack("<I", 0x00010000)    # BANKSIZE
    seg += struct.pack("<H", 0)             # KIND (code)
    seg += b"\x00" * 2                      # undef
    seg += struct.pack("<I", 0)             # ORG
    seg += struct.pack("<I", 0)             # ALIGN
    seg += b"\x00"                           # NUMSEX (little-endian)
    seg += b"\x00"                           # undef
    seg += struct.pack("<H", seg_num)       # SEGNUM
    seg += struct.pack("<I", 0)             # ENTRY
    seg += struct.pack("<H", dispname)      # DISPNAME
    seg += struct.pack("<H", dispdata)      # DISPDATA
    seg += b"\x00" * 4                      # reserved (TEMPORG)
    seg += loadname                          # LOADNAME (10 bytes)
    seg += bytes([segname_len])              # SEGNAME length byte
    seg += name_bytes                        # SEGNAME
    seg += b"\x00"                           # END opcode

    assert len(seg) == blkcnt, f"size mismatch: {len(seg)} != {blkcnt}"
    return seg


def set_prodos_obj_type(path: str) -> None:
    """
    Set com.apple.FinderInfo to ProDOS OBJ type 0xB1 + creator 'pdos'.

    GoldenGate encodes ProDOS file types in FinderInfo as:
      fdType[0] = 0x70  ('p' marker)
      fdType[1] = filetype byte (0xB1 = OBJ)
      fdType[2-3] = 0x00 0x00
      fdCreator  = 'pdos' (0x70 0x64 0x6F 0x73)
    """
    finder_info = bytes([
        0x70, 0xB1, 0x00, 0x00,  # fdType: ProDOS OBJ
        0x70, 0x64, 0x6F, 0x73,  # fdCreator: 'pdos'
    ]) + b"\x00" * 24

    hex_str = " ".join(f"{b:02x}" for b in finder_info)
    subprocess.run(["xattr", "-wx", "com.apple.FinderInfo", hex_str, path], check=True)


def main() -> None:
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "fixtures")
    os.makedirs(out_dir, exist_ok=True)

    fixtures = [
        ("fixa.a", "FIXA", 1),
        ("fixb.a", "FIXB", 2),
    ]
    for filename, segname, segnum in fixtures:
        path = os.path.join(out_dir, filename)
        data = make_omf_segment(segname, segnum)
        with open(path, "wb") as f:
            f.write(data)
        set_prodos_obj_type(path)
        print(f"  {path}  ({len(data)} bytes, SEGNAME='{segname}')")


if __name__ == "__main__":
    main()
