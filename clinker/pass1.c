/**************************************************************
*
*  pass1.c -- Pass 1 for clinker
*
*  Walks each input segment body to measure its logical length
*  and collect symbol definitions into the hash table.
*
*  OMF v2 symbol record formats (all opcode bytes are consumed
*  by the dispatch loop before these handlers run):
*
*  GLOBAL ($E6) / LOCAL ($EF):
*    pstring name
*    word    length-attribute   (v0/v1: 1 byte; v2: 2 bytes)
*    byte    type-attribute
*    byte    private-flag
*    (value = current PC, not stored in record)
*
*  GEQU ($E7) / EQU ($F0):
*    pstring name
*    word    length-attribute
*    byte    type-attribute
*    byte    private-flag
*    expr... $00   (expression; walks until end opcode)
*
*  EXPR ($EB) / ZEXPR ($EC) / BEXPR ($ED):
*    byte    patchLen    (advances PC by this many bytes)
*    expr... $00
*
*  RELEXPR ($EE):
*    byte    patchLen
*    dword   base displacement
*    expr... $00
*
*  Expression opcodes (terminated by $00):
*    $00         end
*    $01-$15     arithmetic/logical operators (1 byte, no payload)
*    $80         PC value (1 byte, no payload)
*    $81         4-byte constant
*    $82/$83     weak/strong label reference (pstring name)
*    $87         4-byte segment displacement
*
**************************************************************/

#pragma keep "pass1"
#pragma optimize 9

#include "clinker.h"

/* ----------------------------------------------------------
   SkipExpression

   Walk an expression stream (starting at current fp position)
   until the $00 end opcode, advancing fp past it.
   ---------------------------------------------------------- */
static void SkipExpression(FILE *fp)
{
int op;
for (;;) {
    op = fgetc(fp);
    if (op == EOF || op == EXPR_END) return;
    if (op >= 0x01 && op <= 0x15) {
        /* operator: no payload */
        }
    else if (op == EXPR_PC) {
        /* no payload */
        }
    else if (op == EXPR_CONST || op == EXPR_SEGDISP) {
        /* 4-byte payload */
        fseek(fp, 4, SEEK_CUR);
        }
    else if (op == EXPR_WEAK || op == EXPR_STRONG ||
             op == 0x84 || op == 0x85 || op == 0x86) {
        /* pstring name: record as requested for library search */
        char name[NAME_MAX];
        int  nlen = fgetc(fp);
        if (nlen != EOF && nlen > 0) {
            int i, c;
            for (i = 0; i < nlen && i < NAME_MAX - 1; i++) {
                c = fgetc(fp);
                if (c == EOF) break;
                name[i] = (char)toupper(c);
                }
            name[i] = 0;
            for (; i < nlen; i++) fgetc(fp);
            SymRequest(name, 1);
            }
        }
    }
}

/* ----------------------------------------------------------
   EvalExpression

   Evaluate an expression stream terminated by $00.
   Sets *result to the computed value.
   Sets *needsReloc if the result depends on a segment symbol.
   Returns 1 on success.
   ---------------------------------------------------------- */
static int EvalExpression(FILE *fp, long pc,
                          long *result, int *segOut,
                          BOOLEAN *needsReloc)
{
long stack[32];
int  top = 0;
int  op;

*result     = 0;
*segOut     = 0;
*needsReloc = FALSE;

for (;;) {
    op = fgetc(fp);
    if (op == EOF || op == EXPR_END) break;

    if (op >= 0x01 && op <= 0x15) {
        long b = (top > 0) ? stack[--top] : 0;
        long a = (top > 0) ? stack[--top] : 0;
        long r = 0;
        switch (op) {
            case 0x01: r = a + b;  break;
            case 0x02: r = a - b;  break;
            case 0x03: r = a * b;  break;
            case 0x04: r = b ? a / b : 0; break;
            case 0x05: r = b ? a % b : 0; break;
            case 0x06: r = ~b;     break;
            case 0x09: r = a & b;  break;
            case 0x0A: r = a | b;  break;
            case 0x0B: r = a ^ b;  break;
            default:   r = a + b;  break;
            }
        if (top < 32) stack[top++] = r;
        }
    else if (op == EXPR_PC) {
        if (top < 32) stack[top++] = pc;
        }
    else if (op == EXPR_CONST) {
        dword v;
        OmfReadDword(fp, &v);
        if (top < 32) stack[top++] = (long)v;
        }
    else if (op == EXPR_SEGDISP) {
        dword v;
        OmfReadDword(fp, &v);
        if (top < 32) stack[top++] = (long)v;
        *needsReloc = TRUE;
        }
    else if (op == EXPR_STRONG || op == EXPR_WEAK ||
             op == 0x84 || op == 0x85 || op == 0x86) {
        char name[NAME_MAX];
        Symbol *sym;
        OmfReadPString(fp, name, NAME_MAX);
        {
        char *p;
        for (p = name; *p; p++) *p = (char)toupper(*p);
        }
        sym = SymFind(name);
        if (!sym || !(sym->flags & SYM_PASS1_RESOLVED)) {
            SymRequest(name, 1);  /* mark for library search */
            if (top < 32) stack[top++] = 0;
            } else {
            if (top < 32) stack[top++] = sym->value;
            if (!(sym->flags & SYM_IS_CONSTANT)) {
                *segOut     = sym->segNum;
                *needsReloc = TRUE;
                }
            }
        }
    }

*result = (top > 0) ? stack[top - 1] : 0;
return 1;
}

/* ----------------------------------------------------------
   ReadSymAttrs

   Read the 4 bytes of symbol attributes that follow the name
   in a GLOBAL/LOCAL/GEQU/EQU record (v2 format):
     word length-attribute
     byte type
     byte private-flag
   ---------------------------------------------------------- */
static void ReadSymAttrs(FILE *fp, int *outPrivate)
{
word lenAttr;
byte privAttr;

OmfReadWord(fp, &lenAttr);
fgetc(fp);  /* type attribute (not used) */
privAttr = (byte)fgetc(fp);

if (outPrivate) *outPrivate = (int)privAttr;
}

/* ----------------------------------------------------------
   DefineFromRecord

   Called with the opcode already consumed.  Reads one
   GLOBAL/LOCAL/GEQU/EQU record and defines the symbol.
   ---------------------------------------------------------- */
static void DefineFromRecord(FILE *fp, int opcode, InSeg *seg, long pc)
{
char    name[NAME_MAX];
int     isPrivate = 0;
int     symFlags;
long    value;
BOOLEAN needsReloc;
int     segOut;

OmfReadPString(fp, name, NAME_MAX);
{
char *p;
for (p = name; *p; p++) *p = (char)toupper(*p);
}

ReadSymAttrs(fp, &isPrivate);

symFlags = SYM_PASS1_RESOLVED;
if (isPrivate) symFlags |= SEGKIND_PRIVATE;

if (opcode == OP_GLOBAL || opcode == OP_LOCAL) {
    value = pc;
    } else {
    /* EQU / GEQU: expression follows */
    EvalExpression(fp, pc, &value, &segOut, &needsReloc);
    symFlags |= SYM_IS_CONSTANT;
    }

SymDefine(name, value, seg->outSegNum, symFlags);
}

/* ----------------------------------------------------------
   MeasureBody

   Walks the body of one input segment to compute its logical
   length.  Sets seg->measuredLen.  Returns the length on
   success, -1 on error.
   ---------------------------------------------------------- */
long MeasureBody(FILE *fp, InSeg *seg)
{
long pc = 0;
int  op;

if (fseek(fp, seg->fileBodyOffset, SEEK_SET) != 0)
    return -1;

for (;;) {
    op = fgetc(fp);
    if (op == EOF) break;
    op &= 0xFF;

    if (op == OP_END) {
        break;
        }
    else if (op >= 0x01 && op <= 0xDF) {
        /* Const: opcode = byte count */
        pc += op;
        fseek(fp, op, SEEK_CUR);
        }
    else if (op == OP_DS) {
        dword len;
        OmfReadDword(fp, &len);
        pc += (long)len;
        }
    else if (op == OP_LCONST) {
        dword len;
        OmfReadDword(fp, &len);
        pc += (long)len;
        fseek(fp, (long)len, SEEK_CUR);
        }
    else if (op == OP_ALIGN) {
        dword factor;
        OmfReadDword(fp, &factor);
        if (factor > 1) {
            long mask = (long)factor - 1;
            pc = (pc + mask) & ~mask;
            }
        }
    else if (op == OP_ORG) {
        dword newpc;
        OmfReadDword(fp, &newpc);
        pc = (long)newpc;
        }
    else if (op == OP_GLOBAL || op == OP_LOCAL) {
        DefineFromRecord(fp, op, seg, pc);
        }
    else if (op == OP_GEQU || op == OP_EQU) {
        DefineFromRecord(fp, op, seg, pc);
        }
    else if (op == OP_EXPR || op == OP_ZEXPR || op == OP_BEXPR) {
        byte pLen = (byte)fgetc(fp);
        SkipExpression(fp);
        pc += (long)pLen;
        }
    else if (op == OP_RELEXPR) {
        byte pLen = (byte)fgetc(fp);
        fseek(fp, 4, SEEK_CUR);  /* base displacement */
        SkipExpression(fp);
        pc += (long)pLen;
        }
    else if (op == OP_LEXPR) {
        /* LEXPR ($F3): like EXPR but patch is in outer segment; same layout */
        byte pLen = (byte)fgetc(fp);
        SkipExpression(fp);
        pc += (long)pLen;
        }
    else if (op == OP_ENTRY) {
        /* ENTRY ($F4): word(reserved) + dword(value) + pstring(name) */
        word  w;
        dword v;
        int   nlen;
        OmfReadWord(fp, &w);
        OmfReadDword(fp, &v);
        nlen = fgetc(fp);
        if (nlen != EOF) fseek(fp, nlen, SEEK_CUR);
        }
    else if (op == OP_RELOC) {
        /* pLen(1) shift(1) pc(4) value(4) = 10 more bytes */
        fseek(fp, 10, SEEK_CUR);
        }
    else if (op == OP_INTERSEG) {
        /* pLen(1) shift(1) pc(4) fileNum(2) segNum(2) addend(4) = 14 bytes */
        fseek(fp, 14, SEEK_CUR);
        }
    else if (op == OP_CRELOC) {
        /* pLen(1) shift(1) pc16(2) value16(2) = 6 bytes */
        fseek(fp, 6, SEEK_CUR);
        }
    else if (op == OP_CINTERSEG) {
        /* pLen(1) shift(1) pc16(2) segNum(1) value16(2) = 7 bytes */
        fseek(fp, 7, SEEK_CUR);
        }
    else if (op == OP_SUPER) {
        dword len;
        OmfReadDword(fp, &len);
        fseek(fp, (long)len, SEEK_CUR);
        }
    else if (op == OP_STRONG || op == OP_USING) {
        int nlen = fgetc(fp);
        if (nlen != EOF) fseek(fp, nlen, SEEK_CUR);
        }
    else if (op == OP_MEM) {
        /* MEM: two 4-byte numbers */
        fseek(fp, 8, SEEK_CUR);
        }
    else {
        char msg[64];
        sprintf(msg, "unknown OMF record $%02X in pass 1", op);
        LinkError(msg, seg->segName);
        break;
        }
    }

seg->measuredLen = pc;
return pc;
}

/* ----------------------------------------------------------
   Pass1Seg

   Process one input segment during pass 1.
   ---------------------------------------------------------- */
int Pass1Seg(InputFile *inf, InSeg *seg)
{
long measured;
OutSeg *out;

measured = MeasureBody(inf->fp, seg);
if (measured < 0) return 0;

out = FindOrCreateOutSeg(seg->loadName, seg->segName, seg->segType);
seg->outSegNum  = out->segNum;
seg->baseOffset = out->length;
out->length    += measured;

/* Register segment name as a symbol (always define; may already exist
 * from SymRequest before this segment was processed) */
if (seg->segName[0])
    SymDefine(seg->segName, seg->baseOffset, out->segNum,
              SYM_PASS1_RESOLVED | SYM_IS_SEGMENT);

return 1;
}

/* ----------------------------------------------------------
   Pass1

   Iterates all input files and all segments within each.
   ---------------------------------------------------------- */
int Pass1(void)
{
InputFile *inf;
int fileNum = 0;

for (inf = inputFiles; inf; inf = inf->next) {
    long segOff = 0;
    InSeg *seg, *tail = NULL;

    fileNum++;
    inf->fileNum = fileNum;

    if (opt_progress)
        printf("Pass 1: %s\n", inf->name);

    for (;;) {
        long diskLen;

        seg = (InSeg *)malloc(sizeof(InSeg));
        if (!seg) FatalError("out of memory (InSeg)");
        memset(seg, 0, sizeof(InSeg));
        seg->inputFileNum = fileNum;

        if (!OmfReadHeader(inf->fp, seg, segOff)) {
            free(seg);
            break;
            }

        diskLen = seg->bodyLen;  /* bodyLen = BLKCNT*512 at this point */
        if (diskLen == 0) {
            free(seg);
            break;
            }

        /* Link into file's segment list */
        if (!inf->segs)
            inf->segs = seg;
        else
            tail->next = seg;
        tail = seg;

        Pass1Seg(inf, seg);

        segOff += diskLen;
        }
    }

return (numErrors == 0);
}

/* ----------------------------------------------------------
   SegDefinesNeededSym

   Quick scan of a segment body: return 1 if it defines any
   symbol that is requested but not yet resolved.
   ---------------------------------------------------------- */
static int SegDefinesNeededSym(FILE *fp, InSeg *seg)
{
int op;
Symbol *sym;

/* Check segment name itself (segment names become symbols) */
if (seg->segName[0]) {
    char *p;
    char upper[NAME_MAX];
    strncpy(upper, seg->segName, NAME_MAX - 1);
    upper[NAME_MAX - 1] = 0;
    for (p = upper; *p; p++) *p = (char)toupper(*p);
    sym = SymFind(upper);
    if (sym && (sym->flags & SYM_PASS1_REQUESTED) &&
               !(sym->flags & SYM_PASS1_RESOLVED))
        return 1;
    }

if (fseek(fp, seg->fileBodyOffset, SEEK_SET) != 0)
    return 0;

for (;;) {
    op = fgetc(fp);
    if (op == EOF) break;
    op &= 0xFF;

    if (op == OP_END) break;
    else if (op >= 0x01 && op <= 0xDF) {
        fseek(fp, op, SEEK_CUR);
        }
    else if (op == OP_DS || op == OP_LCONST || op == OP_SUPER) {
        dword len;
        OmfReadDword(fp, &len);
        if (op != OP_DS) fseek(fp, (long)len, SEEK_CUR);
        }
    else if (op == OP_GLOBAL || op == OP_LOCAL) {
        char name[NAME_MAX];
        Symbol *sym;
        OmfReadPString(fp, name, NAME_MAX);
        {
        char *p;
        for (p = name; *p; p++) *p = (char)toupper(*p);
        }
        fseek(fp, 4, SEEK_CUR);  /* skip 4 bytes of attrs */
        sym = SymFind(name);
        if (sym && (sym->flags & SYM_PASS1_REQUESTED) &&
                   !(sym->flags & SYM_PASS1_RESOLVED))
            return 1;
        }
    else if (op == OP_GEQU || op == OP_EQU) {
        char name[NAME_MAX];
        Symbol *sym;
        OmfReadPString(fp, name, NAME_MAX);
        {
        char *p;
        for (p = name; *p; p++) *p = (char)toupper(*p);
        }
        fseek(fp, 4, SEEK_CUR);  /* skip attrs */
        SkipExpression(fp);
        sym = SymFind(name);
        if (sym && (sym->flags & SYM_PASS1_REQUESTED) &&
                   !(sym->flags & SYM_PASS1_RESOLVED))
            return 1;
        }
    else if (op == OP_RELOC)  { fseek(fp, 10, SEEK_CUR); }
    else if (op == OP_INTERSEG) { fseek(fp, 14, SEEK_CUR); }
    else if (op == OP_CRELOC)   { fseek(fp, 6, SEEK_CUR); }
    else if (op == OP_CINTERSEG){ fseek(fp, 7, SEEK_CUR); }
    else if (op == OP_ALIGN || op == OP_ORG) { fseek(fp, 4, SEEK_CUR); }
    else if (op == OP_EXPR || op == OP_ZEXPR || op == OP_BEXPR ||
             op == OP_LEXPR) {
        fgetc(fp);
        SkipExpression(fp);
        }
    else if (op == OP_RELEXPR) {
        fgetc(fp);
        fseek(fp, 4, SEEK_CUR);
        SkipExpression(fp);
        }
    else if (op == OP_ENTRY) {
        word  w; dword v; int nlen;
        OmfReadWord(fp, &w);
        OmfReadDword(fp, &v);
        nlen = fgetc(fp);
        if (nlen != EOF) fseek(fp, nlen, SEEK_CUR);
        }
    else if (op == OP_STRONG || op == OP_USING || op == OP_MEM) {
        if (op == OP_MEM) { fseek(fp, 8, SEEK_CUR); }
        else { int nlen = fgetc(fp); if (nlen != EOF) fseek(fp, nlen, SEEK_CUR); }
        }
    else break;  /* unknown: stop scan */
    }
return 0;
}

/* ----------------------------------------------------------
   LibrarySearch

   After Pass1, repeatedly scan library files for segments
   that define symbols needed (SYM_PASS1_REQUESTED) but not
   yet resolved.  Pull matching segments into inputFiles and
   run Pass1Seg on them.  Repeat until stable.
   ---------------------------------------------------------- */
void LibrarySearch(void)
{
BOOLEAN changed;
static int libFileSeq = 0;

if (!libFiles) return;

do {
    LibFile *lf;
    changed = FALSE;

    for (lf = libFiles; lf; lf = lf->next) {
        long segOff = 0;

        for (;;) {
            InSeg     *seg;
            InputFile *inf;
            long       diskLen;

            seg = (InSeg *)malloc(sizeof(InSeg));
            if (!seg) FatalError("out of memory (LibSeg)");
            memset(seg, 0, sizeof(InSeg));

            if (!OmfReadHeader(lf->fp, seg, segOff)) {
                free(seg);
                break;
                }

            diskLen = seg->bodyLen;
            if (diskLen == 0) {
                free(seg);
                break;
                }

            if (SegDefinesNeededSym(lf->fp, seg)) {
                /* Pull this segment: wrap in a minimal InputFile */
                inf = (InputFile *)malloc(sizeof(InputFile));
                if (!inf) FatalError("out of memory (LibInputFile)");
                memset(inf, 0, sizeof(InputFile));
                inf->fp      = lf->fp;
                inf->isLib   = TRUE;
                inf->fileNum = ++libFileSeq;
                strncpy(inf->name, lf->path, PATH_MAX - 1);
                inf->segs    = seg;

                /* Append to inputFiles */
                if (!inputFiles)
                    inputFiles = inf;
                else
                    inputTail->next = inf;
                inputTail = inf;

                Pass1Seg(inf, seg);
                changed = TRUE;
                } else {
                free(seg);
                }

            segOff += diskLen;
            }

        /* Reset library file position for next iteration */
        fseek(lf->fp, 0, SEEK_SET);
        }
    } while (changed);
}

