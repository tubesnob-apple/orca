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

/* Uppercase (ORCA convention) */
{
char *p;
for (p = seg->loadName; *p; p++) *p = (char)toupper(*p);
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
char ldBuf[10];
int  i, llen;

/* OMF v2.1 segment header layout:
 *   DISPNAME = $30 (48): fixed header + 4-byte tempORG, then names
 *   LOADNAME: 10 bytes, space-padded, no length prefix
 *   SEGNAME:  pstring, length 10, content = space-padded LOADNAME
 *   Body follows immediately; DISPDATA = 48 + 10 + 1 + 10 = 69. */

/* Build 10-byte space-padded load name. loadName may be "" (all spaces). */
llen = (int)strlen(loadName);
if (llen > 10) llen = 10;
for (i = 0; i < llen; i++) ldBuf[i] = loadName[i];
for (; i < 10; i++)        ldBuf[i] = ' ';

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

/* LOADNAME: 10 bytes space-padded */
fwrite(ldBuf, 1, 10, fp);
/* SEGNAME: pstring with length 10, content = ldBuf (matches LOADNAME) */
OmfWriteByte(fp, 10);
fwrite(ldBuf, 1, 10, fp);

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
        OmfWriteByte(fp, (int)r->segNum);
        OmfWriteWord(fp, (int)r->value);
        }
    else {
        /* INTERSEG: op(1) pLen(1) shift(1) pc(4) fileNum(2) segNum(2) value(4) */
        OmfWriteByte (fp, OP_INTERSEG);
        OmfWriteByte (fp, r->patchLen);
        OmfWriteByte (fp, r->shift);
        OmfWriteDword(fp, r->pc);
        OmfWriteWord (fp, r->fileNum);
        OmfWriteWord (fp, r->segNum);
        OmfWriteDword(fp, r->value);
        }
    }
}

/* ----------------------------------------------------------
   SUPER RELOC2 / RELOC3 packing

   Entries that qualify (type=0, shift=0, patchLen in {2,3}) are
   grouped into one SUPER record per patchLen.  Everything else
   falls through to individual cRELOC/RELOC/cINTERSEG/INTERSEG.

   Packing follows Appendix F pp. 463-464: a stream of subrecords,
   each a Count byte with (count & $80 == 0 -> Count+1 in-page
   offsets follow) or (count & $80 -> skip (count & $7F) pages).

   Because SUPER reads the reference value from the LCONST at each
   patch location, OmfSuperPrepare must be called before the LCONST
   is written so the right bytes are present.

   INTERSEG SUPER subtypes (2..37) are not yet packed here; those
   still go out as individual cINTERSEG/INTERSEG records.
   ---------------------------------------------------------- */

static BOOLEAN SuperReloc23Type(const RelocRec *r, byte *outType)
{
if (r->type != 0 || r->shift != 0 || r->pc < 0) return FALSE;
if (r->patchLen == 2) { if (outType) *outType = 0x00; return TRUE; }
if (r->patchLen == 3) { if (outType) *outType = 0x01; return TRUE; }
return FALSE;
}

/* Collect, in ascending-pc order, every RelocRec in `seg` that packs
 * into SUPER subtype `superType`.  Returns offset array (caller frees)
 * and count via *outCount.  Caller receives NULL/0 when none match. */
static long *CollectSuperOffsets(const OutSeg *seg, byte superType,
                                 int *outCount)
{
long     *arr;
int       count = 0;
int       i;
RelocRec *r;

for (r = seg->relocHead; r; r = r->next) {
    byte t;
    if (SuperReloc23Type(r, &t) && t == superType) count++;
    }

*outCount = count;
if (count == 0) return NULL;

arr = (long *)malloc((size_t)count * sizeof(long));
if (!arr) return NULL;  /* caller treats as empty */

i = 0;
for (r = seg->relocHead; r; r = r->next) {
    byte t;
    if (SuperReloc23Type(r, &t) && t == superType) arr[i++] = r->pc;
    }

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

static void EmitSuperStream(FILE *fp, byte superType,
                            const long *offs, int n)
{
long streamBytes = PackedSuperBytes(offs, n);
int  i = 0;
int  page = 0;

OmfWriteByte (fp, OP_SUPER);
OmfWriteDword(fp, 1L + streamBytes);   /* length = type byte + stream */
OmfWriteByte (fp, (int)superType);

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

/* Total bytes the SUPER emission will write for this segment (SUPER
 * records for RELOC2/RELOC3 + individual records for everything else). */
long OmfSuperBytes(const OutSeg *seg)
{
long       bytes = 0;
int        n;
long      *arr;
RelocRec  *r;
byte       t;

arr = CollectSuperOffsets(seg, 0x00, &n);
if (n > 0) { bytes += 1 + 4 + 1 + PackedSuperBytes(arr, n); free(arr); }

arr = CollectSuperOffsets(seg, 0x01, &n);
if (n > 0) { bytes += 1 + 4 + 1 + PackedSuperBytes(arr, n); free(arr); }

for (r = seg->relocHead; r; r = r->next) {
    if (SuperReloc23Type(r, &t)) continue;  /* packed */
    bytes += OmfRelocSize(r);
    }

return bytes;
}

/* Populate seg->data at each SUPER-packable patch location with the
 * reference value from the matching RelocRec.  Call before writing the
 * LCONST so that SUPER's "value lives in LCONST" contract holds at
 * load time. */
void OmfPrepareSuper(OutSeg *seg)
{
RelocRec *r;
int       i;
byte      t;

for (r = seg->relocHead; r; r = r->next) {
    if (!SuperReloc23Type(r, &t)) continue;
    if (r->pc + r->patchLen > seg->dataLen) continue;
    for (i = 0; i < r->patchLen; i++)
        seg->data[r->pc + i] = (byte)(r->value >> (i * 8));
    }
}

void OmfWriteSuper(FILE *fp, OutSeg *seg)
{
long      *arr;
int        n;
RelocRec  *r;
byte       t;

arr = CollectSuperOffsets(seg, 0x00, &n);
if (n > 0) { EmitSuperStream(fp, 0x00, arr, n); free(arr); }

arr = CollectSuperOffsets(seg, 0x01, &n);
if (n > 0) { EmitSuperStream(fp, 0x01, arr, n); free(arr); }

/* Everything that didn't pack — INTERSEG, shift != 0, out-of-range
 * pc/value for RELOC2/3 — gets emitted individually. */
for (r = seg->relocHead; r; r = r->next) {
    if (SuperReloc23Type(r, &t)) continue;
    OmfWriteReloc(fp, r);
    }
}
