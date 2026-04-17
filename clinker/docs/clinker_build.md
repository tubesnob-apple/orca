# clinker — build, scope, and reference

Working notes for `clinker/`, the ORCA/C reimplementation of the ORCA/M OMF
linker. Everything we know in one place so we don't have to re-derive it each
session.

---

## What clinker is

- A drop-in replacement for the ORCA/M link editor (`linker/` in this repo),
  written in ORCA/C instead of 65816 assembly.
- Reads ORCA-family OMF object (OBJ) and library (LIB) files and emits a single
  GS/OS load file.
- Invoked from the shell as `iix clinker <args>` — same general argument shape
  as `iix link`.

## Scope (deliberately narrow)

- **Input OMF: v2 and v2.1 only.** No v0, no v1. Modern ORCA/M, ORCA/C, and
  ORCA/Pascal all emit v2.
- **Output: GS/OS (ProDOS 16) load files and GNO executables.** Same OMF v2
  layout on disk; GNO differs only by ProDOS file type (EXE vs S16).
- Out of scope: 8-bit ORCA/M, ProDOS 8, Apple II (non-IIgs), cc65/non-IIgs
  targets, run-time library (RTL) generation.

Consequences of this scope:
- Skip VERSION dispatch in the header reader. Reject `VERSION != 2` as a
  "foreign file" per GS/OS rules.
- BLKCNT/BYTECNT ambiguity is moot — always treat the first header field as
  BYTECNT.
- GLOBAL/LOCAL/GEQU/EQU attribute trailer is always 4 bytes (2-byte length +
  1-byte type + 1-byte private).
- LCBANK, absolute-bank segment type ($11), and other v0/v1-only concerns do
  not apply.

---

## Build and run

Build:

    make -C goldengate clinker
    # OR, equivalently, directly in clinker/goldengate:
    make -C clinker/goldengate

Output binary: `clinker/goldengate/clinker` (ProDOS file type EXE, aux
`$DB01`).

Run in place — **do not install just to iterate:**

    iix ~/source/orca/clinker/goldengate/clinker <args>

Install (only when promoting a build; goes to
`/Library/GoldenGate/Utilities/clinker`):

    make -C goldengate clinker-install

Clean:

    make -C goldengate clinker-clean

`goldengate/Makefile` (top-level) exposes these as aggregate targets.
Note: top-level `make`/`make install`/`make clean` currently **exclude**
clinker — it builds only via its explicit target.

### USE_CLINKER

Sub-makes accept `USE_CLINKER=1` to swap `iix link` for `iix clinker` when
linking other ORCA tools. We have not exercised this end-to-end yet — it's a
hook for eventual self-hosting, not a sanctioned workflow.

---

## CLI

Argument parsing lives in `clinker.c` (`GetParms`, `ParseFlag`). Today it
understands:

| Form                | Meaning                                               |
| ---                 | ---                                                   |
| `<file>`            | Input OBJ (also auto-tries `<file>.a`, `<file>.A`, and pulls sibling `<file>.ROOT` if present) |
| `keep=<file>`       | Output file name (ORCA convention)                    |
| `-o <file>`         | Same as `keep=<file>`                                 |
| `lib=<path>`        | Library file for deferred symbol extraction           |
| `+L`                | List segment info                                     |
| `+S`                | List symbol table                                     |
| `+W`                | Pause on error                                        |
| `+M`                | Memory-only link                                      |
| `+B`                | Bank-org mode                                         |
| `+X` / `-X`         | Disable ExpressLoad (default: off, because emission is unimplemented) |
| `-C`                | Disable compact RELOC records                         |
| `-P`                | Disable progress messages                             |

### Shell variables consumed

- `gsplusSymbols` (set via `iix -DgsplusSymbols=1 clinker ...`): emit a JSON
  `.symbols` sidecar next to the output. Mirrors the linker.asm feature.

### `.ROOT` companion behavior

ORCA/M writes data/privdata segments (direct-page LOCAL symbols) into a
separate `foo.ROOT` file alongside `foo.a`. `iix link` pulls it in
automatically. clinker does too: when an input arg has no extension and
opens successfully, clinker also tries `<arg>.ROOT` / `<arg>.root` and
appends it to the input list.

---

## Source layout

Everything under `clinker/`:

| File             | Role                                                              |
| ---              | ---                                                               |
| `clinker.h`      | Shared types, OMF opcodes, segment-type constants, globals        |
| `clinker.c`      | `main`, CLI parsing, output writer, global helpers                |
| `omf.c`          | Low-level OMF read/write primitives and segment-header I/O        |
| `pass1.c`        | Body measurement, symbol collection, library search               |
| `pass2.c`        | Data emission, expression evaluation, relocation collection       |
| `symbol.c`       | Hash-table symbol store                                           |
| `goldengate/`    | Makefile that compiles with `iix compile` and links with `iix link` |
| `docs/`          | OMF-notes.mhtml, Loader Secrets.mhtml, and this doc               |

Key types (`clinker.h`):
- `InputFile` — one OBJ or LIB file on the command line
- `InSeg` — one segment within an input file (links under `InputFile.segs`)
- `OutSeg` — one output segment in the final load file (links under global `outSegs`)
- `RelocRec` — pending RELOC or INTERSEG record attached to an `OutSeg`
- `Symbol` — hash-chained global/private symbol entry

Hash table is size 1021 (prime), keyed by uppercase symbol name.

---

## OMF v2 reference (reading only what we need)

### Segment header (v2, 44 bytes + names)

| Off | Size | Field    | Notes                                                                 |
| --- | ---  | ---      | ---                                                                   |
| 0   | 4    | BYTECNT  | Total bytes in this segment on disk, including header                 |
| 4   | 4    | RESSPC   | Trailing zero-padding bytes (reserved space)                          |
| 8   | 4    | LENGTH   | Memory size after loading; includes RESSPC                            |
| 12  | 1    | (unused) |                                                                       |
| 13  | 1    | LABLEN   | 0 = variable-length pstrings for in-body labels                       |
| 14  | 1    | NUMLEN   | Must be 4                                                             |
| 15  | 1    | VERSION  | 2 (we enforce this)                                                   |
| 16  | 4    | BANKSIZE | Max memory bank size; must be ≤ $10000                                |
| 20  | 2    | KIND     | Low 5 bits = segment type; upper bits = flags                         |
| 22  | 2    | (unused) |                                                                       |
| 24  | 4    | ORG      | Absolute load address (usually 0)                                     |
| 28  | 4    | ALIGN    | Alignment; must be ≤ $10000                                           |
| 32  | 1    | NUMSEX   | 0 = little-endian (must be 0)                                         |
| 33  | 1    | LANG     | Source language ID (informational)                                    |
| 34  | 2    | SEGNUM   |                                                                       |
| 36  | 4    | ENTRY    | Entry-point offset within segment                                     |
| 40  | 2    | DISPNAME | Offset to LOADNAME from segment start (normally 44 = $2C)             |
| 42  | 2    | DISPDATA | Offset to body from segment start                                     |
| ... | 10   | LOADNAME | Fixed 10 bytes, space-padded                                          |
| ... | *    | SEGNAME  | Pascal string (1-byte length + chars)                                 |

v2.1 adds a 4-byte `tempOrg` at +$2c (used only by MPW cross-assembler — we
pass through but don't interpret).

### Segment types (low 5 bits of KIND)

| Code  | Meaning                                                |
| ---   | ---                                                    |
| $00   | code                                                   |
| $01   | data                                                   |
| $02   | jump table                                             |
| $04   | pathname                                               |
| $08   | library dictionary                                     |
| $10   | initialization                                         |
| $12   | direct-page / stack                                    |

ExpressLoad segment uses the full KIND word $8001 (dynamic-data bit set so
pre-5.0 loaders skip it). See "ExpressLoad" below.

### KIND flags (upper bits) we should preserve on output

- $0100 bank-relative
- $0200 skip
- $0400 reload
- $0800 absolute bank
- $1000 no-special-memory
- $2000 position-independent
- $4000 private
- $8000 dynamic

Currently clinker writes only the low 5 bits and drops flags. This is a
known gap.

### Foreign-file rejection (GS/OS rule)

Reject on read if any of:
- `NUMSEX != 0`
- `NUMLEN != 4`
- `BANKSIZE > $10000`
- `ALIGN > $10000`
- `VERSION != 2`

Cheap 5-line check, not yet implemented.

### Body records

Each segment body is a stream of opcode-prefixed records, terminated by $00.

| Opcode     | Name       | Payload                                                             |
| ---        | ---        | ---                                                                 |
| $00        | END        | (none)                                                              |
| $01-$DF    | Const      | Literal byte count; that many data bytes follow                     |
| $E0        | ALIGN      | dword factor                                                        |
| $E1        | ORG        | dword new-pc                                                        |
| $E2        | RELOC      | pLen(1) shift(1) pc(4) value(4) — patch within this segment         |
| $E3        | INTERSEG   | pLen(1) shift(1) pc(4) fileNum(2) segNum(2) addend(4)               |
| $E4        | USING      | pstring name                                                        |
| $E5        | STRONG     | pstring name                                                        |
| $E6        | GLOBAL     | pstring name + 2-byte lenAttr + 1-byte type + 1-byte private        |
| $E7        | GEQU       | like GLOBAL + expression                                            |
| $E8        | MEM        | 2× dword                                                            |
| $EB        | EXPR       | pLen(1) + expression stream                                         |
| $EC        | ZEXPR      | same as EXPR; zero-truncated result                                 |
| $ED        | BEXPR      | same as EXPR; bank-relative                                         |
| $EE        | RELEXPR    | pLen(1) + dword base + expression                                   |
| $EF        | LOCAL      | like GLOBAL, but private to this segment                            |
| $F0        | EQU        | like LOCAL + expression                                             |
| $F1        | DS         | dword length (zero-fill)                                            |
| $F2        | LCONST     | dword length + that many literal bytes                              |
| $F3        | LEXPR      | EXPR whose patch lives in the outer segment                         |
| $F4        | ENTRY      | word(reserved) + dword(value) + pstring(name)                       |
| $F5        | cRELOC     | pLen(1) shift(1) pc(2) value(2) — compact RELOC                     |
| $F6        | cINTERSEG  | pLen(1) shift(1) pc(2) segNum(1) value(2) — compact INTERSEG        |
| $F7        | SUPER      | dword length + packed RELOC/cRELOC/INTERSEG stream                  |

Expression opcodes (terminated by $00): $01–$0B arithmetic/logical ops (pop
two, push one; details in `clinker.h` and `pass1.c`), $80 PC, $81 const
(dword), $82 weak ref (pstring name), $83 strong ref (pstring name), $87
segment displacement (dword).

---

## ExpressLoad (optional; output side)

ExpressLoad is a pre-indexed first segment that lets the GS/OS loader skip
parsing the whole file. Emit when `opt_express` is set.

On-disk shape (KIND = $8001, SEGNUM = 1, name "~EXPRESSLOAD", body is a
single LCONST):

    word  file refnum               (0 on disk)
    word  reserved                  (0 on disk)
    word  numSegments - 2
    8*N   segment list              (one per non-ExpressLoad segment)
    2*N   remapping list            (old-segnum → new-segnum; often i+2)
    var   segment header info array (file offsets, reloc offsets, header copy minus first 12 bytes)

Important interaction with our `.symbols` JSON: when ExpressLoad is
prepended, all other segments shift by 1. Symbol records in input OMF carry
*pre-remap* segment numbers; the output segment table holds *post-remap*
numbers. The "segment `number` differs from symbol `segment` by 1" note in
the top-level `CLAUDE.md` is exactly this. When we implement ExpressLoad
emission, remap symbol segNums at the same time.

Full layout details in `docs/Loader Secrets.mhtml` (Neil Parker).

---

## Current status (2026-04-17)

**Committed baseline:**
- Clinker compiles under GoldenGate and produces a binary at
  `clinker/goldengate/clinker`.
- End-to-end: reads OMF v2 OBJ/LIB/ROOT, walks passes 1 and 2, writes a load
  file with RELOC/INTERSEG dictionary records. Handles the GSplus WDM
  prologue and `.symbols` JSON emission mirroring linker.asm.
- **Uncommitted WIP** (~450 added / 90 removed across
  clinker.c/h, omf.c, pass1.c, pass2.c, goldengate/Makefile) extends record
  coverage and fixes a handful of parsing gaps.

**Known correctness gaps — in priority order:**
1. **SUPER input records are skipped** (`pass2.c:291-296`). Modern ORCA/C
   output uses SUPER heavily. Dropping them drops relocations — this is a
   real correctness bug, not "future work."
2. **No foreign-file rejection.** We should enforce the five-rule check on
   every segment header and refuse obviously-invalid inputs.
3. **KIND flags dropped on output.** `OmfWriteSegHeader` writes only the low
   5 bits; upper-byte flags (private, dynamic, reload, etc.) are lost.
4. **ExpressLoad emission not implemented.** `opt_express` flag exists but
   is never consumed by the output writer.
5. **Library search is O(N²).** `LibrarySearch` rescans every library on
   every iteration; doesn't read the library dictionary segment ($08).
   Correct, but slow on ORCALib.
6. **Symbol-record attribute handling** hardcodes v2 layout (correct for our
   scope, but a VERSION=2 assertion would make that explicit).
7. **Dead v0/v1 fallback paths** in `omf.c` (the `DISPNAME < $2C` branch) —
   unreachable under v2-only, should be deleted for clarity.

**Not bugs, deliberate deferrals for now:**
- No cRELOC/cINTERSEG emission on output (we emit full-size RELOC/INTERSEG).
- No SUPER emission on output.
- No library-dictionary writing (we don't produce libraries; that's
  `makelib`'s job).
- v2.1 `tempOrg` is read-transparent but not acted on.

---

## Roadmap (phased, correctness-first)

Each phase should land as small, reviewable commits, guarded by the test
harness from phase 1.

1. **Byte-diff test harness.** Link the same fixed set of OBJ/LIB inputs
   with `iix link` (the ORCA/M reference) and `iix clinker` (ours), then
   byte-compare the outputs. Any divergence is a ticket. Same pattern we
   used to validate the `.symbols` feature.
2. **SUPER input parsing.** Expand compressed RELOC/cRELOC/INTERSEG streams
   into our internal RelocRec list. Single biggest correctness item.
3. **Foreign-file rejection.** 5-line header check in `OmfReadHeader`.
4. **`.sym65` sidecar emission.** Ten-line post-process that emits SourceGen-
   compatible `LABEL @ $hex width` lines alongside `.symbols`. Adds a
   second independent verifier (SourceGen) with almost no code.
5. **KIND-flag preservation** on output: write the full 16-bit KIND, not
   just the low 5 bits.
6. **ExpressLoad emission** when `opt_express` is set. Remap symbol
   segment numbers at the same time to kill the off-by-one.
7. **Library-dictionary reader.** Use the $08 segment to target-look-up
   symbols instead of rescanning whole libraries.
8. **cRELOC/cINTERSEG emission** on output (size optimization).
9. **SUPER emission** on output (further size optimization).

---

## Library dictionary segment (KIND = $08) — from Apple's Appendix F

Library files embed a single KIND=$08 segment that MakeLib fills in.
The body is three LCONST records, in this order:

1. **Filenames record** — sequence of `(word file_num, pstring filename)`
   entries terminated by `file_num == 0`. file_num assigned by MakeLib.
2. **Symbol table record** — fixed 12-byte entries, one per global symbol:
   - `dword name_displacement` (into the symbol-names record)
   - `word  file_number` (into filenames record)
   - `word  private_flag` (1 = private, 0 = public)
   - `dword segment_displacement` (file offset of the segment's header)
3. **Symbol names record** — sequence of pstring names (length byte + up
   to 255 chars); no terminator, walk to end of LCONST.

Lookup flow for "does library L define symbol FOO?":

1. Read symbol-names record, linear-scan for "FOO".
2. If found, note the offset. Find the matching symbol-table entry (the
   one whose `name_displacement` equals that offset).
3. `segment_displacement` tells you exactly where in the library file the
   object segment lives. Seek, read header, pass1/pass2 that segment.

Replaces our current O(N²) whole-library rescans. Turns into O(names) per
unresolved symbol.

## Pathname segment (KIND = $04)

Linker-emitted into load files that reference run-time libraries. Entry
format, terminated by `file_num == 0`:

    word   file_number         ; 1 = this load file itself
    8bytes file_date_time      ; GS/OS ReadTimeHex
    pstring pathname

The loader compares the stored date/time to the actual RTL file at load
time to detect version skew.

## Jump-table segment (KIND = $02)

One per load file (when dynamic segments are used). 14 bytes per entry,
terminated by `load_file_num == 0`. **Unloaded** state (what we emit):

    word  user_id              ; 0; loader patches at initial load
    word  load_file_num
    word  load_segment_num
    dword load_segment_offset
    dword jsl_to_loader        ; loader patches at initial load

**Loaded** state (runtime transform — not our concern on output):

    word  user_id
    word  load_file_num
    word  load_segment_num
    dword load_segment_offset
    dword jml_to_external_ref

Historical: OMF v1/v2 jump-table segments start with 8 bytes of zeros
before the first entry. v2.1 calls these out as possibly removable. For
safety: emit the leading zeros; tolerate them on input.

## SUPER subrecord type catalog (from Appendix F)

There are 38 SUPER subrecord types. Pattern: `(type_byte, patch_bytes,
bit_shift, file_num, segment_num_source)`.

| Type  | Name             | Replaces   | Bytes | Shift | File | Segment                       |
| ---   | ---              | ---        | ---   | ---   | ---  | ---                           |
| 0     | RELOC2           | cRELOC     | 2     | 0     | —    | —                             |
| 1     | RELOC3           | cRELOC     | 3     | 0     | —    | —                             |
| 2     | INTERSEG1        | cINTERSEG  | 3     | 0     | 1    | in byte 2 of patched location |
| 3-13  | INTERSEG2-12     | INTERSEG   | 3     | 0     | type-1| in byte 2 of patched location |
| 14-25 | INTERSEG13-24    | cINTERSEG  | 2     | 0     | 1    | type - 12                     |
| 26-37 | INTERSEG25-36    | cINTERSEG  | 2     | -16   | 1    | type - 24                     |

For all SUPER types, the **offset to be patched** is reconstructed from
(256-byte page * page_number) + in-page offset; the **reference value**
lives in the LCONST at the patch location (2 bytes, little-endian). For
SUPER INTERSEG1-12 (types 2-13), the **segment number** of the target
lives in the third byte of the 3-byte patched location. See Appendix F
pp. 463-464 for the full worked example.

## SUPER records — what we learned from OMFAnalyzer

SUPER ($F7) is the compressed form of RELOC/cRELOC/INTERSEG. Our current
pass2 skips SUPER entirely, which is a correctness bug (modern ORCA/C
output relies on it heavily).

Format (per OMFAnalyzer's `OMF_Record.c:1051-1269` and the OMF v2 spec):

    dword length          ; byte count of the SUPER payload
    byte  RecordType      ; 0x00=Reloc2, 0x01=Reloc3, 0x02..0x25=Interseg*
    ... packed subrecord stream follows ...

Subrecord stream is divided into implicit 256-byte "pages." Each subrecord
begins with a `Count` byte:

- If `Count & 0x80` is set: skip `Count & 0x7F` pages and continue. No
  further payload.
- Otherwise: `Count + 1` is the number of in-page offsets. The next
  `Count+1` bytes are in-page offsets; each maps to a full patch address
  via `OffsetPatch = (page << 8) | inPageOffset`.

**Non-obvious detail that matters:** the *reference value* to be relocated
(the target address before relocation) is **not** in the SUPER stream. It
lives in the LCONST at offset `OffsetPatch`. Decoding reads two bytes
from the LCONST at that position, that's the OffsetReference. See
`DecodeSuperReloc2` at `OMF_Record.c:1329-1398`, specifically the
`memcpy(&one_word, &lconst[OffsetPatch[i]], sizeof(WORD))` at line 1380.

Consequence for clinker: when pass2 processes SUPER records, it must read
back from the output segment's data buffer (which already contains the
emitted LCONST bytes) at `out->data[seg->baseOffset + OffsetPatch]` to get
the addend. Today pass2 emits LCONST and retains the buffer through the
end of the segment body, so this works — but any future refactor that
streams LCONST data must preserve readback access.

SUPER types (full list in `OMF_Record.c`):

| Type | Meaning                                                        |
| ---  | ---                                                            |
| 0x00 | SuperReloc2 — 2-byte patches within this segment               |
| 0x01 | SuperReloc3 — 3-byte patches within this segment               |
| 0x02 | SuperInterseg1 — to segment 1                                  |
| 0x03..0x0D | SuperInterseg2..12 — to segments 2..12                   |
| 0x0E..0x25 | SuperInterseg13..36 — to segments 13..36                 |

For now, plan: unpack SUPER into our existing `RelocRec` list rather than
keeping a separate representation. Treat SUPER as a parse-time transform,
not a separate record kind in our IR.

## References

All local copies live under `clinker/docs/`:
- `OMF-notes.mhtml` — ciderpress2 summary (concise, authoritative for v2).
- `Loader Secrets.mhtml` — Neil Parker's ExpressLoad + loader internals.
- `omfanalyzer.mhtml` — Brutal Deluxe's product page for OMFAnalyzer.

Primary-source docs:
- **GS/OS Reference, Appendix F** — OMF v2.1. Local copy
  (Appendix F only): `clinker/docs/GSOSReferenceManual_Appendix-F-OMF.pdf`.
  Extract text with `pdftotext -layout <pdf> <txt>` (install via
  `brew install poppler`). Known errata from the community: table F-2
  says "blockCount" where it should say SEGNAME; "REVISION" field does
  not exist. (`tempOrg` location is actually $2c despite table F-2
  listing $2a.)
- **ORCA/M 2.0 manual, appendix B** — OMF v0/v1/v2.1 (p.488). Not in repo.
- **Apple IIgs Programmer's Workshop Reference, chapter 7** — OMF v1/v2
  (p.228). Not in repo.

Reference implementations (not in repo):
- **6502Bench SourceGen** — `~/source/iigs-official-repos/6502Bench/SourceGen/Tools/Omf/`
  is a C#/.NET OMF parser (Windows-only natively). `Loader.cs` mirrors what
  the GS/OS system loader does at run time — useful sanity-check target
  for our output.
- **OMFAnalyzer v1.3 (Brutal Deluxe)** — `~/source/iigs-official-repos/OMFAnalyzer_v1.3/`.
  Portable C. The best reference for:
  - **SUPER subrecord unpacking** — `OMF_Record.c:1051-1269` and decoders at
    `DecodeSuperReloc2` (line 1329), `DecodeSuperReloc3`, `DecodeSuperInterseg*`.
  - **V2 header field layout + foreign-file validation ladder** —
    `DecodeSegmentHeaderV2` at `OMF_Load.c:482-650`.
  - **ExpressLoad on-disk format parsing** — `DumpExpressLoadData` in
    `OMF_Dump.c:~750-850`.
  OMFAnalyzer does NOT parse library dictionaries ($08 segments) or resolve
  symbols — for those we're on our own with the spec.

## Related repo pieces

- `linker/` — the reference ORCA/M implementation, in 65816 assembly. Byte-
  diff oracle for clinker.
- `linker/gsplusSymbols.md` — spec for the `.symbols` JSON format, shared
  between linker and clinker.
- `dumpobj/` — standalone OMF dumper, useful for inspecting what either
  linker produces.
