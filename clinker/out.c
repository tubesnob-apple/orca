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
 *   reloc records: RELOC = 11 bytes, INTERSEG = 15 bytes
 *   data:          LCONST ($F2) + 4-byte length + dataLen bytes
 *   trailer:       END ($00) = 1 byte
 *
 * An empty segment still has an END, so the minimum is 1.
 */
static long BodyBytes(const OutSeg *seg)
{
long n = 0;
RelocRec *r;

for (r = seg->relocHead; r; r = r->next)
    n += OmfRelocSize(r);

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
if (seg->dataLen > 0) {
    OmfWriteByte (fp, OP_LCONST);
    OmfWriteDword(fp, seg->dataLen);
    fwrite(seg->data, 1, (size_t)seg->dataLen, fp);
    }
OmfWriteSuper(fp, seg);  /* emits each RelocRec as RELOC/INTERSEG */
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

/* ── ExpressLoad stub ──────────────────────────────────────────────────── */

/*
 * WriteExpressSegment — write the ExpressLoad stub at the current file
 * position.  KIND=$8001 (dynamic data so pre-System-5 loaders skip it),
 * name "~ExpressLoad", body is a tiny LCONST-style payload pointing at
 * the first real data segment.
 *
 * TODO: this is a placeholder shape only.  A real ExpressLoad segment
 * contains per-segment offset/header tables (see Loader Secrets + the
 * Appendix F notes).  Not used today — opt_express defaults to FALSE
 * and is never flipped on by any current input.
 */
static void WriteExpressSegment(FILE *fp, long dataSegOffset,
                                const char *loadName)
{
OutSeg stub;

memset(&stub, 0, sizeof(stub));
strcpy(stub.segName,  "~ExpressLoad");
strncpy(stub.loadName, loadName, NAME_MAX - 1);
stub.segType  = (word)(SEGTYPE_EXPRESS & 0x1F);
stub.kind     = SEGTYPE_EXPRESS;        /* full 16-bit KIND word */
stub.banksize = 0x10000L;

/* Body: segment count (word) + first-segment offset (dword) + END */
OmfWriteSegHeader(fp, &stub, 7L, 1, loadName);

OmfWriteWord (fp, 1);
OmfWriteDword(fp, dataSegOffset);
OmfWriteByte (fp, OP_END);
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
    /* Reserve space for the ExpressLoad stub; real data goes after.
     * After we write the data segments we'll seek back and fill the
     * stub's body in place. */
    long stubNameLen = (long)strlen("~ExpressLoad");
    long stubBodyOff = 48L + 10 + 1 + 10;   /* matches OmfWriteSegHeader */
    long stubSpan    = stubBodyOff + 7L;    /* body = word + dword + END */
    long stubStart   = ftell(fp);
    long dataStart;
    long savedPos;
    long i;

    (void)stubNameLen;                      /* reserved for future use */
    for (i = 0; i < stubSpan; i++) fputc(0, fp);

    dataStart = ftell(fp);
    segNum = 1;                             /* stub occupies #1 */
    for (seg = outSegs; seg; seg = seg->next)
        WriteSeg(fp, seg, ++segNum);

    /* Go back and patch in the real stub header/body. */
    savedPos = ftell(fp);
    fseek(fp, stubStart, SEEK_SET);
    WriteExpressSegment(fp, dataStart - stubStart, baseName);
    fseek(fp, savedPos, SEEK_SET);
    }
else {
    segNum = 1;
    for (seg = outSegs; seg; seg = seg->next)
        WriteSeg(fp, seg, segNum++);
    }

fclose(fp);
return (numErrors == 0);
}
