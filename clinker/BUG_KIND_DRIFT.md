# clinker: output-segment KIND word drifts from stock `iix link`

## TL;DR

`FindOrCreateOutSeg` in `clinker.c` sets the output segment's KIND word
by a blanket rule (first strip `$4000`, then later preserve it on every
input). Stock `iix link` follows a different, more nuanced rule: **the
first input segment that contributes to a given output LOADNAME wins
— its full 16-bit KIND is carried to the output unchanged.** Any
subsequent input segments that merge into the same output LOADNAME do
not modify the already-fixed KIND, regardless of whether their own
KIND differs.

Clinker currently violates that rule in both directions, producing
141-segment worth of drift across a gno clean-build — including the
load-time-significant `$4000` bit on type-`$12` (direct-page / stack)
segments.

The most visible symptom: the gno kernel (`gno_obj/kern`) launches
successfully under stock `iix link` but fails at load time with GS/OS
`$1101` under clinker.

## Timeline

| Clinker revision | `FindOrCreateOutSeg` policy | Result |
|---|---|---|
| Original (`clinker.c:111`, `s->kind = kind & ~0x4000`) | always strip `$4000` | `~_STACK kind=0x0012` → GS/OS `$1101` on kernel |
| Current fix (`s->kind = kind`, `$4000` flow-through) | always preserve `$4000` | `~_STACK` now `0x4012` (correct), but 150+ CODE segments pick up stray `$4000` bit → different runtime failure |

Neither rule is what stock does; both approximate it.

## Data

From a clean gno build (`gno_obj.stock/` = stock `iix link`,
`gno_obj/` = current clinker after fix):

```
=== KIND mismatches across 140 differing binaries, aggregated by (stock_kind, clinker_kind, segname) ===
  stock=0x0000  clinker=0x4000  seg=''                        count=126
  stock=0x0000  clinker=0x4000  seg='apropos2__'              count=3
  stock=0x0000  clinker=0x4000  seg='regex_2___'              count=2
  stock=0x4000  clinker=0x0000  seg='regex_4___'              count=2
  stock=0x0000  clinker=0x4000  seg='apropos___'              count=2
  stock=0x0000  clinker=0x4000  seg='common____'              count=2
  stock=0x0000  clinker=0x4000  seg='HUSH_A____'              count=1
  stock=0x0000  clinker=0x4000  seg='HUSH_C____'              count=1
  stock=0x0000  clinker=0x4000  seg='libc_gno__'              count=1
  stock=0x0000  clinker=0x4000  seg='regexp'                  count=1
  stock=0x0000  clinker=0x4000  seg='search'                  count=1
  stock=0x0000  clinker=0x4000  seg='KERN2'                   count=1
  stock=0x0000  clinker=0x4000  seg='KERN3'                   count=1
  stock=0x0000  clinker=0x4000  seg='run'                     count=1
  stock=0x0000  clinker=0x4000  seg='lex'                     count=1
  stock=0x0000  clinker=0x4000  seg='b'                       count=1
  stock=0x0000  clinker=0x4000  seg='lib'                     count=1
  stock=0x0000  clinker=0x4000  seg='tran'                    count=1
  stock=0x0000  clinker=0x4000  seg='man_______'              count=1
  stock=0x0000  clinker=0x4000  seg='main______'              count=1
  stock=0x0000  clinker=0x4000  seg='compile'                 count=1
  stock=0x0000  clinker=0x4000  seg='process'                 count=1
  stock=0x0000  clinker=0x4000  seg='catman____'              count=1
  stock=0x0000  clinker=0x4000  seg='makewhatis'              count=1
```

Direction split:

- **≈150 output segments** where clinker now has `$4000` set but stock
  has it clear. Almost all are CODE segments (type `$00`) with names
  like `KERN2`, `main______`, `lex`, `tran`. The private bit on CODE
  is linker-stage visibility; stock apparently clears it because the
  *second* input segment contributing to that LOADNAME lacked the
  bit, and clinker now lets the first-seen bit stick.
- **2 output segments** (`regex_4___`) in the opposite direction:
  stock has `$4000` set, clinker clears it. Same root cause — order
  of input-segment merging — just producing the other direction.

The kernel's `~_STACK` (type `$12`) now matches stock at `$4012`, so
the specific failure that triggered this bug-hunt is resolved, but
the kernel still crashes, suggesting the CODE-segment `$4000` drift
or some other structural difference is the current blocker.

## Stock `iix link` behavior (hypothesis, matches observed data)

Stock linker implements "first-seen KIND wins":

```
for each input OMF segment I in order:
    o = find_out_seg_by_loadname(I.loadname)
    if o is None:
        o = new_out_seg()
        o.kind = I.kind        # preserve *exactly*, including $4000
    # Merging subsequent I into o: append body, track relocs,
    # DO NOT touch o.kind.
```

In particular, the `$4000` (private) flag from the first contributing
input is preserved verbatim; no subsequent merge changes it.

Clinker's `FindOrCreateOutSeg` already calls out "first-seen KIND wins"
in its top comment (`clinker.c:88-92`), but the code right below it
on line ~111 applies a policy to `s->kind` that departs from that
rule.

## Fix location

`~/source/orca/clinker/clinker.c`, inside `FindOrCreateOutSeg`:

```c
OutSeg *FindOrCreateOutSeg(const char *loadName, const char *segName,
                            word kind)
{
OutSeg *s;
for (s = outSegs; s; s = s->next) {
    if (strcmp(s->loadName, loadName) == 0)
        return s;           // existing: do NOT modify s->kind
    }

/* Create new. Capture the full 16-bit KIND from the first contributor. */
s = (OutSeg *)malloc(sizeof(OutSeg));
...
s->segType = (word)(kind & 0x1F);
s->kind    = kind;          // preserve verbatim — no $4000 strip, no mask
s->banksize = 0x10000L;
...
}
```

Key requirement: on the **first** call for a given `loadName`, take
`kind` *unchanged*. Subsequent calls with the same `loadName` must
early-return without touching `s->kind` (they already do). No
blanket mask either direction.

## Verify the fix

After clinker rebuild + install:

```
# Stock reference build
make -C goldengate clinker-restore      # optional if the install uses clink builtin directly
GSPLUS_SYMBOLS= bash goldengate/build/rebuild-all.sh
rsync -a --delete gno_obj/ gno_obj.stock/

# Clinker build
USE_CLINKER=1 GSPLUS_SYMBOLS= bash goldengate/build/rebuild-all.sh

# Compare — expect zero KIND mismatches
python3 - <<'PY'
import struct, os
def walk(p):
    d=open(p,'rb').read(); off=0; s=[]
    while off<len(d):
        if off+44>len(d): break
        bc=struct.unpack_from('<I',d,off)[0]; k=struct.unpack_from('<H',d,off+20)[0]
        dn=struct.unpack_from('<H',d,off+40)[0]; po=off+dn
        pl=d[po+10] if po<len(d) else 0
        n=d[po+11:po+11+pl].decode('ascii','replace').strip()
        s.append((n,k,bc))
        if bc==0: break
        off+=bc
    return s
import pathlib
mm=0
for f in pathlib.Path('gno_obj.stock').rglob('*'):
    if not f.is_file(): continue
    r=f.relative_to('gno_obj.stock'); c=pathlib.Path('gno_obj')/r
    if not c.exists(): continue
    try:
        a=walk(str(f)); b=walk(str(c))
    except: continue
    if len(a)!=len(b): continue
    for (sn,sk,sb),(cn,ck,cb) in zip(a,b):
        if sk!=ck: mm+=1
print(f"kind mismatches: {mm}")
PY
```

Expected after fix: `kind mismatches: 0`.

Also verify the gno kernel boots under `USE_CLINKER=1` build — the
`$1101` failure should be gone.

## Not in scope

Body-size deltas (clinker's output is ~1-4% smaller than stock on
most binaries) are legitimate SUPER-packing / cRELOC optimizations
per the clinker docs and should NOT be reverted. Only the KIND-word
mismatch is a correctness problem.

## Reproducer one-liner

```
diff <(python3 /tmp/omfsegs.py gno_obj.stock/kern) \
     <(python3 /tmp/omfsegs.py gno_obj/kern)
```

where `/tmp/omfsegs.py` is the 23-line walker used in the original
comparison (available in this session's history).
