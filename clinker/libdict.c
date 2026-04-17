/**************************************************************
*
*  libdict.c -- library dictionary parser and lookup
*
*  A library file (ProDOS type $B2) contains object segments plus
*  one special KIND=$08 segment — the library dictionary — that
*  maps global symbol names to the file offsets of the segments
*  defining them.  Parsing it once lets LibrarySearch jump straight
*  to a symbol's defining segment rather than re-scanning the whole
*  library on every pass.
*
*  On-disk format (GS/OS Reference Appendix F, p. 470):
*
*    body of the dictionary segment = three consecutive LCONST records
*      1. filenames:    sequence of (word file#, pstring name),
*                       terminated by file# == 0
*      2. symbol table: fixed 12-byte entries
*                           dword name_displacement   (into record 3)
*                           word  file_number
*                           word  private_flag
*                           dword segment_displacement (file offset)
*      3. symbol names: sequence of pstring names, no terminator
*                       (walk to end of LCONST).
*
*  We ignore the filenames record (we only need name → segOffset
*  mappings).  Names are uppercased at parse time so lookups are
*  direct strcmp.
*
**************************************************************/

#pragma keep "libdict"
#pragma optimize 9

#include "clinker.h"

/* Read N-byte little-endian unsigned integer from buf+off. */
static dword ReadLE(const byte *buf, long off, int n)
{
dword v = 0;
int   i;
for (i = 0; i < n; i++)
    v |= ((dword)buf[off + i]) << (i * 8);
return v;
}

/* Consume one LCONST record from fp; allocate and return its body
 * bytes (caller frees).  *lenOut receives the body length.  Returns
 * NULL on any malformed record. */
static byte *ReadLConst(FILE *fp, dword *lenOut)
{
int    op;
dword  len;
byte  *buf;

op = fgetc(fp);
if (op != OP_LCONST) return NULL;
if (!OmfReadDword(fp, &len)) return NULL;
if (len == 0) {
    *lenOut = 0;
    return (byte *)calloc(1, 1);    /* non-NULL sentinel */
    }

buf = (byte *)malloc((size_t)len);
if (!buf) return NULL;
if (fread(buf, 1, (size_t)len, fp) != (size_t)len) {
    free(buf);
    return NULL;
    }
*lenOut = len;
return buf;
}

/* Comparator for qsort (by uppercase name). */
static int CmpSymEntry(const void *a, const void *b)
{
return strcmp(((const LibSymEntry *)a)->name,
              ((const LibSymEntry *)b)->name);
}

/* Parse the dictionary body at dictSeg->fileBodyOffset into lf->syms.
 * Returns 1 on success, 0 on malformed input. */
static int ParseDict(LibFile *lf, const InSeg *dictSeg)
{
byte  *symTable = NULL;
byte  *symNames = NULL;
byte  *filenames = NULL;
dword  fnLen = 0, stLen = 0, snLen = 0;
int    ok = 0;
int    entryCount;
int    i;

if (fseek(lf->fp, dictSeg->fileBodyOffset, SEEK_SET) != 0) return 0;

filenames = ReadLConst(lf->fp, &fnLen);
symTable  = ReadLConst(lf->fp, &stLen);
symNames  = ReadLConst(lf->fp, &snLen);
if (!filenames || !symTable || !symNames) goto done;

/* Each symbol-table entry is exactly 12 bytes. */
if (stLen % 12) goto done;
entryCount = (int)(stLen / 12);
if (entryCount == 0) { ok = 1; goto done; }

lf->syms = (LibSymEntry *)malloc((size_t)entryCount * sizeof(LibSymEntry));
if (!lf->syms) goto done;
lf->numSyms = 0;

for (i = 0; i < entryCount; i++) {
    long   off = (long)i * 12;
    dword  nameDisp = ReadLE(symTable, off + 0, 4);
    word   privFlag = (word)ReadLE(symTable, off + 6, 2);
    dword  segDisp  = ReadLE(symTable, off + 8, 4);
    int    nLen;
    int    j, n;
    LibSymEntry *e;

    /* Name displacement into the symbol-names LCONST; the name is
     * itself a pstring (1-byte length + chars). */
    if ((long)nameDisp >= (long)snLen) continue;
    nLen = (int)symNames[nameDisp];
    if ((long)nameDisp + 1 + nLen > (long)snLen) continue;

    e = &lf->syms[lf->numSyms];
    n = nLen;
    if (n >= NAME_MAX) n = NAME_MAX - 1;
    for (j = 0; j < n; j++)
        e->name[j] = (char)toupper(symNames[nameDisp + 1 + j]);
    e->name[n]    = 0;
    e->segOffset  = (long)segDisp;
    e->isPrivate  = (privFlag != 0);
    lf->numSyms++;
    }

/* Sort for binary-search lookup. */
qsort(lf->syms, (size_t)lf->numSyms, sizeof(LibSymEntry), CmpSymEntry);
ok = 1;

done:
free(filenames);
free(symTable);
free(symNames);
return ok;
}

/* LibDictInit — lazy-load the dictionary for one library file.
 * Scans the file for a KIND=$08 segment; if found, parses it.
 * Marks dictLoaded in both the hit and miss cases so we never
 * re-scan a given library. */
void LibDictInit(LibFile *lf)
{
long segOff = 0;

if (!lf || lf->dictLoaded) return;
lf->dictLoaded = TRUE;

for (;;) {
    InSeg seg;
    memset(&seg, 0, sizeof(seg));
    if (!OmfReadHeader(lf->fp, &seg, segOff)) break;
    if (seg.bodyLen == 0) break;

    if ((seg.segkind & 0x1F) == 0x08) {          /* Library Dictionary */
        if (ParseDict(lf, &seg))
            lf->dictValid = TRUE;
        break;
        }

    segOff += seg.bodyLen;
    }

/* Reset file position so callers aren't surprised. */
fseek(lf->fp, 0L, SEEK_SET);

if (opt_progress)
    printf("libdict: %s — %s, %d symbols\n",
           lf->path,
           lf->dictValid ? "loaded" : "no dictionary",
           lf->numSyms);
}

/* LibDictFind — binary search for `name` (already uppercase) in lf's
 * parsed dictionary.  Returns the file offset of the defining
 * segment's header, or -1 if the library has no dictionary or the
 * symbol isn't present. */
long LibDictFind(LibFile *lf, const char *name)
{
int lo, hi, mid, cmp;

if (!lf) return -1;
if (!lf->dictLoaded) LibDictInit(lf);
if (!lf->dictValid)  return -1;

lo = 0;
hi = lf->numSyms - 1;
while (lo <= hi) {
    mid = (lo + hi) >> 1;
    cmp = strcmp(lf->syms[mid].name, name);
    if (cmp == 0) {
        /* Per Appendix F: a private_flag=1 entry's symbol "is valid
         * only in the object file in which it occurred" — i.e. it
         * can satisfy references from segments MakeLib pulled from
         * the same original object, but not an arbitrary external
         * reference.  Treat those as no-match for external lookups. */
        if (lf->syms[mid].isPrivate) return -1;
        return lf->syms[mid].segOffset;
        }
    if (cmp < 0)  lo = mid + 1;
    else          hi = mid - 1;
    }
return -1;
}
