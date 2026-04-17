# GSplus Symbol Table Integration

This document describes the `.symbols` JSON file emitted by the ORCA/M
linker for consumption by the GSplus emulator's trace/debug window.

## Overview

When enabled, the linker writes a `<target>.symbols` file alongside every
linked binary. The file contains a single-line JSON object listing all
segments and symbols in the linked output, with enough information for the
emulator to map execution addresses back to human-readable names.

## Enabling emission

Symbol-file emission is **off by default**. It is gated on the
`gsplusSymbols` shell variable, which must be set to a non-empty value
before the linker runs.

Set the variable with the iix `-D` flag:

```bash
iix -DgsplusSymbols=1 link foo.a keep=foo
```

**Important:** `-D` is an iix-level flag, not a linker flag. It must
appear **before** the `link` sub-command:

```bash
iix -DgsplusSymbols=1 link ...      # correct
iix -D gsplusSymbols=1 link ...     # correct (space form)
iix link -DgsplusSymbols=1 ...      # WRONG — linker does not understand -D
```

The variable name `gsplusSymbols` is **case-sensitive** — it must match
exactly.

When the variable is unset or empty, no symbol file is written and no
WDM prologue is injected into the binary. When the variable is set, both
outputs are produced together.

## Output file

The symbol file is placed alongside the linked binary, with `.symbols`
appended to the keep-file name:

```
iix -DgsplusSymbols=1 link foo.a keep=MyProg
→ creates MyProg          (the linked binary)
→ creates MyProg.symbols  (the JSON symbol table)
```

If no `keep=` is specified (no output file), no symbol file is created.

## WDM prologue injected into the binary

When `gsplusSymbols` is set, the linker injects an 8-byte prologue at the
very start of the first OMF code segment (segment 1). This prologue fires
the WDM trap when the binary begins executing, signalling the emulator to
bind the symbol table to the loaded binary.

```
Offset 0–1:  $42 $0F   WDM $0F  — traps to the emulator
Offset 2–3:  $80 $04   BRA +4   — CPU skips the 4-byte signature on resume
Offset 4–7:  <sig>     32-bit link signature (little-endian)
Offset 8+:              actual user code
```

All pass-1 symbol offsets in segment 1 are automatically shifted by 8 to
account for the prologue, so no relocation entries are disturbed.

### Signature generation

The 32-bit signature (`symsig`) is computed at link time:

1. The tick counter (`GetTick`, Misc Tools) seeds a 32-bit value.
2. Each character of the output basename is mixed in: rotate the value
   left 7 bits, then XOR with the character byte.

The result changes on every link run of the same file (different ticks)
and differs across files with different names (different hash). It is
stored little-endian in the binary at offsets 4–7 of the prologue.

### How GSplus binds symbols

1. **At reset/restart:** GSplus walks the file root, opens every file
   whose name ends in `.symbols`, reads the `symsig` field, and builds
   an in-memory index keyed by signature value.
2. **At WDM $0F trap:** the emulator reads the 4 bytes at `[PC+4]`
   (immediately after the BRA instruction) as a little-endian 32-bit
   integer, looks it up in the signature index, and binds that symbol
   table to the segment currently loading.

## JSON format

The file contains a single line of JSON (no pretty-printing, no trailing
newline). The top-level object has these fields:

```json
{
  "orca_symbols_version": "0x0001",
  "symsig": "0xXXXXXXXX",
  "target": "MyProg",
  "linker": "ORCA/M Link Editor 2.3.0",
  "segments": [ ... ],
  "symbols": [ ... ]
}
```

### Top-level fields

| Field | Type | Description |
|-------|------|-------------|
| `orca_symbols_version` | string | Format version. Currently `"0x0001"`. |
| `symsig` | string | 32-bit link signature as an 8-digit hex string (e.g. `"0x1A2B3C4D"`). Matches the value embedded in the binary prologue at offsets 4–7 (little-endian). |
| `target` | string | Base name of the linked binary (from the `keep=` argument). |
| `linker` | string | Linker identification string including version. |
| `segments` | array | Array of segment objects (see below). |
| `symbols` | array | Array of symbol objects (see below). |

### Segment object

```json
{
  "number": "0x0002",
  "name": "MyProg",
  "type": "code",
  "org": "0x00000000",
  "length": "0x00004A00"
}
```

| Field | Type | Description |
|-------|------|-------------|
| `number` | string | Segment number as a hex string. |
| `name` | string | Segment name (from the OMF header). May be empty if the name is blank/spaces. |
| `type` | string | One of `"code"`, `"data"`, `"jumptable"`, or `"other"`. Derived from the low 5 bits of the OMF segment type field. |
| `org` | string | Segment origin address as an 8-digit hex string. |
| `length` | string | Segment length in bytes as an 8-digit hex string. |

### Symbol object

```json
{
  "name": "MAIN",
  "segment": "0x0001",
  "offset": "0x00000000",
  "global": true
}
```

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Symbol name as stored in the linker's symbol table (uppercase, ORCA convention). |
| `segment` | string | Segment number where this symbol is defined, as a 4-digit hex string. |
| `offset` | string | Offset within the segment as an 8-digit hex string. This is the raw `symVal` from the linker's hash table — it is the value assigned to the symbol during pass 1 (typically the program counter at the point of definition). |
| `global` | boolean | `true` if the symbol has global linkage, `false` if private/local. |

### Numeric encoding

All numeric values are encoded as hex strings with a `0x` prefix:

- 16-bit values: `"0xHHLL"` (4 hex digits, e.g. `"0x0001"`)
- 32-bit values: `"0xHHHHLLLL"` (8 hex digits, e.g. `"0x00004A00"`)

### Filtering

The symbol list includes **all** symbols that are:

- Resolved on pass 2 (`symFlag & pass2Resolved`)
- NOT segment-name symbols (`symFlag & isSegmentFlag` = 0)

Both global and private symbols are included. Segment-name symbols (which
are bookkeeping entries the linker creates for each OMF segment name) are
excluded to reduce noise.

## Example

Given a simple program with one code segment:

```bash
iix -DgsplusSymbols=1 link obj/hello keep=Hello
```

Produces `Hello.symbols`:

```json
{"orca_symbols_version":"0x0001","symsig":"0xDEADBEEF","target":"Hello","linker":"ORCA/M Link Editor 2.3.0","segments":[{"number":"0x0002","name":"Hello","type":"code","org":"0x00000000","length":"0x00000108"}],"symbols":[{"name":"MAIN","segment":"0x0001","offset":"0x00000008","global":true},{"name":"_HELPER","segment":"0x0001","offset":"0x00000088","global":false}]}
```

Pretty-printed for readability:

```json
{
  "orca_symbols_version": "0x0001",
  "symsig": "0xDEADBEEF",
  "target": "Hello",
  "linker": "ORCA/M Link Editor 2.3.0",
  "segments": [
    {
      "number": "0x0002",
      "name": "Hello",
      "type": "code",
      "org": "0x00000000",
      "length": "0x00000108"
    }
  ],
  "symbols": [
    {
      "name": "MAIN",
      "segment": "0x0001",
      "offset": "0x00000008",
      "global": true
    },
    {
      "name": "_HELPER",
      "segment": "0x0001",
      "offset": "0x00000088",
      "global": false
    }
  ]
}
```

## Loading the symbol file in GSplus

To use these symbols in the GSplus trace window:

1. Build the target with `iix -DgsplusSymbols=1 link ...`
2. The `.symbols` file will be created alongside the binary
3. GSplus reads the file and maps addresses to symbol names in the trace
   output

### Address resolution

To resolve a runtime address to a symbol name:

1. Determine which segment the address falls in by checking each segment's
   `org` and `length` — the address is in segment S if
   `org <= address < org + length`.
2. Compute the segment-relative offset: `offset = address - org`.
3. Find the symbol whose `offset` is the largest value <= the computed
   offset. That symbol is the best match. The remaining difference
   (`computed_offset - symbol_offset`) is the displacement within the
   function/label.
4. Display as `SYMBOL_NAME+displacement` (e.g., `MAIN+0x0012`).

### Notes for emulator implementers

- The file is a single line of JSON. No newlines within the data. Parse
  with any standard JSON library.
- All hex strings use uppercase `A`–`F` for hex digits.
- The `orca_symbols_version` field allows for future format changes.
  Current version is `"0x0001"`. If the version is unrecognised, the
  emulator should ignore the file rather than crash.
- `symsig` is a 32-bit value encoded as an 8-digit hex string with `0x`
  prefix. The same value appears little-endian at bytes 4–7 of the WDM
  prologue in the binary. To read it from the binary: treat the 4 bytes
  starting at `PC+4` (where PC points to the WDM instruction) as a
  little-endian 32-bit integer. That integer equals
  `strtoul(symsig, NULL, 16)`.
- Symbol names are stored in uppercase per ORCA convention (ORCA/M
  assembler uppercases all identifiers). Source-level names like `main`
  or `_helper` become `MAIN` and `_HELPER`.
- All symbol `offset` values in segment 1 are ≥ 8 because the 8-byte
  WDM prologue is at offsets 0–7. The first user symbol starts at
  offset `0x00000008`.
- The `segment` field in symbol objects refers to the segment number
  assigned during pass 1 of the link. This may differ from the final
  segment `number` in the segments array by 1 (due to the linker's
  internal express-load segment numbering). For single-segment links,
  symbols typically show `segment: "0x0001"` while the emitted segment
  shows `number: "0x0002"`. This is a known quirk — treat all symbols
  as belonging to the output segment for address-resolution purposes.
- The `offset` field is the raw pass-1 value from the linker's symbol
  table. For most code and data labels this is the segment-relative
  offset. For constants defined via `equ`/`gequ`, the value is the
  constant itself (not a memory address). The `global` field can help
  distinguish: most `equ` constants are global and have small values
  that don't fall within any segment's address range.

## Known limitations

- **Segment name may be empty.** The linker emits the current segment's
  name from the `loadName` global at Terminate time, which may be blank
  if KeepFile cleared it during processing. The segment's `number`,
  `type`, `org`, and `length` are always valid.
- **Single-segment links only emit one segment.** Multi-segment links
  emit the "current" segment plus any remaining in `loadList`. In
  practice most ORCA programs link to a single segment.
- **No source-file or line-number information.** The linker's symbol
  table does not track source locations.
