/**************************************************************
*
*  linfo.c -- iix-shell LangInfo bridge
*
*  When clinker is invoked via `iix link` (typically through a
*  symlink at /Library/GoldenGate/Languages/Linker -> clinker),
*  arguments don't arrive via argv. iix packs them into a
*  LangInfo structure that the shell returns when we call
*  GetLInfoGS. This file implements that path; the argv path
*  stays in clinker.c.
*
**************************************************************/

#pragma optimize 9
#pragma segment "LINFO"

#include "clinker.h"
#include <shell.h>
#include <gsos.h>
#include <orca.h>

/* Fields consumed from LangInfo:
 *   sFile    - space-joined input filenames (no extensions;
 *              .ROOT / .a / .B sibling search applies)
 *   dFile    - output filename (maps to keep=)
 *   pFlags   - bitmap of + flags (L, S, W, M, B)
 *   mFlags   - bitmap of - flags (X, C, P)
 *
 * Flag bit masks come verbatim from linker.asm:81-88 so any
 * Byte Works bitmap update stays portable. */

/* GS-string buffer format: [word bufSize][word length][chars...]
 * Sized like ORCA/M linker's fileBuffSize (8KB) for the input
 * list; smaller for the rest since they carry at most one path
 * or name. Allocated on the heap so they don't bloat ~_ROOT. */
#define LINFO_SFILE_MAX  8192
#define LINFO_DFILE_MAX  512
#define LINFO_PARMS_MAX  256
#define LINFO_ISTR_MAX   256

/* Flag bit masks - from linker.asm:81-88 */
#define LNK_FLAG_B  0x40000000UL  /* +B: bank org */
#define LNK_FLAG_C  0x20000000UL  /* -C: no compact */
#define LNK_FLAG_L  0x00100000UL  /* +L: list segments */
#define LNK_FLAG_M  0x00080000UL  /* +M: memory-only */
#define LNK_FLAG_P  0x00010000UL  /* -P: no progress */
#define LNK_FLAG_S  0x00002000UL  /* +S: list symbols */
#define LNK_FLAG_W  0x00000200UL  /* +W: pause on error */
#define LNK_FLAG_X  0x00000100UL  /* -X: no expressload */

/* Colon -> slash on GS/OS-style paths, in place. */
static void ColonToSlash(char *s)
{
while (*s) {
    if (*s == ':') *s = '/';
    s++;
    }
}

/* Split src on ASCII spaces/tabs, calling AddInputFile on each
 * token. Modifies src in place (inserts NULs at separators). */
static void AddInputFilesFromList(char *src)
{
char *p = src;
char *tok;

while (*p) {
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) break;
    tok = p;
    while (*p && *p != ' ' && *p != '\t') p++;
    if (*p) { *p = 0; p++; }
    ColonToSlash(tok);
    AddInputFile(tok);
    }
}

/* Walk prefix {13}, queueing every ProDOS-type-$B2 (LIB) entry
 * as "13/<name>". Matches ORCA/M linker's GetLibFile
 * (file.asm:238) - the real source of libc / ORCALib / SysLib
 * auto-linkage. */
static void EnumLibPrefix13(void)
{
static OpenRecGS      openPB;
static RefNumRecGS    closePB;
static DirEntryRecGS  dirPB;
static GSString255    openPath;
static ResultBuf255   entryName;
char                  libPath[PATH_MAX];
Word                  index;

memset(&openPath, 0, sizeof(openPath));
openPath.length = 3;
openPath.text[0] = '1';
openPath.text[1] = '3';
openPath.text[2] = '/';

openPB.pCount        = 2;
openPB.pathname      = &openPath;
openPB.refNum        = 0;
openPB.requestAccess = 0;
OpenGS(&openPB);
if (toolerror()) return;

memset(&entryName, 0, sizeof(entryName));
entryName.bufSize = sizeof(entryName.bufString);

for (index = 1; ; index++) {
    memset(&dirPB, 0, sizeof(dirPB));
    /* pCount=7 so fileType is populated (positions after pCount:
     * refNum, flags, base, displacement, name, entryNum, fileType). */
    dirPB.pCount       = 7;
    dirPB.refNum       = openPB.refNum;
    dirPB.base         = 0;
    dirPB.displacement = index;
    dirPB.name         = &entryName;

    GetDirEntryGS(&dirPB);
    if (toolerror()) break;

    if (dirPB.fileType == 0xB2) {
        int nlen = entryName.bufString.length;
        if (nlen > (int)(sizeof libPath - 4)) nlen = sizeof libPath - 4;
        libPath[0] = '1';
        libPath[1] = '3';
        libPath[2] = '/';
        memcpy(libPath + 3, entryName.bufString.text, nlen);
        libPath[3 + nlen] = 0;
        AddLibFile(libPath);
        }
    }

closePB.pCount = 1;
closePB.refNum = openPB.refNum;
CloseGS(&closePB);
}

/* A GS result-string buffer: [word bufSize][word length][chars...].
 * Allocated to hold maxChars+1 bytes of text (NUL-terminable for
 * downstream C-string use). Returns NULL on OOM. */
static void *AllocGSBuf(int maxChars)
{
unsigned short *buf = (unsigned short *)malloc(4 + maxChars + 1);
if (!buf) return NULL;
buf[0] = (unsigned short)(maxChars + 4);  /* bufSize (total incl. hdr) */
buf[1] = 0;                               /* length */
return buf;
}

/* Extract actLen + NUL-terminate text; returns ptr to chars. */
static char *GSBufText(void *buf, int *outLen)
{
unsigned short *hdr = (unsigned short *)buf;
char *text = (char *)(hdr + 2);
int len = hdr[1];
*outLen = len;
text[len] = 0;
return text;
}

/* Query GetLInfoGS; if a source-file list came through, populate
 * clinker globals and return TRUE. Empty sFile or shell error
 * means we weren't invoked via iix link - caller falls back to
 * argv parsing. */
BOOLEAN LoadFromLInfo(void)
{
static GetLInfoGSPB pb;
void *sFileBuf, *dFileBuf, *parmsBuf, *iStringBuf;
char *sText, *dText;
int sLen, dLen;

sFileBuf   = AllocGSBuf(LINFO_SFILE_MAX);
dFileBuf   = AllocGSBuf(LINFO_DFILE_MAX);
parmsBuf   = AllocGSBuf(LINFO_PARMS_MAX);
iStringBuf = AllocGSBuf(LINFO_ISTR_MAX);
if (!sFileBuf || !dFileBuf || !parmsBuf || !iStringBuf) {
    free(sFileBuf); free(dFileBuf); free(parmsBuf); free(iStringBuf);
    return FALSE;
    }

pb.pCount  = 11;
pb.sFile   = sFileBuf;
pb.dFile   = dFileBuf;
pb.parms   = parmsBuf;
pb.iString = iStringBuf;

GetLInfoGS(&pb);
if (toolerror()) {
    free(sFileBuf); free(dFileBuf); free(parmsBuf); free(iStringBuf);
    return FALSE;
    }

sText = GSBufText(sFileBuf, &sLen);
dText = GSBufText(dFileBuf, &dLen);

if (sLen == 0) {
    free(sFileBuf); free(dFileBuf); free(parmsBuf); free(iStringBuf);
    return FALSE;
    }

if (dLen) {
    strncpy(keepName, dText, PATH_MAX - 1);
    ColonToSlash(keepName);
    SetBaseName();
    }

if (pb.pFlags & LNK_FLAG_L) opt_list     = TRUE;
if (pb.pFlags & LNK_FLAG_S) opt_symbols  = TRUE;
if (pb.pFlags & LNK_FLAG_W) opt_pause    = TRUE;
if (pb.pFlags & LNK_FLAG_M) opt_memory   = TRUE;
if (pb.pFlags & LNK_FLAG_B) opt_bankorg  = TRUE;
if (pb.mFlags & LNK_FLAG_X) opt_express  = FALSE;
if (pb.mFlags & LNK_FLAG_C) opt_compact  = FALSE;
if (pb.mFlags & LNK_FLAG_P) opt_progress = FALSE;

/* Libraries first (ORCA/M convention: matches iix link ordering). */
EnumLibPrefix13();

AddInputFilesFromList(sText);

/* Don't free sFileBuf — AddInputFile may retain pointers into it
 * via the fopen'd file names we stashed. Actually no — we strncpy
 * into inf->name, so we own our copy. Free. */
free(sFileBuf);
free(dFileBuf);
free(parmsBuf);
free(iStringBuf);
return TRUE;
}

/* Report link result to the shell so `iix link`'s pass/fail
 * check (maxErrFound > maxErr) sees it. Called only when
 * LoadFromLInfo succeeded. */
void ReportToShell(void)
{
static SetLInfoGSPB pb;

memset(&pb, 0, sizeof(pb));
pb.pCount = 11;
pb.merrf  = (numErrors > 0) ? 2 : 0;
SetLInfoGS(&pb);
}
