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

   Read the OMF v2 segment header starting at startOff in fp.
   Fills seg fields and sets seg->fileBodyOffset to the body
   start.  Returns 1 on success, 0 on EOF/error.
   ---------------------------------------------------------- */
int OmfReadHeader(FILE *fp, InSeg *seg, long startOff)
{
dword blkcnt, resspc, length;
word  kind, segnum, unused1;
dword banksize, org, align, entry;
word  dispname, dispbody;
long  nameStart;

if (fseek(fp, startOff, SEEK_SET) != 0)
    return 0;

/* First field = total byte count of this segment on disk.
 * A zero or EOF here means no more segments -- clean end of iteration. */
{
int b0 = fgetc(fp);
if (b0 == EOF) return 0;
{
int b1 = fgetc(fp);
int b2 = fgetc(fp);
int b3 = fgetc(fp);
if (b1 == EOF || b2 == EOF || b3 == EOF) return 0;
blkcnt = (dword)b0 | ((dword)b1 << 8) | ((dword)b2 << 16) | ((dword)b3 << 24);
}
}
if (blkcnt == 0) return 0;

OmfReadDword(fp, &resspc);
OmfReadDword(fp, &length);  /* logical body length */

fgetc(fp);  /* unused */
fgetc(fp);  /* LABLEN */
fgetc(fp);  /* NUMLEN */
fgetc(fp);  /* VERSION */

OmfReadDword(fp, &banksize);
seg->banksize = (long)banksize;

OmfReadWord(fp, &kind);
seg->segType = (word)(kind & 0x1F);
seg->segkind = kind;

OmfReadWord(fp, &unused1);
OmfReadDword(fp, &org);
seg->org = (long)org;
OmfReadDword(fp, &align);
seg->align = (long)align;

fgetc(fp);   /* NUMSEX */
fgetc(fp);   /* LANG   */

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

/* Disk span = first header field, which is the total byte count of the
 * segment on disk (NOT a block count despite being called BLKCNT in
 * many docs -- DumpOBJ labels it "Byte count" which is correct). */
seg->bodyLen = (long)blkcnt;

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
/* KIND */    OmfWriteWord (fp, (int)seg->segType);
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
   OmfWriteReloc

   Emit a RELOC ($E2) or INTERSEG ($E3) dictionary record.
   ---------------------------------------------------------- */
void OmfWriteReloc(FILE *fp, RelocRec *r)
{
if (r->type == 0) {
    /* RELOC: opcode(1) patchLen(1) shift(1) pc(4) value(4) */
    OmfWriteByte (fp, OP_RELOC);
    OmfWriteByte (fp, r->patchLen);
    OmfWriteByte (fp, r->shift);
    OmfWriteDword(fp, r->pc);
    OmfWriteDword(fp, r->value);
    } else {
    /* INTERSEG: op(1) pLen(1) shift(1) pc(4) fileNum(2) segNum(2) addend(4) */
    OmfWriteByte (fp, OP_INTERSEG);
    OmfWriteByte (fp, r->patchLen);
    OmfWriteByte (fp, r->shift);
    OmfWriteDword(fp, r->pc);
    OmfWriteWord (fp, r->fileNum);
    OmfWriteWord (fp, r->segNum);
    OmfWriteDword(fp, r->value);  /* addend */
    }
}

/* ----------------------------------------------------------
   OmfWriteSuper

   Emit all relocation records for one output segment.
   Writes plain RELOC/INTERSEG records (no SUPER packing for now).
   ---------------------------------------------------------- */
void OmfWriteSuper(FILE *fp, OutSeg *seg)
{
RelocRec *r;
for (r = seg->relocHead; r; r = r->next)
    OmfWriteReloc(fp, r);
}
