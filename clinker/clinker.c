/**************************************************************
*
*  clinker.c -- ORCA/C reimplementation of the ORCA/M linker
*
*  Accepts the same command-line interface as the assembly
*  linker.  Produces identical OMF v2 output suitable for
*  loading by GS/OS.
*
*  Usage:
*    clinker [flags] file ... [keep=outfile]
*
*  Flags (same as asm linker):
*    +L    list segment info
*    +S    list symbol table after link
*    -X    disable express-load segment (default: enabled)
*    +W    pause on error
*    +M    memory-only link (no keep file written)
*    -C    disable compact records
*    +B    bank-org mode
*    -P    disable progress messages (default: show progress)
*
*  Shell variable:
*    gsplusSymbols=1   inject WDM prologue + write .symbols file
*
**************************************************************/

#pragma keep "clinker"
#pragma optimize 9

#include "clinker.h"

/* ----------------------------------------------------------
   Global state
   ---------------------------------------------------------- */

BOOLEAN  opt_list     = FALSE;
BOOLEAN  opt_symbols  = FALSE;
BOOLEAN  opt_express  = FALSE; /* express loading requires separate reloc dict; disabled until implemented */
BOOLEAN  opt_pause    = FALSE;
BOOLEAN  opt_memory   = FALSE;
BOOLEAN  opt_compact  = TRUE;
BOOLEAN  opt_bankorg  = FALSE;
BOOLEAN  opt_progress = TRUE;
BOOLEAN  opt_gsplus   = FALSE;
char     keepName[PATH_MAX] = "";
char     baseName[PATH_MAX] = "";

int      numErrors  = 0;
InputFile *inputFiles = NULL;
InputFile *inputTail  = NULL;
LibFile   *libFiles   = NULL;
OutSeg    *outSegs    = NULL;
int        numOutSegs = 0;
dword      sfSig      = 0;

/* ----------------------------------------------------------
   Error reporting
   ---------------------------------------------------------- */

void LinkError(const char *msg, const char *name)
{
numErrors++;
if (name && name[0])
    fprintf(stderr, "clinker: error: %s: %s\n", msg, name);
else
    fprintf(stderr, "clinker: error: %s\n", msg);
}

void FatalError(const char *msg)
{
fprintf(stderr, "clinker: fatal: %s\n", msg);
exit(1);
}

/* ----------------------------------------------------------
   Output segment management
   ---------------------------------------------------------- */

OutSeg *FindOrCreateOutSeg(const char *loadName, const char *segName,
                            word segType)
{
OutSeg *s;

/* Search for existing segment with matching name and type */
for (s = outSegs; s; s = s->next) {
    if (strcmp(s->segName, segName) == 0 &&
        (s->segType & 0x1F) == (segType & 0x1F))
        return s;
    }

/* Create a new output segment */
s = (OutSeg *)malloc(sizeof(OutSeg));
if (!s) FatalError("out of memory (OutSeg)");
memset(s, 0, sizeof(OutSeg));

strncpy(s->loadName, loadName, NAME_MAX - 1);
strncpy(s->segName,  segName,  NAME_MAX - 1);
s->segType   = segType & 0x1F;
s->banksize  = 0x10000L;
s->segNum    = ++numOutSegs;
s->length    = 0;

/* Append to list */
if (!outSegs) {
    outSegs = s;
    } else {
    OutSeg *t;
    for (t = outSegs; t->next; t = t->next) /* walk to end */ ;
    t->next = s;
    }

return s;
}

OutSeg *OutSegByNum(int n)
{
OutSeg *s;
for (s = outSegs; s; s = s->next)
    if (s->segNum == n) return s;
return NULL;
}

/* ----------------------------------------------------------
   Output data buffer helpers
   ---------------------------------------------------------- */

void GrowData(OutSeg *seg, long need)
{
long required = seg->dataLen + need + 16;
if (required <= seg->dataAlloc) return;
seg->data = (byte *)realloc(seg->data, (size_t)required);
if (!seg->data) FatalError("out of memory (GrowData)");
memset(seg->data + seg->dataAlloc, 0,
       (size_t)(required - seg->dataAlloc));
seg->dataAlloc = required;
}

void EmitData(OutSeg *seg, const byte *src, long len)
{
if (len <= 0) return;
GrowData(seg, len);
memcpy(seg->data + seg->dataLen, src, (size_t)len);
seg->dataLen += len;
}

void EmitZero(OutSeg *seg, long len)
{
if (len <= 0) return;
GrowData(seg, len);
memset(seg->data + seg->dataLen, 0, (size_t)len);
seg->dataLen += len;
}

/* ----------------------------------------------------------
   Relocation record management
   ---------------------------------------------------------- */

void AppendReloc(OutSeg *seg, long pc, byte patchLen, byte shift,
                 long value, int type, int segNum, int fileNum)
{
RelocRec *r = (RelocRec *)malloc(sizeof(RelocRec));
if (!r) FatalError("out of memory (RelocRec)");
r->pc       = pc;
r->patchLen = patchLen;
r->shift    = shift;
r->value    = value;
r->type     = type;
r->segNum   = segNum;
r->fileNum  = fileNum;
r->next     = NULL;

if (!seg->relocHead)
    seg->relocHead = r;
else
    seg->relocTail->next = r;
seg->relocTail = r;
}

/* ----------------------------------------------------------
   GSplus support
   ---------------------------------------------------------- */

void InitGsplusSymbols(void)
{
char *envval;

envval = getenv("gsplusSymbols");
if (!envval || !envval[0]) {
    opt_gsplus = FALSE;
    return;
    }
opt_gsplus = TRUE;

/* Generate signature: seed from baseName characters */
{
dword sig = 0x12345678UL;  /* seed */
char *p;
for (p = baseName; *p; p++) {
    sig = (sig << 7) | (sig >> 25);  /* rotate left 7 */
    sig ^= (dword)(unsigned char)*p;
    }
sfSig = sig;
}
}

void WriteSymbolFile(void)
{
char symPath[PATH_MAX + 16];
FILE *fp;
OutSeg *seg;
Symbol *sym;
int i;
BOOLEAN firstSeg, firstSym;

if (!opt_gsplus || !keepName[0]) return;

snprintf(symPath, sizeof(symPath), "%s.symbols", keepName);
fp = fopen(symPath, "w");
if (!fp) {
    LinkError("cannot create symbol file", symPath);
    return;
    }

/* Emit JSON: single line, no pretty-printing */
fprintf(fp, "{");
fprintf(fp, "\"orca_symbols_version\":\"0x0001\",");
fprintf(fp, "\"symsig\":\"0x%08lX\",", sfSig);
fprintf(fp, "\"target\":\"%s\",", baseName);
fprintf(fp, "\"linker\":\"ORCA/M Link Editor 2.3.0 (clinker)\",");

/* Segments array */
fprintf(fp, "\"segments\":[");
firstSeg = TRUE;
for (seg = outSegs; seg; seg = seg->next) {
    const char *typeStr;
    if (!firstSeg) fprintf(fp, ",");
    firstSeg = FALSE;
    switch (seg->segType & 0x1F) {
        case 0x00: typeStr = "code";      break;
        case 0x01: typeStr = "data";      break;
        case 0x12: typeStr = "jumptable"; break;
        default:   typeStr = "other";     break;
        }
    fprintf(fp, "{\"number\":\"0x%04X\",", seg->segNum);
    fprintf(fp, "\"name\":\"%s\",",         seg->segName);
    fprintf(fp, "\"type\":\"%s\",",         typeStr);
    fprintf(fp, "\"org\":\"0x%08lX\",",     seg->org);
    fprintf(fp, "\"length\":\"0x%08lX\"}",  seg->dataLen);
    }
fprintf(fp, "],");

/* Symbols array */
fprintf(fp, "\"symbols\":[");
firstSym = TRUE;
for (i = 0; i < SYM_HASH_SIZE; i++) {
    for (sym = symHash[i]; sym; sym = sym->next) {
        if (!(sym->flags & SYM_PASS1_RESOLVED)) continue;
        if (sym->flags & SYM_IS_SEGMENT)        continue;
        if (!firstSym) fprintf(fp, ",");
        firstSym = FALSE;
        fprintf(fp, "{\"name\":\"%s\",", sym->name);
        fprintf(fp, "\"segment\":\"0x%04X\",", sym->segNum);
        fprintf(fp, "\"offset\":\"0x%08lX\",", sym->value);
        fprintf(fp, "\"global\":%s}",
                (sym->flags & SEGKIND_PRIVATE) ? "false" : "true");
        }
    }
fprintf(fp, "]");
fprintf(fp, "}");

fclose(fp);

if (opt_progress)
    printf("Symbol file: %s\n", symPath);
}

/* ----------------------------------------------------------
   WriteExpressSegment

   Writes the express-load dynamic segment header (OMF type
   $8012) that GS/OS uses to locate the first real segment.
   ---------------------------------------------------------- */
static void WriteExpressSegment(FILE *fp, long dataSegOffset,
                                const char *loadName)
{
OutSeg expr;

memset(&expr, 0, sizeof(expr));
strcpy(expr.segName,  "~ExpressLoad");
strncpy(expr.loadName, loadName, NAME_MAX - 1);
expr.segType  = SEGTYPE_EXPRESS;
expr.banksize = 0x10000L;

/* Express body: 2 bytes (segment count word) + 4 bytes (offset) + END */
OmfWriteSegHeader(fp, &expr, 7L, 1, loadName);

/* body: segment count = 1 */
OmfWriteWord(fp, 1);
/* offset to first segment (4 bytes) */
OmfWriteDword(fp, dataSegOffset);
/* END */
OmfWriteByte(fp, OP_END);
}

/* ----------------------------------------------------------
   WriteOutput

   Writes all output segments to the keep file.
   ---------------------------------------------------------- */
static int WriteOutput(void)
{
FILE   *fp;
OutSeg *seg;
int     segNum = 1;
long    bodyLen;

if (!keepName[0]) return 1;   /* no keep= specified, nothing to write */

fp = fopen(keepName, "wb");
if (!fp) {
    LinkError("cannot create output file", keepName);
    return 0;
    }

if (opt_express) {
    /* Express segment goes first.  Compute its exact byte count so we can
     * write a same-size placeholder, fill in data segments, then fix up. */
    long exprSegLen   = (long)strlen("~ExpressLoad");
    long exprPadSeg   = (1 + exprSegLen) & 1;
    long exprBodyDisp = 44L + 10
                             + 1 + exprSegLen + exprPadSeg;
    long exprTotalLen = exprBodyDisp + 7L;  /* body: word + dword + END */
    long exprStart    = ftell(fp);
    long i;

    for (i = 0; i < exprTotalLen; i++) fputc(0, fp);

    long dataStart = ftell(fp);

    for (seg = outSegs; seg; seg = seg->next) {
        RelocRec *r;

        /* Compute body length: reloc records + data frame + END */
        bodyLen = 0;
        for (r = seg->relocHead; r; r = r->next)
            bodyLen += (r->type == 0) ? 11L : 15L;
        if (seg->dataLen > 0xDF)
            bodyLen += 6L + seg->dataLen;  /* LCONST(1)+len(4)+data+END */
        else if (seg->dataLen > 0)
            bodyLen += 2L + seg->dataLen;  /* opcode(1)+data+END */
        else
            bodyLen += 1L;                 /* END only */

        OmfWriteSegHeader(fp, seg, bodyLen, ++segNum, baseName);

        if (seg->dataLen > 0) {
            if (seg->dataLen > 0xDF) {
                OmfWriteByte (fp, OP_LCONST);
                OmfWriteDword(fp, seg->dataLen);
                fwrite(seg->data, 1, (size_t)seg->dataLen, fp);
                } else {
                OmfWriteByte(fp, (int)seg->dataLen);
                fwrite(seg->data, 1, (size_t)seg->dataLen, fp);
                }
            }
        OmfWriteSuper(fp, seg);
        OmfWriteByte(fp, OP_END);
        }

    /* Fix up the express-load segment placeholder */
    {
    long savedPos = ftell(fp);
    fseek(fp, exprStart, SEEK_SET);
    WriteExpressSegment(fp, dataStart - exprStart, baseName);
    fseek(fp, savedPos, SEEK_SET);
    }
    } else {
    /* No express segment: just write data segments directly */
    for (seg = outSegs; seg; seg = seg->next) {
        RelocRec *r;

        bodyLen = 0;
        for (r = seg->relocHead; r; r = r->next)
            bodyLen += (r->type == 0) ? 11L : 15L;
        if (seg->dataLen > 0xDF)
            bodyLen += 6L + seg->dataLen;
        else if (seg->dataLen > 0)
            bodyLen += 2L + seg->dataLen;
        else
            bodyLen += 1L;

        OmfWriteSegHeader(fp, seg, bodyLen, segNum++, baseName);

        if (seg->dataLen > 0) {
            if (seg->dataLen > 0xDF) {
                OmfWriteByte (fp, OP_LCONST);
                OmfWriteDword(fp, seg->dataLen);
                fwrite(seg->data, 1, (size_t)seg->dataLen, fp);
                } else {
                OmfWriteByte(fp, (int)seg->dataLen);
                fwrite(seg->data, 1, (size_t)seg->dataLen, fp);
                }
            }
        OmfWriteSuper(fp, seg);
        OmfWriteByte(fp, OP_END);
        }
    }

fclose(fp);
return (numErrors == 0);
}

/* ----------------------------------------------------------
   CLI parsing
   ---------------------------------------------------------- */

static void ParseFlag(const char *arg)
{
char sign = arg[0];  /* '+' or '-' */
char flag = (char)toupper((int)arg[1]);

if (sign == '+') {
    if      (flag == 'L') opt_list     = TRUE;
    else if (flag == 'S') opt_symbols  = TRUE;
    else if (flag == 'W') opt_pause    = TRUE;
    else if (flag == 'M') opt_memory   = TRUE;
    else if (flag == 'B') opt_bankorg  = TRUE;
    else if (flag == 'X') opt_express  = FALSE;  /* +X = no express (same as -X in asm) */
    else fprintf(stderr, "clinker: unknown flag: %s\n", arg);
    } else {
    if      (flag == 'X') opt_express  = FALSE;
    else if (flag == 'C') opt_compact  = FALSE;
    else if (flag == 'P') opt_progress = FALSE;
    else fprintf(stderr, "clinker: unknown flag: %s\n", arg);
    }
}

static void SetBaseName(void)
{
/* Extract the last path component, strip extension */
char *slash, *dot;
int i;

strncpy(baseName, keepName, PATH_MAX - 1);
slash = strrchr(baseName, '/');
if (!slash) slash = strrchr(baseName, ':');  /* GS/OS path separator */
if (slash)
    memmove(baseName, slash + 1, strlen(slash));

dot = strrchr(baseName, '.');
if (dot) *dot = 0;

/* Uppercase */
for (i = 0; baseName[i]; i++)
    baseName[i] = (char)toupper((int)baseName[i]);
}

static int GetParms(int argc, char *argv[])
{
int i;
InputFile *tail = NULL;
static int fileSeq = 0;

for (i = 1; i <= argc; i++) {
    char *arg = argv[i];
    if (!arg || !arg[0]) continue;

    if (arg[0] == '-' && arg[1] == 'o' && arg[2] == 0) {
        /* -o <file>: same as keep=<file> */
        if (i + 1 <= argc && argv[i + 1]) {
            i++;
            strncpy(keepName, argv[i], PATH_MAX - 1);
            SetBaseName();
            }
        }
    else if (arg[0] == '+' || arg[0] == '-') {
        ParseFlag(arg);
        }
    else if ((toupper((int)arg[0]) == 'K') &&
             (toupper((int)arg[1]) == 'E') &&
             (toupper((int)arg[2]) == 'E') &&
             (toupper((int)arg[3]) == 'P') && arg[4] == '=') {
        strncpy(keepName, arg + 5, PATH_MAX - 1);
        SetBaseName();
        }
    else if ((toupper((int)arg[0]) == 'L') &&
             (toupper((int)arg[1]) == 'I') &&
             (toupper((int)arg[2]) == 'B') && arg[3] == '=') {
        /* lib=<path>: add library file for symbol extraction */
        LibFile *lf = (LibFile *)malloc(sizeof(LibFile));
        if (!lf) FatalError("out of memory (LibFile)");
        memset(lf, 0, sizeof(LibFile));
        strncpy(lf->path, arg + 4, PATH_MAX - 1);
        lf->fp = fopen(lf->path, "rb");
        if (!lf->fp) {
            fprintf(stderr, "clinker: cannot open library: %s\n", lf->path);
            free(lf);
            numErrors++;
            } else {
            lf->next = libFiles;
            libFiles = lf;
            }
        }
    else {
        /* Input file.  If no extension and bare open fails, try .a then .A.
         * If the original arg had no extension, also try the .ROOT companion
         * file (ORCA/M assembler emits data/privdata segments there). */
        char tryName[PATH_MAX];
        int  noExt = !strchr(arg, '.');
        InputFile *inf = (InputFile *)malloc(sizeof(InputFile));
        if (!inf) FatalError("out of memory (InputFile)");
        memset(inf, 0, sizeof(InputFile));
        inf->fp = fopen(arg, "rb");
        if (!inf->fp && noExt) {
            snprintf(tryName, sizeof(tryName), "%s.a", arg);
            inf->fp = fopen(tryName, "rb");
            if (inf->fp) arg = tryName;
            }
        if (!inf->fp && noExt) {
            snprintf(tryName, sizeof(tryName), "%s.A", arg);
            inf->fp = fopen(tryName, "rb");
            if (inf->fp) arg = tryName;
            }
        if (!inf->fp) {
            fprintf(stderr, "clinker: cannot open: %s\n", arg);
            free(inf);
            numErrors++;
            continue;
            }
        strncpy(inf->name, arg, PATH_MAX - 1);
        inf->fileNum = ++fileSeq;
        if (!inputFiles)
            inputFiles = inf;
        else
            tail->next = inf;
        tail = inf;
        inputTail = inf;

        /* If original arg had no extension, also load the .ROOT companion.
         * The ORCA/M assembler writes data/privdata segments (which define
         * LOCAL symbols for direct-page variables) into a separate .ROOT file.
         * iix link includes it automatically; clinker must too. */
        if (noExt) {
            char rootName[PATH_MAX];
            FILE *rfp = NULL;
            InputFile *rootInf;

            snprintf(rootName, sizeof(rootName), "%s.ROOT", argv[i]);
            rfp = fopen(rootName, "rb");
            if (!rfp) {
                snprintf(rootName, sizeof(rootName), "%s.root", argv[i]);
                rfp = fopen(rootName, "rb");
                }
            if (rfp) {
                rootInf = (InputFile *)malloc(sizeof(InputFile));
                if (!rootInf) FatalError("out of memory (RootFile)");
                memset(rootInf, 0, sizeof(InputFile));
                rootInf->fp      = rfp;
                rootInf->fileNum = ++fileSeq;
                strncpy(rootInf->name, rootName, PATH_MAX - 1);
                tail->next = rootInf;
                tail       = rootInf;
                inputTail  = rootInf;
                }
            }
        }
    }
return (numErrors == 0);
}

/* ----------------------------------------------------------
   main
   ---------------------------------------------------------- */

int main(int argc, char *argv[])
{
int ok;

/* Parse command line */
if (!GetParms(argc, argv)) {
    fprintf(stderr, "clinker: aborting due to errors\n");
    return 1;
    }

if (!inputFiles) {
    fprintf(stderr, "clinker: no input files\n");
    return 1;
    }

/* Check shell variables */
InitGsplusSymbols();

if (opt_progress)
    puts("clinker 2.3.0 (ORCA/C)");

/* Pass 1: measure sizes and collect symbols */
ok = Pass1();
if (!ok && numErrors > 0) {
    fprintf(stderr, "clinker: %d error(s) in pass 1\n", numErrors);
    return 1;
    }

/* Library search: pull needed segments from library files */
if (libFiles)
    LibrarySearch();

/* Pass 2: generate output data */
ok = Pass2();
if (!ok && numErrors > 0) {
    fprintf(stderr, "clinker: %d error(s) in pass 2\n", numErrors);
    return 1;
    }

/* Write output file */
if (!opt_memory)
    ok = WriteOutput();

/* Write GSplus symbol file */
if (opt_gsplus)
    WriteSymbolFile();

/* Optionally dump symbol table */
if (opt_symbols)
    SymDump();

/* List segments */
if (opt_list) {
    OutSeg *s;
    for (s = outSegs; s; s = s->next)
        printf("Segment %d: %-16s type=%04X length=%08lX\n",
               s->segNum, s->segName, (unsigned)s->segType, s->dataLen);
    }

if (numErrors) {
    fprintf(stderr, "clinker: %d error(s)\n", numErrors);
    return 1;
    }

return 0;
}
