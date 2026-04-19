# clinker: one output segment's LENGTH field is still 62 B short

## Status

The LOADNAME fix and the primary LENGTH fix are **in**. Four of five
gno kernel segments now have BYTECNT/LENGTH/LOADNAME matching stock
exactly. One segment — the unnamed-SEGNAME code segment 4 — still
has LENGTH short by **62 bytes**, and the gno kernel still fails to
load with GS/OS `$1101`.

## Evidence

Clean gno kernel build (`gno_obj/kern`, 157,601 B) vs stock reference
(`gno_obj.stock/kern`, 157,482 B):

```
=== stock/kern ===
  SEGNAME       LOADNAME   BYTECNT   LENGTH    BODY    RELOC_overhead
  ~ExpressLoad              395       322       328       6
  KERN2                     70007     65516     69938     4422
  ~_STACK                   1099      1024      1030      6
  (unnamed)                 69405     63746     69336     5590   <---
  KERN3                     16576     15231     16507     1276

=== clinker/kern ===
  SEGNAME       LOADNAME   BYTECNT   LENGTH    BODY    RELOC_overhead
  ~ExpressLoad              395       322       328       6
  KERN2                     70283     65516     70214     4698
  ~_STACK                   1099      1024      1030      6
  (unnamed)                 69764     63684     69695     6011   <---
  KERN3                     16060     15231     15991     760
```

Segment 2 (KERN2), segment 3 (~_STACK), and segment 5 (KERN3) all have
matching LENGTH fields (65516, 1024, 15231). Segment 4 (unnamed) is
**62 bytes short** in clinker's output (63684 vs 63746).

The BODY-minus-LENGTH relationship is internally consistent in each
build ("reloc overhead" column), so clinker isn't miscomputing the
subtraction — it's emitting 62 fewer bytes of LCONST-equivalent
content in segment 4. LENGTH tracks "in-memory size after load" =
sum of LCONST + DS bytes; clinker is omitting or truncating 62 bytes
of one of those.

## Why this almost certainly causes `$1101`

`$1101` is the GS/OS System Loader's general-purpose load-failure
error. The loader:
1. Reads the segment header.
2. Allocates LENGTH bytes of memory.
3. Lays out LCONST/DS data into that memory, applying RELOC patches
   as it goes.

If the LCONST stream for this segment extends 62 bytes past LENGTH's
claimed bounds, the loader aborts with `$1101`.

## What 62 bytes looks like

Candidates for the missing content:
- A trailing **DS (define space / zero-fill) record** at the end of
  the segment that clinker's output writer skips when the segment
  has no explicit ENTRY or EOB.
- A **final LCONST whose length record is correct but whose data
  buffer is truncated** during segment finalization — e.g. a
  GrowData boundary bug.
- An **alignment pad** between the last code byte and the
  segment-end boundary that the ORCA/M linker writes but clinker
  skips.

62 is not a power-of-2 or any common alignment size, so it's probably
not a simple alignment pad. More likely: a specific DS record at
segment tail. A direct-page or zero-init region for globals is a
common trailing construct.

Segment 4 is the unnamed default root segment (SEGNAME = ""), which
in ORCA/C output typically holds module-init glue + globals. If
there's a trailing zero-init block for uninitialised globals, losing
it would explain both the 62-byte LENGTH shortfall and a kernel that
fails to load (uninitialised-global pointers have garbage instead of
zero).

## Reproducer

```
USE_CLINKER=1 GSPLUS_SYMBOLS= bash goldengate/build/rebuild-all.sh

python3 - <<'PY'
import struct
def walk(p):
    d=open(p,'rb').read(); o=0; r=[]
    while o<len(d):
        if o+44>len(d): break
        bc=struct.unpack_from('<I',d,o)[0]
        lg=struct.unpack_from('<I',d,o+8)[0]
        dd=struct.unpack_from('<H',d,o+42)[0]
        dn=struct.unpack_from('<H',d,o+40)[0]
        pl=d[o+dn+10]
        sn=d[o+dn+11:o+dn+11+pl].decode('ascii','replace').strip()
        r.append((sn, bc, lg, bc-dd))
        if bc==0: break
        o+=bc
    return r
for p in ['gno_obj.stock/kern','gno_obj/kern']:
    print(p)
    for sn,bc,lg,body in walk(p):
        print(f"  {sn!r:14s} BYTECNT={bc} LENGTH={lg} BODY={body}")
PY
```

Expected output after fix: LENGTH identical across both builds for
every kernel segment.

## Scope

Only kernel segment 4 currently misbehaves. Other binaries in the
gno tree produce smaller output than stock due to SUPER packing, but
their LENGTH fields are fine — that's intentional compression.

## Follow-on

Once this 62-byte shortfall is fixed, also spot-check `bin/egrep`
and `bin/less` for the residual regex-segment KIND swap (see
BUG_KIND_DRIFT.md — `regex_2___` / `regex_4___` swap their `$4000`
bits). That's not the `$1101` blocker but it's the last known
correctness divergence.

## Data on hand

- `gno_obj.stock/kern` — stock iix link, kernel 157,482 B, boots.
- `gno_obj/kern` — current clinker, kernel 157,601 B, `$1101`.
- `diskImages/gno-built.2mg` — current clinker image (`$1101`).
- `diskImages/gno-built.stock.2mg` — stock image (boots).
