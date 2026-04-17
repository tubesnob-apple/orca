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

/* BLKCNT -- if 0 and at EOF, no more segments */
if (!OmfReadDword(fp, &blkcnt)) return 0;
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

/* Names at offset 44 from segment start */
nameStart = startOff + 44;
fseek(fp, nameStart, SEEK_SET);
OmfReadPString(fp, seg->loadName, NAME_MAX);
OmfReadPString(fp, seg->segName,  NAME_MAX);

/* Uppercase (ORCA convention) */
{
char *p;
for (p = seg->loadName; *p; p++) *p = (char)toupper(*p);
for (p = seg->segName;  *p; p++) *p = (char)toupper(*p);
}

/* Body starts at startOff + DISPDATA */
seg->fileBodyOffset = startOff + (long)dispbody;

/* Disk span = BLKCNT * 512 (used by caller to find next segment) */
seg->bodyLen = (long)blkcnt * 512L;

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
int  loadLen, segLen, padLoad, padSeg;
long blkcnt, totalLen;

loadLen = (int)strlen(loadName);
segLen  = (int)strlen(seg->segName);

/* Each pstring is (1 + N) bytes; pad to even */
padLoad = (loadLen + 1) & 1;
padSeg  = (segLen  + 1) & 1;

bodyDisp = 44L + 1 + loadLen + padLoad + 1 + segLen + padSeg;
totalLen = bodyDisp + bodyLen;
blkcnt   = (totalLen + 511L) / 512L;

/* BLKCNT */  OmfWriteDword(fp, blkcnt);
/* RESSPC */  OmfWriteDword(fp, 0L);
/* LENGTH */  OmfWriteDword(fp, bodyLen);
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
/* DISPNAME */ OmfWriteWord(fp, 44);
/* DISPDATA */ OmfWriteWord(fp, (int)bodyDisp);

/* LOADNAME */ OmfWritePString(fp, loadName);
               if (padLoad) OmfWriteByte(fp, 0);
/* SEGNAME */  OmfWritePString(fp, seg->segName);
               if (padSeg)  OmfWriteByte(fp, 0);

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
    /* INTERSEG: op(1) pLen(1) shift(1) unused(5) pc(4) fileNum(2) segNum(2) */
    OmfWriteByte (fp, OP_INTERSEG);
    OmfWriteByte (fp, r->patchLen);
    OmfWriteByte (fp, r->shift);
    OmfWriteDword(fp, 0L);  /* unused 4 bytes */
    OmfWriteByte (fp, 0);   /* unused 1 byte  */
    OmfWriteDword(fp, r->pc);
    OmfWriteWord (fp, r->fileNum);
    OmfWriteWord (fp, r->segNum);
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
