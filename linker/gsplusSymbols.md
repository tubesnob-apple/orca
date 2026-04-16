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

When the variable is unset or empty, WriteSymbolFile returns immediately
without creating the file. The linked binary is byte-identical whether
the gate is open or closed.

## Output file

The symbol file is placed alongside the linked binary, with `.symbols`
appended to the keep-file name:

```
iix -DgsplusSymbols=1 link foo.a keep=MyProg
→ creates MyProg          (the linked binary)
→ creates MyProg.symbols  (the JSON symbol table)
```

If no `keep=` is specified (no output file), no symbol file is created.

## JSON format

The file contains a single line of JSON (no pretty-printing, no trailing
newline). The top-level object has these fields:

```json
{
  "orca_symbols_version": "0x0001",
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
{"orca_symbols_version":"0x0001","target":"Hello","linker":"ORCA/M Link Editor 2.3.0","segments":[{"number":"0x0002","name":"Hello","type":"code","org":"0x00000000","length":"0x00000100"}],"symbols":[{"name":"MAIN","segment":"0x0001","offset":"0x00000000","global":true},{"name":"_HELPER","segment":"0x0001","offset":"0x00000080","global":false}]}
```

Pretty-printed for readability:

```json
{
  "orca_symbols_version": "0x0001",
  "target": "Hello",
  "linker": "ORCA/M Link Editor 2.3.0",
  "segments": [
    {
      "number": "0x0002",
      "name": "Hello",
      "type": "code",
      "org": "0x00000000",
      "length": "0x00000100"
    }
  ],
  "symbols": [
    {
      "name": "MAIN",
      "segment": "0x0001",
      "offset": "0x00000000",
      "global": true
    },
    {
      "name": "_HELPER",
      "segment": "0x0001",
      "offset": "0x00000080",
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
- Symbol names are stored in uppercase per ORCA convention (ORCA/M
  assembler uppercases all identifiers). Source-level names like `main`
  or `_helper` become `MAIN` and `_HELPER`.
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
