# clinker: LOADNAME and LENGTH field drift from stock `iix link`

## TL;DR

After the KIND-drift fix, two more structural differences remain that
plausibly cause GS/OS loader error `$1101` on the gno kernel:

1. **LOADNAME not blanked on consolidated load files.** Stock `iix link`
   emits LOADNAME = 10 spaces ("") for *every* segment when producing a
   single-load-file output. Clinker copies each input segment's LOADNAME
   verbatim, so most output segments end up with LOADNAME = their
   SEGNAME (e.g. "KERN2", "~_STACK").

2. **LENGTH field underestimates for SUPER-packed CODE segments.**
   Stock and clinker agree on LENGTH for segments where clinker did
   not apply SUPER packing (ExpressLoad, ~_STACK, KERN3). They
   disagree on CODE segments where clinker's SUPER packing kicked in
   (KERN2, the unnamed segment). Delta is *larger* than the body-size
   delta would suggest, implying clinker is double-counting SUPER
   payload in its LENGTH subtraction.

The LENGTH discrepancy is the more likely culprit for `$1101` since
the GS/OS loader uses LENGTH to size the memory allocation for each
segment. Too-small LENGTH → loader can't complete segment placement
→ load rejected.

## Evidence — gno kernel

Same clean-build comparison as BUG_KIND_DRIFT.md.

### LOADNAME field

```
=== gno_obj.stock/kern ===   (stock iix link, loads cleanly)
  seg='~ExpressLoad'  LOADNAME=''           LENGTH=322
  seg='KERN2'         LOADNAME=''           LENGTH=65516
  seg='~_STACK'       LOADNAME=''           LENGTH=1024
  seg=''              LOADNAME=''           LENGTH=63746
  seg='KERN3'         LOADNAME=''           LENGTH=15231

=== gno_obj/kern ===         (clinker, fails $1101 at launch)
  seg='~ExpressLoad'  LOADNAME=''           LENGTH=322      ← matches
  seg='KERN2'         LOADNAME='KERN2'      LENGTH=61414    ← LOADNAME WRONG, LENGTH WRONG
  seg='~_STACK'       LOADNAME='~_STACK'    LENGTH=1024     ← LOADNAME WRONG, length OK
  seg=''              LOADNAME=''           LENGTH=58439    ← LENGTH WRONG
  seg='KERN3'         LOADNAME='KERN3'      LENGTH=15231    ← LOADNAME WRONG, length OK
```

Stock blanks LOADNAME on every segment — the idiom says "this segment
is part of the containing load file; there is no separate load file
to reference." Clinker preserves the ORCA/C-default LOADNAME = SEGNAME
that came in from the input .a/.root files.

### LENGTH field

| Segment | Stock LENGTH | Clinker LENGTH | Δ | Note |
|---|---:|---:|---:|---|
| `~ExpressLoad` | 322 | 322 | 0 | clinker writes ExpressLoad anew, LENGTH correct |
| `KERN2` | 65,516 | 61,414 | **−4,102** | CODE; clinker used SUPER packing |
| `~_STACK` | 1,024 | 1,024 | 0 | DP/stack; no SUPER packing |
| (unnamed) | 63,746 | 58,439 | **−5,307** | CODE; clinker used SUPER packing |
| `KERN3` | 15,231 | 15,231 | 0 | small CODE; SUPER packing not applied |

`LENGTH` per the OMF v2 spec: "memory size after loading; includes
RESSPC." It's the number of bytes the loader will allocate + lay
out + patch for this segment.

Body-size deltas for the two CODE segments: KERN2 -3,828 B, unnamed
-4,890 B. **LENGTH deltas are LARGER** than the body deltas by 274 B
and 417 B respectively. That rules out "LENGTH just follows body
size." It looks like clinker is subtracting the *entire SUPER record
payload* from LENGTH, whereas it should subtract only the SUPER
overhead bytes (count/page markers), because the relocation values
packed inside SUPER are still meant to occupy memory post-load.

Or equivalently: the LCONST data that SUPER encodes relocations for
still contributes to LENGTH; clinker may be forgetting to include it.

## Why this plausibly causes `$1101`

The GS/OS system loader error codes in the `$1100` range are all
loader-internal failures. `$1101` specifically signifies a load-file
validation or placement failure. Two loader paths could trip on our
data:

- **LENGTH too small**: loader reserves LENGTH bytes, then starts
  laying out LCONST data from the body. If the emitted LCONST stream
  contains more bytes than LENGTH claims, the loader overshoots its
  allocated region and aborts.

- **LOADNAME mismatch between ExpressLoad's cached header copy and
  the on-disk segment header**. ExpressLoad stores a "segment header
  copy minus first 12 bytes" for each segment. If clinker regenerates
  ExpressLoad consistently with the stored LOADNAMEs, it's
  self-consistent — so this is the less likely failure mode. Still
  worth checking.

## Fix location — LOADNAME

`~/source/orca/clinker/out.c` (segment header writer) — wherever it
emits the LOADNAME field. Change:

```c
/* Old */
write_bytes(out, outseg->loadName, 10);  /* space-padded */
```

to

```c
/* New: match iix link — single-load-file output always has blank LOADNAME */
memset(pad, ' ', 10);
write_bytes(out, pad, 10);
```

Or, more faithfully: if clinker is targeting a single output file
(the common case — only multi-load-file output via run-time-library
paths needs populated LOADNAMEs), force LOADNAME to blank in the
output-writer regardless of what came from the input segment.

If multi-load-file output is ever a thing, this needs thought — but
for the gno build and the ORCA tool builds, all outputs are
single-load-file and should have blank LOADNAME.

## Fix location — LENGTH

Also in `out.c`, wherever it writes the LENGTH header field.
Compare against the equivalent calculation the ORCA/M linker does.
Candidates to inspect:

- Is `out->length` being decremented anywhere during SUPER emission?
- Is the post-pass-2 `out->length` the in-memory image size, or is
  it getting reduced to match the compacted disk-body size?

Possibly the fix is to track *two* lengths per OutSeg: `memLength`
(for the LENGTH header field) and `bodyLength` (for BYTECNT).
Currently clinker may be conflating them.

## Verify the fix

After rebuilding clinker and rebuilding the gno kernel:

```
python3 - <<'PY'
import struct
def walk(p):
    d=open(p,'rb').read(); off=0; s=[]
    while off<len(d):
        if off+44>len(d): break
        bc=struct.unpack_from('<I',d,off)[0]
        ln_len=struct.unpack_from('<I',d,off+8)[0]
        dn=struct.unpack_from('<H',d,off+40)[0]
        loadname=d[off+dn:off+dn+10].rstrip(b'\x00\x20').decode('ascii','replace')
        pl=d[off+dn+10] if off+dn+10<len(d) else 0
        segname=d[off+dn+11:off+dn+11+pl].decode('ascii','replace').strip()
        s.append((segname, loadname, ln_len))
        if bc==0: break
        off+=bc
    return s
for p in ['gno_obj.stock/kern','gno_obj/kern']:
    print(p)
    for sn,ln,length in walk(p):
        print(f"  {sn!r:20s} LOADNAME={ln!r:10s} LENGTH={length}")
PY
```

Expected: LOADNAME = "" for every segment in both files, and
LENGTH identical between stock and clinker for every segment.

Then: try booting `diskImages/gno-built.2mg` (built with
`USE_CLINKER=1 bash goldengate/build/rebuild-all.sh`). Should clear
`$1101`.

## Data on hand

- `gno_obj.stock/kern` — stock iix link, boots
- `gno_obj/kern` — current clinker, `$1101`
- `diskImages/gno-built.stock.2mg` — stock disk image (boots)
- `diskImages/gno-built.2mg` — current clinker disk image (`$1101`)
