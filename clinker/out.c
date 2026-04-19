/**************************************************************
*
*  out.c -- Output load file writer
*
*  Produces the final OMF v2 load file from the in-memory
*  OutSeg list built up by passes 1 and 2.
*
*  Per Appendix F Table F-3, load segments may contain only
*  LCONST, DS, RELOC, cRELOC, INTERSEG, cINTERSEG, SUPER, and
*  END records — so the body emission here is deliberately
*  narrow.
*
*  When opt_express is set, an ExpressLoad-style stub segment
*  ($8001 KIND, name "~ExpressLoad") is prepended.  We write a
*  zero-filled placeholder at file offset 0, emit the real
*  segments after it, then seek back to fill in the stub's
*  body with the correct data-segment offset.
*
**************************************************************/

#pragma keep "out"
#pragma optimize 9

#include "clinker.h"

/* ── Body size calculation ─────────────────────────────────────────────── */

/*
 * BodyBytes — number of bytes the body of `seg` will occupy when written
 * with WriteSegBody, *excluding* the fixed segment header.
 *
 *   data:     LCONST ($F2) + 4-byte length + dataLen bytes
 *   relocs:   bytes reported by OmfSuperBytes (covers SUPER packing of
 *             RELOC2/RELOC3 plus individual cRELOC/RELOC/cINTERSEG/
 *             INTERSEG records for everything that didn't pack)
 *   trailer:  END ($00) = 1 byte
 *
 * An empty segment still has an END, so the minimum is 1.
 */
static long BodyBytes(const OutSeg *seg)
{
long n = 0;

n += OmfSuperBytes(seg);

if (seg->dataLen > 0)
    n += 1L + 4L + seg->dataLen + 1L;    /* LCONST frame + END */
else
    n += 1L;                              /* END only */

return n;
}

/* ── Body emission ─────────────────────────────────────────────────────── */

/*
 * WriteSegBody — write the body records for one output segment.
 * The matching segment header must already have been written by the
 * caller.  Emits LCONST (if any data), relocation records, then END.
 */
static void WriteSegBody(FILE *fp, OutSeg *seg)
{
/* SUPER RELOC2/RELOC3 read their reference values straight out of the
 * LCONST at each patch location, so we must write those values into
 * seg->data before emitting the LCONST.  For entries that won't be
 * SUPER-packed, OmfPrepareSuper is a no-op. */
OmfPrepareSuper(seg);

if (seg->dataLen > 0) {
    OmfWriteByte (fp, OP_LCONST);
    OmfWriteDword(fp, seg->dataLen);
    fwrite(seg->data, 1, (size_t)seg->dataLen, fp);
    }
OmfWriteSuper(fp, seg);   /* SUPER for RELOC2/3, cRELOC/cINTERSEG for rest */
OmfWriteByte(fp, OP_END);
}

/*
 * WriteSeg — emit one complete output segment (header + body).
 */
static void WriteSeg(FILE *fp, OutSeg *seg, int segNum)
{
OmfWriteSegHeader(fp, seg, BodyBytes(seg), segNum, seg->loadName);
WriteSegBody(fp, seg);
}

/* ── ExpressLoad emission ──────────────────────────────────────────────── */
/*
 * When opt_express is set, we prepend a KIND=$8001 "~ExpressLoad"
 * segment as segment #1 and renumber the real data segments from
 * 2..N+1.  The ExpressLoad segment carries a LCONST body encoding
 * per-segment file offsets so the GS/OS loader can go straight to
 * each segment's data without re-parsing the whole file.
 *
 * Shape, per GS/OS Reference Appendix F p.470 and "Undocumented
 * Secrets of the Apple IIGS System Loader" (Neil Parker):
 *
 *   ExpressLoad header (v2 format — DISPNAME=$2C, no tempORG):
 *     44 fixed bytes ending in DISPNAME/DISPDATA
 *     LOADNAME: 10 bytes (space-padded)
 *     SEGNAME:  pstring "~ExpressLoad" (len 12 + 12 chars)
 *     total header = 67 bytes
 *
 *   Body: LCONST opcode + 4-byte length + LCONST data + END
 *
 *   LCONST data:
 *     word   file refnum       (0 on disk)
 *     word   reserved           (0)
 *     word   numSegments - 2    (so N=1 real segment -> 0)
 *     8*N    segment list:  (word self-rel-to-info, word flags=0, dword 0)
 *     2*N    remap list:    word new_segnum (post-Express, so i+2 for each
 *                                  i = 1..N with no reordering)
 *     69*N   header info entries, each 16 bytes of file offsets plus 53
 *                                  bytes of header-copy:
 *                dword  lconst-data file offset
 *                dword  lconst-data length
 *                dword  first-reloc file offset (0 if none)
 *                dword  total reloc bytes
 *                53 bytes — original v2.1 seg header from offset 12, with
 *                  the 4-byte tempORG field stripped and DISPNAME adjusted
 *                  from 48 to 44 and DISPDATA zeroed.
 */

/* Per-segment data collected during the first pass so we can build the
 * ExpressLoad segment in the second pass. */
typedef struct {
    long segFileOffset;   /* where the segment header starts in the file */
    long lconstOffset;    /* file offset of the LCONST data (post-prefix) */
    long lconstLength;
    long relocOffset;     /* file offset of first reloc record (0 if none) */
    long relocLength;
} ExprInfo;

#define EXPR_HDR_BYTES         67L                 /* ExpressLoad seg header */
#define EXPR_PREAMBLE_BYTES     6L                 /* refnum + reserved + N-2 */
#define EXPR_SEGLIST_PER_ENTRY  8L
#define EXPR_REMAP_PER_ENTRY    2L
#define EXPR_HDRCOPY_BYTES     53L                 /* for our v2.1 seg headers */
#define EXPR_INFO_FIXED_BYTES  16L                 /* 4 dwords of file offsets */
#define EXPR_INFO_PER_ENTRY    (EXPR_INFO_FIXED_BYTES + EXPR_HDRCOPY_BYTES)

/* CountOutSegs — number of data segments currently in outSegs. */
static int CountOutSegs(void)
{
int n = 0;
OutSeg *s;
for (s = outSegs; s; s = s->next) n++;
return n;
}

/* ExpressLCONSTBytes — body of the ExpressLoad LCONST record, given N
 * data segments (each contributes one entry throughout). */
static long ExpressLCONSTBytes(int n)
{
return EXPR_PREAMBLE_BYTES
     + n * EXPR_SEGLIST_PER_ENTRY
     + n * EXPR_REMAP_PER_ENTRY
     + n * EXPR_INFO_PER_ENTRY;
}

/* ExpressSegmentBytes — total BYTECNT value for the ExpressLoad segment
 * covering N data segments:
 *   header(67) + LCONST(opcode+len+data)(5+data) + END(1)
 *             = 67 + 5 + (6 + 79N) + 1 = 79 + 79N */
static long ExpressSegmentBytes(int n)
{
return EXPR_HDR_BYTES + 1L + 4L + ExpressLCONSTBytes(n) + 1L;
}

/* WriteExpressHeader — emit the ExpressLoad segment header (v2 shape).
 * Writes exactly 67 bytes at the current file position. */
static void WriteExpressHeader(FILE *fp, long lconstDataBytes)
{
static const char loadPad[10] = "          ";
static const char segName[13] = "~ExpressLoad";

long totalBytes = EXPR_HDR_BYTES + 1 + 4 + lconstDataBytes + 1;

OmfWriteDword(fp, totalBytes);          /* BYTECNT */
OmfWriteDword(fp, 0L);                  /* RESSPC */
OmfWriteDword(fp, lconstDataBytes);     /* LENGTH = LCONST data size */
OmfWriteByte (fp, 0);                   /* unused */
OmfWriteByte (fp, 0);                   /* LABLEN */
OmfWriteByte (fp, 4);                   /* NUMLEN */
OmfWriteByte (fp, 2);                   /* VERSION */
OmfWriteDword(fp, 0x10000L);            /* BANKSIZE */
OmfWriteWord (fp, 0x8001);              /* KIND (dynamic data, so pre-5.0 loaders skip) */
OmfWriteWord (fp, 0);                   /* unused */
OmfWriteDword(fp, 0L);                  /* ORG */
OmfWriteDword(fp, 0L);                  /* ALIGN */
OmfWriteByte (fp, 0);                   /* NUMSEX */
OmfWriteByte (fp, 0);                   /* LANG */
OmfWriteWord (fp, 1);                   /* SEGNUM (ExpressLoad is #1) */
OmfWriteDword(fp, 0L);                  /* ENTRY */
OmfWriteWord (fp, 44);                  /* DISPNAME ($2C — v2 format) */
OmfWriteWord (fp, 67);                  /* DISPDATA (44 + 10 + 13) */
fwrite(loadPad,  1, 10, fp);            /* LOADNAME: 10 spaces */
OmfWriteByte (fp, 12);                  /* SEGNAME pstring length */
fwrite(segName,  1, 12, fp);            /* SEGNAME: "~ExpressLoad" */
}

/* PutLE — little-endian byte-packing helper for in-memory buffers. */
static void PutLE(byte *buf, long off, long value, int n)
{
int i;
for (i = 0; i < n; i++) buf[off + i] = (byte)((value >> (i * 8)) & 0xFF);
}

/* BuildHeaderCopy — write the 53-byte header-copy image for one data
 * segment into `dst`.  Mirrors our own v2.1 OmfWriteSegHeader output,
 * minus the first 12 bytes (BYTECNT/RESSPC/LENGTH), minus the 4-byte
 * tempORG (copy is a v2-shaped reconstruction), with DISPDATA zeroed.
 *
 * Resulting layout (53 bytes):
 *   0   unused byte            (0)
 *   1   LABLEN                  (0)
 *   2   NUMLEN                  (4)
 *   3   VERSION                 (2)
 *   4   BANKSIZE                (4 bytes)
 *   8   KIND                    (2 bytes)
 *  10   unused                  (2 bytes)
 *  12   ORG                     (4 bytes)
 *  16   ALIGN                   (4 bytes)
 *  20   NUMSEX                  (1 byte)
 *  21   LANG                    (1 byte)
 *  22   SEGNUM                  (2 bytes)
 *  24   ENTRY                   (4 bytes)
 *  28   DISPNAME = 44 ($2C)     (2 bytes)
 *  30   DISPDATA = 0            (2 bytes)
 *  32   LOADNAME                (10 bytes, space-padded)
 *  42   SEGNAME pstring len     (1 byte = 10)
 *  43   SEGNAME chars           (10 bytes, = LOADNAME)
 */
static void BuildHeaderCopy(byte *dst, OutSeg *seg, int postExprSegNum)
{
int  i;
char pad[10];

memset(dst, 0, EXPR_HDRCOPY_BYTES);

dst[1] = 0;                                         /* LABLEN */
dst[2] = 4;                                         /* NUMLEN */
dst[3] = 2;                                         /* VERSION */
PutLE(dst,  4, seg->banksize ? seg->banksize
                             : 0x10000L,   4);      /* BANKSIZE */
PutLE(dst,  8, (long)(seg->kind ? seg->kind
                                : seg->segType), 2);/* KIND */
PutLE(dst, 12, seg->org,                    4);     /* ORG */
PutLE(dst, 16, seg->align,                  4);     /* ALIGN */
dst[20] = 0;                                        /* NUMSEX */
dst[21] = 0;                                        /* LANG */
PutLE(dst, 22, (long)postExprSegNum,        2);     /* SEGNUM */
PutLE(dst, 24, 0L,                          4);     /* ENTRY */
PutLE(dst, 28, 44L,                         2);     /* DISPNAME adjusted (v2, no tempORG) */
PutLE(dst, 30, 69L,                         2);     /* DISPDATA — iix keeps the original v2.1
                                                     * value here (not 0 as Loader Secrets
                                                     * claims).  69 = 48+10+1+10. */

/* LOADNAME: 10 blank bytes (consolidated output). */
for (i = 0; i < 10; i++) pad[i] = ' ';
memcpy(dst + 32, pad, 10);

/* SEGNAME pstring: length 10, space-padded seg->loadName (see note in
 * OmfWriteSegHeader — stock uses the merge-key loadName as SEGNAME). */
{
int slen = (int)strlen(seg->loadName);
if (slen > 10) slen = 10;
for (i = 0; i < slen; i++) pad[i] = seg->loadName[i];
for (; i < 10; i++)        pad[i] = ' ';
}
dst[42] = 10;
memcpy(dst + 43, pad, 10);
}

/* WriteExpressBody — emit the LCONST frame + body + END.  Assumes the
 * header has just been written. */
static void WriteExpressBody(FILE *fp, const ExprInfo *info, int n)
{
long   lconstBytes = ExpressLCONSTBytes(n);
byte  *body;
long   off;
int    i;
OutSeg *seg;

/* Build the whole LCONST payload in memory, then write it. */
body = (byte *)calloc(1, (size_t)lconstBytes);
if (!body) FatalError("out of memory (ExpressLoad body)");

/* Preamble */
PutLE(body, 0, 0L,                    2);         /* file refnum */
PutLE(body, 2, 0L,                    2);         /* reserved */
PutLE(body, 4, (long)(n - 1),         2);         /* numSegs - 2, where
                                                   * numSegs counts the
                                                   * ExpressLoad itself, so
                                                   * for N data segments
                                                   * this stores N - 1. */

/* Segment list (8 bytes each). Self-relative offset points at each
 * entry's position in the header-info array. */
for (i = 0; i < n; i++) {
    long entryOff    = EXPR_PREAMBLE_BYTES + (long)i * EXPR_SEGLIST_PER_ENTRY;
    long infoStart   = EXPR_PREAMBLE_BYTES
                     + (long)n * EXPR_SEGLIST_PER_ENTRY
                     + (long)n * EXPR_REMAP_PER_ENTRY
                     + (long)i * EXPR_INFO_PER_ENTRY;
    long selfRel     = infoStart - entryOff;

    PutLE(body, entryOff + 0, selfRel, 2);
    PutLE(body, entryOff + 2, 0L,      2);         /* flags = 0 */
    PutLE(body, entryOff + 4, 0L,      4);         /* handle = 0 */
    }

/* Remap list (2 bytes each). Old segnum i+1 maps to new segnum i+2. */
{
long remapBase = EXPR_PREAMBLE_BYTES + (long)n * EXPR_SEGLIST_PER_ENTRY;
for (i = 0; i < n; i++)
    PutLE(body, remapBase + (long)i * EXPR_REMAP_PER_ENTRY,
          (long)(i + 2), 2);
}

/* Header-info array: 16 bytes of file offsets + 53 bytes of header copy. */
off = EXPR_PREAMBLE_BYTES
    + (long)n * EXPR_SEGLIST_PER_ENTRY
    + (long)n * EXPR_REMAP_PER_ENTRY;
for (seg = outSegs, i = 0; seg && i < n; seg = seg->next, i++) {
    PutLE(body, off + 0,  info[i].lconstOffset, 4);
    PutLE(body, off + 4,  info[i].lconstLength, 4);
    PutLE(body, off + 8,  info[i].relocOffset,  4);
    PutLE(body, off + 12, info[i].relocLength,  4);
    BuildHeaderCopy(body + off + EXPR_INFO_FIXED_BYTES, seg, i + 2);
    off += EXPR_INFO_PER_ENTRY;
    }

/* Emit the LCONST record wrapping this body. */
OmfWriteByte (fp, OP_LCONST);
OmfWriteDword(fp, lconstBytes);
fwrite(body, 1, (size_t)lconstBytes, fp);
OmfWriteByte(fp, OP_END);

free(body);
}

/* WriteDataSegmentWithInfo — emit one data segment at the current file
 * position, capturing the file offsets needed by the ExpressLoad
 * segment into *info.  segNum is the POST-remap segment number. */
static void WriteDataSegmentWithInfo(FILE *fp, OutSeg *seg, int segNum,
                                     ExprInfo *info)
{
long bodyLen = BodyBytes(seg);

info->segFileOffset = ftell(fp);
OmfWriteSegHeader(fp, seg, bodyLen, segNum, seg->loadName);

OmfPrepareSuper(seg);

if (seg->dataLen > 0) {
    OmfWriteByte (fp, OP_LCONST);
    OmfWriteDword(fp, seg->dataLen);
    info->lconstOffset = ftell(fp);
    info->lconstLength = seg->dataLen;
    fwrite(seg->data, 1, (size_t)seg->dataLen, fp);
    }
else {
    info->lconstOffset = 0;
    info->lconstLength = 0;
    }

{
long relocStart = ftell(fp);
OmfWriteSuper(fp, seg);
info->relocLength = ftell(fp) - relocStart;
info->relocOffset = (info->relocLength > 0) ? relocStart : 0;
}

OmfWriteByte(fp, OP_END);
}

/* ── Public entry point ────────────────────────────────────────────────── */

int WriteOutput(void)
{
FILE   *fp;
OutSeg *seg;
int     segNum;

if (!keepName[0]) return 1;    /* memory-only link: nothing to write */

fp = fopen(keepName, "wb");
if (!fp) {
    LinkError("cannot create output file", keepName);
    return 0;
    }

if (opt_express) {
    int       n        = CountOutSegs();
    long      exprSpan = ExpressSegmentBytes(n);
    ExprInfo *info;
    long      i;
    int       k;

    /* Reserve the full ExpressLoad segment span at file offset 0.
     * Data segments get written after, then we seek back and fill
     * the reserved region once we know each segment's file offset. */
    for (i = 0; i < exprSpan; i++) fputc(0, fp);

    info = (ExprInfo *)calloc((size_t)(n > 0 ? n : 1), sizeof(ExprInfo));
    if (!info) FatalError("out of memory (ExprInfo)");

    for (seg = outSegs, k = 0; seg; seg = seg->next, k++)
        WriteDataSegmentWithInfo(fp, seg, k + 2, &info[k]);

    /* Seek back and write the real ExpressLoad segment. */
    fseek(fp, 0L, SEEK_SET);
    WriteExpressHeader(fp, ExpressLCONSTBytes(n));
    WriteExpressBody(fp, info, n);

    free(info);
    }
else {
    segNum = 1;
    for (seg = outSegs; seg; seg = seg->next)
        WriteSeg(fp, seg, segNum++);
    }

fclose(fp);
return (numErrors == 0);
}
