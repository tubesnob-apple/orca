/**************************************************************
*
*  omf.c -- OMF I/O primitives for clinker
*
*  Handles reading OMF v2 segment headers from input files and
*  writing OMF v2 segment headers + bodies to the output file.
*
*  OMF v2 header layout (all multi-byte values little-endian):
*    Off  Size  Field
*     0    4    BLKCNT  (disk blocks used)
*     4    4    RESSPC  (reserved space at end)
*     8    4    LENGTH  (segment length after relocation)
*    12    1    (unused)
*    13    1    LABLEN  (0 = variable-length pstrings)
*    14    1    NUMLEN  (must be 4)
*    15    1    VERSION (2 = OMF v2)
*    16    4    BANKSIZE
*    20    2    KIND    (segment type and attributes)
*    22    2    (unused)
*    24    4    ORG
*    28    4    ALIGN
*    32    1    NUMSEX  (0 = little-endian)
*    33    1    LANG
*    34    2    SEGNUM
*    36    4    ENTRY   (entry point displacement)
*    40    2    DISPNAME (offset to names from segment start)
*    42    2    DISPDATA (offset to body from segment start)
*    44+   str  LOADNAME (Pascal string)
*    ...   str  SEGNAME  (Pascal string)
*
*  DISPNAME = 44 (fixed).
*  DISPDATA = 44 + ceil_even(1+loadname_len) + ceil_even(1+segname_len)
*
**************************************************************/

#pragma keep "omf"
#pragma optimize 9

#include "clinker.h"

/* OMF I/O primitives in their own OMF load segment. Hot — called from
 * pass1, pass2, out — but every caller uses large-model JSL already. */
segment "OMF";

/* ----------------------------------------------------------
   Read helpers
   ---------------------------------------------------------- */

int OmfReadByte(FILE *fp)
{
int c = fgetc(fp);
if (c == EOF) {
    LinkError("unexpected EOF reading OMF", "");
    return 0;
    }
return c & 0xFF;
}

int OmfReadWord(FILE *fp, word *out)
{
int lo = fgetc(fp);
int hi = fgetc(fp);
if (lo == EOF || hi == EOF) {
    LinkError("unexpected EOF reading OMF word", "");
    return 0;
    }
*out = (word)((hi << 8) | lo);
return 1;
}

int OmfReadDword(FILE *fp, dword *out)
{
int b0 = fgetc(fp);
int b1 = fgetc(fp);
int b2 = fgetc(fp);
int b3 = fgetc(fp);
if (b0 == EOF || b1 == EOF || b2 == EOF || b3 == EOF) {
    LinkError("unexpected EOF reading OMF dword", "");
    return 0;
    }
*out = (dword)b0 | ((dword)b1 << 8) | ((dword)b2 << 16) | ((dword)b3 << 24);
return 1;
}

/*
 * OmfReadPString -- Read a Pascal string into out[0..maxLen-1].
 * Returns 1 on success.  out is always NUL-terminated.
 */
int OmfReadPString(FILE *fp, char *out, int maxLen)
{
int len, i;

len = fgetc(fp);
if (len == EOF) {
    LinkError("unexpected EOF reading OMF pstring", "");
    out[0] = 0;
    return 0;
    }
if (len >= maxLen) {
    for (i = 0; i < maxLen - 1; i++) {
        int c = fgetc(fp);
        if (c == EOF) break;
        out[i] = (char)c;
        }
    out[maxLen - 1] = 0;
    for (; i < len; i++) fgetc(fp);
    } else {
    for (i = 0; i < len; i++) {
        int c = fgetc(fp);
        if (c == EOF) { out[i] = 0; return 0; }
        out[i] = (char)c;
        }
    out[len] = 0;
    }
return 1;
}

/* ----------------------------------------------------------
   OmfReadHeader

   Read one OMF v2 / v2.1 segment header starting at startOff.
   Fills seg fields and sets seg->fileBodyOffset to the body
   start.  Returns 1 on success, 0 on EOF or validation failure.

   "Foreign file" rejection per GS/OS Reference Appendix F:
       NUMSEX == 0, NUMLEN == 4, VERSION == 2,
       BANKSIZE <= $10000, ALIGN in {0, $100, $10000}, LABLEN in {0, 10}.
   Any of these out-of-range is a hard reject — clinker only targets
   IIgs v2 OMF (see clinker/docs/clinker_build.md).
   ---------------------------------------------------------- */
int OmfReadHeader(FILE *fp, InSeg *seg, long startOff)
{
dword bytecnt, resspc, length;
word  kind, segnum, unused1;
dword banksize, org, align, entry;
word  dispname, dispbody;
int   labLen, numLen, version, numSex;
long  nameStart;

if (fseek(fp, startOff, SEEK_SET) != 0)
    return 0;

/* First field = BYTECNT (v2 format).  A zero/EOF here is a clean end
 * of the segment iteration, not an error. */
{
int b0 = fgetc(fp);
int b1, b2, b3;
if (b0 == EOF) return 0;
b1 = fgetc(fp);
b2 = fgetc(fp);
b3 = fgetc(fp);
if (b1 == EOF || b2 == EOF || b3 == EOF) return 0;
bytecnt = (dword)b0 | ((dword)b1 << 8) | ((dword)b2 << 16) | ((dword)b3 << 24);
}
if (bytecnt == 0) return 0;

OmfReadDword(fp, &resspc);
seg->resspc = (long)resspc;
OmfReadDword(fp, &length);        /* logical body length */

fgetc(fp);                        /* unused */
labLen  = fgetc(fp);              /* LABLEN  */
numLen  = fgetc(fp);              /* NUMLEN  */
version = fgetc(fp);              /* VERSION */

/* Foreign-file checks (part 1): VERSION / NUMLEN / LABLEN */
if (version != 2) {
    LinkError("unsupported OMF version (expected 2)", seg->segName);
    return 0;
    }
if (numLen != 4) {
    LinkError("invalid NUMLEN (expected 4)", seg->segName);
    return 0;
    }
if (labLen != 0 && labLen != 10) {
    LinkError("invalid LABLEN (expected 0 or 10)", seg->segName);
    return 0;
    }

OmfReadDword(fp, &banksize);
seg->banksize = (long)banksize;

/* Foreign-file check (part 2): BANKSIZE */
if (banksize > 0x10000UL) {
    LinkError("BANKSIZE exceeds $10000", seg->segName);
    return 0;
    }

OmfReadWord(fp, &kind);
seg->segType = (word)(kind & 0x1F);
seg->segkind = kind;

OmfReadWord(fp, &unused1);
OmfReadDword(fp, &org);
seg->org = (long)org;
OmfReadDword(fp, &align);
seg->align = (long)align;

/* Foreign-file check (part 3): ALIGN must be a value the loader
 * natively supports — 0, $100, or $10000.  Anything else will be
 * silently rounded up at load time, so we flag it at link time
 * to make misuse obvious. */
if (align != 0 && align != 0x100UL && align != 0x10000UL) {
    LinkError("invalid ALIGN (expected 0, $100, or $10000)",
              seg->segName);
    return 0;
    }

numSex = fgetc(fp);               /* NUMSEX */
fgetc(fp);                        /* LANG   */

/* Foreign-file check (part 4): NUMSEX must be 0 (little-endian) */
if (numSex != 0) {
    LinkError("NUMSEX != 0 (big-endian OMF not supported)",
              seg->segName);
    return 0;
    }

OmfReadWord(fp, &segnum);
OmfReadDword(fp, &entry);
OmfReadWord(fp, &dispname);
OmfReadWord(fp, &dispbody);

/* Names start at DISPNAME offset from segment start.
 * OMF v2 layout: 10-byte space-padded LOADNAME, then pstring SEGNAME. */
nameStart = startOff + (long)dispname;
fseek(fp, nameStart, SEEK_SET);

{
char tmp[11];
int  last;
fread(tmp, 1, 10, fp);
tmp[10] = 0;
last = 9;
while (last >= 0 && tmp[last] == ' ') last--;
tmp[last + 1] = 0;
strncpy(seg->loadName, tmp, NAME_MAX - 1);
seg->loadName[NAME_MAX - 1] = 0;
OmfReadPString(fp, seg->segName, NAME_MAX);
}

/* Uppercase SEGNAME only (ORCA convention — segment names are used
 * as symbol references, which are case-insensitive, so normalizing
 * up front simplifies hash lookups).  LOADNAME keeps its original
 * case because iix link preserves load-name case on output, and the
 * load name determines segment merging — collapsing "cg" and "CG"
 * (distinct load names in real ORCA/C input) into a single output
 * segment changes the file layout. */
{
char *p;
for (p = seg->segName;  *p; p++) *p = (char)toupper(*p);
}

/* Body starts at startOff + DISPDATA */
seg->fileBodyOffset = startOff + (long)dispbody;

/* Disk span for this segment (used by callers to find the next one). */
seg->bodyLen = (long)bytecnt;

return 1;
}

/* ----------------------------------------------------------
   Write helpers
   ---------------------------------------------------------- */

void OmfWriteByte(FILE *fp, int v)
{
fputc(v & 0xFF, fp);
}

void OmfWriteWord(FILE *fp, int v)
{
fputc(v & 0xFF, fp);
fputc((v >> 8) & 0xFF, fp);
}

void OmfWriteDword(FILE *fp, long v)
{
fputc((int)(v)       & 0xFF, fp);
fputc((int)(v >> 8)  & 0xFF, fp);
fputc((int)(v >> 16) & 0xFF, fp);
fputc((int)(v >> 24) & 0xFF, fp);
}

void OmfWritePString(FILE *fp, const char *s)
{
int len = (int)strlen(s);
fputc(len, fp);
fwrite(s, 1, (size_t)len, fp);
}

/* ----------------------------------------------------------
   OmfWriteSegHeader

   Write a complete OMF v2 segment header to fp.
   Returns the file offset where the body should start.
   ---------------------------------------------------------- */
long OmfWriteSegHeader(FILE *fp, OutSeg *seg, long bodyLen, int segNum,
                       const char *loadName)
{
long bodyDisp;
long blkcnt, totalLen;
char loadBuf[10];
char segBuf[10];
int  i, slen;

(void)loadName;    /* single-load-file output: LOADNAME is always blank */

/* OMF v2.1 segment header layout:
 *   DISPNAME = $30 (48): fixed header + 4-byte tempORG, then names
 *   LOADNAME: 10 bytes space-padded (blank for consolidated output)
 *   SEGNAME:  pstring, length 10, space-padded seg->loadName
 *       (stock iix link uses the merge key here, not the first input's
 *       segName — so blank-loadName groups get blank SEGNAME, and
 *       named groups like "KERN2" get SEGNAME = "KERN2")
 *   Body follows immediately; DISPDATA = 48 + 10 + 1 + 10 = 69. */

for (i = 0; i < 10; i++) loadBuf[i] = ' ';

slen = (int)strlen(seg->loadName);
if (slen > 10) slen = 10;
for (i = 0; i < slen; i++) segBuf[i] = seg->loadName[i];
for (; i < 10; i++)        segBuf[i] = ' ';

bodyDisp = 48L + 10 + 1 + 10;    /* = 69 */
totalLen = bodyDisp + bodyLen;
blkcnt   = totalLen;

/* BYTECNT */ OmfWriteDword(fp, blkcnt);
/* RESSPC */  OmfWriteDword(fp, 0L);
/* LENGTH */  OmfWriteDword(fp, (long)seg->dataLen);
/* unused */  OmfWriteByte (fp, 0);
/* LABLEN */  OmfWriteByte (fp, 0);
/* NUMLEN */  OmfWriteByte (fp, 4);
/* VERSION */ OmfWriteByte (fp, 2);
/* BANKSIZE */ OmfWriteDword(fp, seg->banksize ? seg->banksize : 0x10000L);
/* KIND */    OmfWriteWord (fp, (int)(seg->kind ? seg->kind : seg->segType));
/* unused */  OmfWriteWord (fp, 0);
/* ORG */     OmfWriteDword(fp, seg->org);
/* ALIGN */   OmfWriteDword(fp, seg->align);
/* NUMSEX */  OmfWriteByte (fp, 0);
/* LANG */    OmfWriteByte (fp, 0);
/* SEGNUM */  OmfWriteWord (fp, segNum);
/* ENTRY */   OmfWriteDword(fp, 0L);
/* DISPNAME */ OmfWriteWord(fp, 48);          /* $30 (v2.1, after tempORG) */
/* DISPDATA */ OmfWriteWord(fp, (int)bodyDisp);
/* tempORG */ OmfWriteDword(fp, 0L);          /* v2.1 extension, unused */

/* LOADNAME: 10 blank bytes */
fwrite(loadBuf, 1, 10, fp);
/* SEGNAME: pstring length 10, content = space-padded seg->segName */
OmfWriteByte(fp, 10);
fwrite(segBuf, 1, 10, fp);

return ftell(fp);
}

/* ----------------------------------------------------------
   Relocation-record sizing + emission

   Each RelocRec maps to one of four OMF output records:
     type=0, 16-bit fit     -> cRELOC    ($F5) —  7 bytes
     type=0, else           -> RELOC     ($E2) — 11 bytes
     type=1, cINTERSEG fit  -> cINTERSEG ($F6) —  8 bytes
     type=1, else           -> INTERSEG  ($E3) — 15 bytes
   Compact forms per Appendix F Table F-3 require:
     cRELOC     — pc fits 16 bits, value fits 16 bits
     cINTERSEG  — pc fits 16 bits, value fits 16 bits,
                  segNum fits 8 bits, fileNum == 1
   ---------------------------------------------------------- */

static BOOLEAN FitsCReloc(const RelocRec *r)
{
return (r->pc    >= 0 && r->pc    < 0x10000L &&
        r->value >= 0 && r->value < 0x10000L);
}

static BOOLEAN FitsCInterseg(const RelocRec *r)
{
return (r->pc      >= 0 && r->pc      < 0x10000L &&
        r->value   >= 0 && r->value   < 0x10000L &&
        r->segNum  >= 0 && r->segNum  < 0x100    &&
        r->fileNum == 1);
}

long OmfRelocSize(const RelocRec *r)
{
if (r->type == 0)
    return FitsCReloc(r)    ?  7L : 11L;   /* cRELOC vs RELOC */
else
    return FitsCInterseg(r) ?  8L : 15L;   /* cINTERSEG vs INTERSEG */
}

void OmfWriteReloc(FILE *fp, const RelocRec *r)
{
/* GS/OS loader expects POST-ExpressLoad segment numbers in every
 * relocation record's segNum field. Clinker's internal r->segNum is
 * pre-remap (1..N over data segments); convert to post-remap by
 * adding 1 for the ExpressLoad segment prepended at output time. */
int outSeg = r->segNum + (opt_express ? 1 : 0);

if (r->type == 0) {
    if (FitsCReloc(r)) {
        /* cRELOC: opcode(1) pLen(1) shift(1) pc16(2) value16(2) */
        OmfWriteByte(fp, OP_CRELOC);
        OmfWriteByte(fp, r->patchLen);
        OmfWriteByte(fp, r->shift);
        OmfWriteWord(fp, (int)r->pc);
        OmfWriteWord(fp, (int)r->value);
        }
    else {
        /* RELOC: opcode(1) pLen(1) shift(1) pc(4) value(4) */
        OmfWriteByte (fp, OP_RELOC);
        OmfWriteByte (fp, r->patchLen);
        OmfWriteByte (fp, r->shift);
        OmfWriteDword(fp, r->pc);
        OmfWriteDword(fp, r->value);
        }
    }
else {
    if (FitsCInterseg(r)) {
        /* cINTERSEG: op(1) pLen(1) shift(1) pc16(2) segNum(1) value16(2) */
        OmfWriteByte(fp, OP_CINTERSEG);
        OmfWriteByte(fp, r->patchLen);
        OmfWriteByte(fp, r->shift);
        OmfWriteWord(fp, (int)r->pc);
        OmfWriteByte(fp, outSeg);
        OmfWriteWord(fp, (int)r->value);
        }
    else {
        /* INTERSEG: op(1) pLen(1) shift(1) pc(4) fileNum(2) segNum(2) value(4) */
        OmfWriteByte (fp, OP_INTERSEG);
        OmfWriteByte (fp, r->patchLen);
        OmfWriteByte (fp, r->shift);
        OmfWriteDword(fp, r->pc);
        OmfWriteWord (fp, r->fileNum);
        OmfWriteWord (fp, outSeg);
        OmfWriteDword(fp, r->value);
        }
    }
}

/* ----------------------------------------------------------
   SUPER packing (all 38 subtypes)

   Each RelocRec is classified into one of 38 SUPER subtypes or marked
   as unpackable (-1).  Records that share a subtype are collected,
   their patch offsets are sorted and walked through 256-byte pages,
   and the packed stream is emitted as a single SUPER record.

   The classification table (from GS/OS Reference Appendix F p.463):
       0         RELOC2          type=0, patchLen=2, shift=0
       1         RELOC3          type=0, patchLen=3, shift=0
       2         INTERSEG1       type=1, patchLen=3, shift=0,  file=1
       3..13     INTERSEG2..12   type=1, patchLen=3, shift=0,  file=2..12
       14..25    INTERSEG13..24  type=1, patchLen=2, shift=0,    file=1, seg=1..12
       26..37    INTERSEG25..36  type=1, patchLen=2, shift=0xF0, file=1, seg=1..12

   For RELOC2/3 and INTERSEG13..36 the full patch value lives in the
   LCONST at the patch offset.  For INTERSEG1..12 (3-byte patches), the
   16-bit offset lives in bytes 0-1 and the target segment number in
   byte 2.  OmfPrepareSuper writes those bytes before the LCONST is
   emitted so the loader can read them back at run time.

   Unpackable records (non-matching shape, large fileNum for INTERSEG,
   anything with shift values other than 0 or 0xF0) fall through to
   individual cRELOC/RELOC/cINTERSEG/INTERSEG emission.

   Packing per Appendix F pp. 463-464: a stream of subrecords, each
   a Count byte with (Count & $80 == 0 -> Count+1 in-page offsets
   follow) or (Count & $80 -> skip (Count & $7F) pages).
   ---------------------------------------------------------- */

#define SUPER_SUBTYPE_COUNT 38

/* ClassifyRelocForSuper — best SUPER subtype for r, or -1 if unpackable.
 *
 * GS/OS loader decodes SUPER_INTERSEG13..36 subtype via
 * `segment = type - 12` and treats `segment` as the POST-ExpressLoad
 * SEGNUM in the output file. Clinker's internal r->segNum is pre-remap
 * (1..N over data segments) so we bump by 1 when opt_express prepends
 * an ExpressLoad. Without this bump every subtype comes out one less
 * than stock's and the loader patches with the wrong segment base. */
static int ClassifyRelocForSuper(const RelocRec *r)
{
int outSeg;

if (r->pc < 0) return -1;

if (r->type == 0) {
    if (r->shift == 0 && r->patchLen == 2) return 0;    /* RELOC2 */
    if (r->shift == 0 && r->patchLen == 3) return 1;    /* RELOC3 */
    return -1;
    }

/* r->type == 1 (INTERSEG) */
outSeg = r->segNum + (opt_express ? 1 : 0);

if (r->patchLen == 3 && r->shift == 0 &&
    r->fileNum >= 1 && r->fileNum <= 12) {
    return r->fileNum + 1;                              /* INTERSEG1..12  */
    }

if (r->patchLen == 2 && r->fileNum == 1 &&
    outSeg >= 1 && outSeg <= 12) {
    if (r->shift == 0)    return outSeg + 13;           /* INTERSEG13..24 */
    if (r->shift == 0xF0) return outSeg + 25;           /* INTERSEG25..36 */
    }

return -1;
}

/* Collect, in ascending-pc order, every RelocRec in `seg` whose
 * classification matches `subtype`.  Returns offset array (caller
 * frees) and count via *outCount.  Returns NULL/0 when none match. */
static long *CollectSuperOffsets(const OutSeg *seg, int subtype,
                                 int *outCount)
{
long     *arr;
int       count = 0;
int       i;
RelocRec *r;

for (r = seg->relocHead; r; r = r->next)
    if (ClassifyRelocForSuper(r) == subtype) count++;

*outCount = count;
if (count == 0) return NULL;

arr = (long *)malloc((size_t)count * sizeof(long));
if (!arr) return NULL;

i = 0;
for (r = seg->relocHead; r; r = r->next)
    if (ClassifyRelocForSuper(r) == subtype) arr[i++] = r->pc;

/* Insertion sort (reloc counts per segment are small). */
for (i = 1; i < count; i++) {
    long v = arr[i];
    int  j = i;
    while (j > 0 && arr[j - 1] > v) { arr[j] = arr[j - 1]; j--; }
    arr[j] = v;
    }

return arr;
}

/* Bytes required by the packed subrecord stream (excluding the leading
 * opcode, 4-byte length, and 1-byte type).  Mirrors the walk in
 * EmitSuperStream below. */
static long PackedSuperBytes(const long *offs, int n)
{
long bytes = 0;
int  i = 0;
int  page = 0;

while (i < n) {
    int targetPage = (int)(offs[i] >> 8);
    int start, pageCount;

    /* Each skip marker jumps up to 127 pages */
    while (page < targetPage) {
        int skip = targetPage - page;
        if (skip > 0x7F) skip = 0x7F;
        bytes += 1;                  /* skip byte */
        page  += skip;
        }

    start = i;
    while (i < n && (long)(offs[i] >> 8) == (long)page) i++;
    pageCount = i - start;
    bytes += 1 + pageCount;           /* count + N offsets */
    page++;
    }

return bytes;
}

static void EmitSuperStream(FILE *fp, int subtype,
                            const long *offs, int n)
{
long streamBytes = PackedSuperBytes(offs, n);
int  i = 0;
int  page = 0;

OmfWriteByte (fp, OP_SUPER);
OmfWriteDword(fp, 1L + streamBytes);   /* length = type byte + stream */
OmfWriteByte (fp, subtype);

while (i < n) {
    int targetPage = (int)(offs[i] >> 8);
    int start, pageCount, k;

    while (page < targetPage) {
        int skip = targetPage - page;
        if (skip > 0x7F) skip = 0x7F;
        OmfWriteByte(fp, 0x80 | skip);
        page += skip;
        }

    start = i;
    while (i < n && (long)(offs[i] >> 8) == (long)page) i++;
    pageCount = i - start;
    OmfWriteByte(fp, pageCount - 1);   /* stored as count-1 */
    for (k = start; k < i; k++)
        OmfWriteByte(fp, (int)(offs[k] & 0xFF));
    page++;
    }
}

/* Total bytes the SUPER emission will write for this segment (one
 * SUPER record per populated subtype + an individual record for each
 * unpackable reloc). */
long OmfSuperBytes(const OutSeg *seg)
{
long       bytes = 0;
int        subtype;
int        n;
long      *arr;
RelocRec  *r;

for (subtype = 0; subtype < SUPER_SUBTYPE_COUNT; subtype++) {
    arr = CollectSuperOffsets(seg, subtype, &n);
    if (n > 0) {
        bytes += 1L + 4L + 1L + PackedSuperBytes(arr, n);
        free(arr);
        }
    }

for (r = seg->relocHead; r; r = r->next) {
    if (ClassifyRelocForSuper(r) >= 0) continue;   /* packed */
    bytes += OmfRelocSize(r);
    }

return bytes;
}

/* Populate seg->data at each SUPER-packable patch location with the
 * reference value (and, for INTERSEG1..12, the target segment number)
 * that the SUPER record will later point at.  Call before writing the
 * LCONST so the loader can read back the values at run time. */
void OmfPrepareSuper(OutSeg *seg)
{
RelocRec *r;
int       i, subtype;

for (r = seg->relocHead; r; r = r->next) {
    subtype = ClassifyRelocForSuper(r);
    if (subtype < 0) continue;
    if (r->pc < 0 || r->pc + r->patchLen > seg->dataLen) continue;

    if (subtype >= 2 && subtype <= 13) {
        /* INTERSEG1..12 (3-byte): 16-bit offset in bytes 0-1,
         * POST-ExpressLoad target segment number in byte 2. */
        int outSeg = r->segNum + (opt_express ? 1 : 0);
        seg->data[r->pc + 0] = (byte)(r->value);
        seg->data[r->pc + 1] = (byte)(r->value >> 8);
        seg->data[r->pc + 2] = (byte)outSeg;
        }
    else {
        /* RELOC2 (2b), RELOC3 (3b), INTERSEG13..36 (2b): pure value. */
        for (i = 0; i < r->patchLen; i++)
            seg->data[r->pc + i] = (byte)(r->value >> (i * 8));
        }
    }
}

void OmfWriteSuper(FILE *fp, OutSeg *seg)
{
int        subtype;
int        n;
long      *arr;
RelocRec  *r;

for (subtype = 0; subtype < SUPER_SUBTYPE_COUNT; subtype++) {
    arr = CollectSuperOffsets(seg, subtype, &n);
    if (n > 0) {
        EmitSuperStream(fp, subtype, arr, n);
        free(arr);
        }
    }

/* Anything ClassifyRelocForSuper couldn't pack — non-matching shape,
 * large fileNum, unusual shift — gets emitted individually as
 * cRELOC / RELOC / cINTERSEG / INTERSEG. */
for (r = seg->relocHead; r; r = r->next)
    if (ClassifyRelocForSuper(r) < 0)
        OmfWriteReloc(fp, r);
}
