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

segment "PASS1";

/* Expression handling lives in expr.c — pass 1 uses EXPR_PHASE_COLLECT
 * to mark unresolved references for library search. */

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
static void DefineFromRecord(FILE *fp, int opcode, InSeg *seg, long pc,
                             int dataArea)
{
char    name[NAME_MAX];
int     isPrivate = 0;
int     symFlags;
long    value;
BOOLEAN needsReloc;
int     segOut, fileOut;
int     listDataArea;
int     isGlobal;

/* Pass the original-case name to SymDefine — it uppercases internally
 * for its hash/compare key but preserves the original in displayName
 * for +S output. */
OmfReadPString(fp, name, NAME_MAX);

ReadSymAttrs(fp, &isPrivate);

symFlags = SYM_PASS1_RESOLVED;
if (isPrivate) symFlags |= SEGKIND_PRIVATE;
segOut = seg->outSegNum;

/* OP_GLOBAL ($E6) and OP_GEQU ($E7) are externally visible;
 * OP_LOCAL ($EF) and OP_EQU ($F0) are file-internal. ORCA/C emits
 * many LOCAL symbols (helpers, static functions) whose names collide
 * with GLOBALs in other files — if we let a later LOCAL overwrite an
 * earlier GLOBAL in our flat symbol table, cross-file references
 * resolve to the wrong segment. Flag GLOBAL symbols so SymDefine can
 * reject downgrade attempts. */
isGlobal = (opcode == OP_GLOBAL || opcode == OP_GEQU);
if (isGlobal) symFlags |= SYM_IS_GLOBAL;

if (opcode == OP_GLOBAL || opcode == OP_LOCAL) {
    /* pc is the input-local offset. Translate to merged-output offset
     * by adding seg->baseOffset so the symbol's value is the same
     * coordinate system as OP_ENTRY and segName symbols elsewhere in
     * pass1. Without this, any LOCAL/GLOBAL in a non-first contributor
     * to its output segment gets sym->value short by baseOffset, and
     * pass2's EXPR evaluator writes the wrong LCONST / RELOC value
     * for every reference → loader patches to the wrong address →
     * kernel crashes at first indirect call. */
    value = pc + seg->baseOffset;
    } else {
    /* EQU / GEQU: expression follows. Track segOut from the
     * expression evaluator — an EQU whose RHS is a cross-segment
     * label (e.g. GEQU pstrlen = <label in KERN3>) must record the
     * TARGET segment, not the defining segment. Only the non-
     * relocatable case is SYM_IS_CONSTANT. */
    EvalExpr(fp, pc, &value, &segOut, &fileOut, &needsReloc,
             EXPR_PHASE_COLLECT, NULL, NULL);
    if (!needsReloc) {
        symFlags |= SYM_IS_CONSTANT;
        segOut = seg->outSegNum;
        }
    else if (segOut == 0) {
        segOut = seg->outSegNum;
        }
    }

/* +S listing rule (stock symbol.asm:374-381): GLOBAL and GEQU record
 * dataArea 0 regardless of what segment they appear in; LOCAL and EQU
 * record the segment's dataArea (0 for code segs, non-zero for data). */
listDataArea = isGlobal ? 0 : dataArea;
SymDefineData(name, value, segOut, symFlags, listDataArea);
/* SymDump applies the ExpressLoad +1 remap when it prints, so pass the
 * raw (pre-remap) segNum here. */
SymAddListEntry(name, value, segOut, listDataArea, symFlags);
}

/* ----------------------------------------------------------
   MeasureBody

   Walks the body of one input segment to compute its logical
   length.  Sets seg->measuredLen.  Returns the length on
   success, -1 on error.
   ---------------------------------------------------------- */
long MeasureBody(FILE *fp, InSeg *seg, int dataArea)
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
        DefineFromRecord(fp, op, seg, pc, dataArea);
        }
    else if (op == OP_GEQU || op == OP_EQU) {
        DefineFromRecord(fp, op, seg, pc, dataArea);
        }
    else if (op == OP_EXPR || op == OP_ZEXPR || op == OP_BEXPR) {
        byte pLen = (byte)fgetc(fp);
        SkipExpr(fp, EXPR_PHASE_COLLECT);
        pc += (long)pLen;
        }
    else if (op == OP_RELEXPR) {
        byte pLen = (byte)fgetc(fp);
        fseek(fp, 4, SEEK_CUR);  /* base displacement */
        SkipExpr(fp, EXPR_PHASE_COLLECT);
        pc += (long)pLen;
        }
    else if (op == OP_LEXPR) {
        /* LEXPR ($F3): like EXPR but patch is in outer segment; same layout */
        byte pLen = (byte)fgetc(fp);
        SkipExpr(fp, EXPR_PHASE_COLLECT);
        pc += (long)pLen;
        }
    else if (op == OP_ENTRY) {
        /* ENTRY ($F4): word(reserved) + dword(value) + pstring(name).
         *
         * Appendix F Table F-3 lists ENTRY as valid only in run-time
         * library dictionaries, but ORCA/Pascal uses it inside regular
         * object files too — it's how the compiler exposes nested
         * procedures as named entry points within their enclosing
         * segment.  The name resolves to (seg_base + value) and is
         * visible to INTERSEG references from other segments, the
         * same as a GLOBAL symbol.
         *
         * Without this SymDefine, linking real ORCA/C output fails
         * with hundreds of "undefined symbol" errors for names like
         * OUT, CNOUT, HASH, SAVEBF that Pascal emits as ENTRY
         * records. */
        char   name[NAME_MAX];
        word   reserved;
        dword  value;
        OmfReadWord(fp, &reserved);
        OmfReadDword(fp, &value);
        OmfReadPString(fp, name, NAME_MAX);
        /* SymDefine preserves original case in displayName for +S.
         * ENTRY is global — stock's Define rule stores dataArea=0 for
         * global non-segment symbols, so always 0 here regardless of
         * the enclosing seg's data-area. */
        if (name[0]) {
            long entryVal = seg->baseOffset + (long)value;
            SymDefineData(name, entryVal, seg->outSegNum,
                          SYM_PASS1_RESOLVED | SYM_IS_GLOBAL, 0);
            SymAddListEntry(name, entryVal, seg->outSegNum, 0,
                            SYM_PASS1_RESOLVED | SYM_IS_GLOBAL);
            }
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
        /* STRONG ($E5) forces the linker to resolve the named symbol
         * "as if a STRONG reference had been encountered."  USING
         * ($E4) names a data segment that this segment addresses —
         * the referenced segment must be included in the link for
         * direct-page addresses to resolve.  Both are pull triggers
         * (this is how iix pulls library private segments like
         * ~GSOSIO, whose segment name appears only in USING records
         * inside other pulled library segments). */
        char name[NAME_MAX];
        OmfReadPString(fp, name, NAME_MAX);
        SymRequest(name, 1);
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
int dataArea = 0;

/* Assign output segment BEFORE walking the body so any
 * OP_GLOBAL/OP_LOCAL/OP_ENTRY records inside the body see a valid
 * seg->outSegNum and seg->baseOffset when they call SymDefine. The
 * body walk is length-computation only — it doesn't depend on the
 * already-accumulated out->length, so we can grow that AFTER. */
out = FindOrCreateOutSeg(seg->loadName, seg->segName, seg->segkind);

/* GSplus WDM prologue reservation: when opt_gsplus, pass2 prepends an
 * 8-byte prologue to the very first output segment (outSegs head).
 * If we don't account for that prefix during pass1, every LOCAL/GLOBAL
 * symbol defined in that output seg will have a value 8 bytes short of
 * its post-injection position, and every reloc that references one of
 * those symbols resolves to the wrong address at load time. Pre-bump
 * out->length by 8 the FIRST time we see this output seg so all
 * subsequent baseOffset/symbol/reloc calculations see the shifted
 * layout. */
if (opt_gsplus && out == outSegs && out->length == 0)
    out->length = 8;

/* Assign data-area number for DATA-kind input segments. Stock's
 * seg.asm:1156 bumps lastDataNumber per DATA input seg — counter is
 * link-wide (not per-output-seg), which is why ~STRLEN in the output
 * shows data-area 24 even though it's the first data seg for *its*
 * output seg. Mirror that by making lastDataNumber a global. */
if ((seg->segkind & 0x1F) == SEGTYPE_DATA) {
    lastDataNumber++;
    dataArea = lastDataNumber;
    }

seg->outSegNum  = out->segNum;
seg->baseOffset = out->length;

/* Seed SegDisp ($87) anchor for any EQU/GEQU expressions evaluated
 * during MeasureBody. Without this, pass-1 evaluation of an EQU whose
 * RHS is `SegDisp N` yields N (input-local) instead of baseOffset+N,
 * leaving the EQU symbol with a value short by baseOffset. Pass2Seg
 * re-seeds this for the same reason before its own body walk. */
exprSegStartPc = seg->baseOffset;

/* Register segment name as a symbol (always define; may already exist
 * from SymRequest before this segment was processed). Propagate the
 * segment's PRIVATE kind bit into the symbol so +S output prints `P`
 * for segment-name symbols in private segments (e.g. CalcBankCH), as
 * stock does. Segment-name symbols are GLOBAL in stock's rule set,
 * with isData=1 only for DATA-kind segs — so the +S listing records
 * dataArea = (segType==DATA ? lastDataNumber : 0). */
if (seg->segName[0]) {
    int segFlags = SYM_PASS1_RESOLVED | SYM_IS_SEGMENT | SYM_IS_GLOBAL;
    if (seg->segkind & SEGKIND_PRIVATE)
        segFlags |= SEGKIND_PRIVATE;
    SymDefine(seg->segName, seg->baseOffset, out->segNum, segFlags);
    SymAddListEntry(seg->segName, seg->baseOffset, out->segNum,
                    dataArea, segFlags);
    }

measured = MeasureBody(inf->fp, seg, dataArea);
if (measured < 0) return 0;

/* RESSPC: trailing reserved-space bytes the input declared. They're
 * part of the input's memory image but not its body on disk. When
 * merging input segments into an output segment, stock iix link
 * inlines them as zero bytes so the merged body contains the full
 * memory image (output RESSPC stays 0). Match that. */
measured += seg->resspc;

out->length += measured;

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

/* Check segment name itself (segment names become symbols). SymFind
 * is case-insensitive so we can pass segName verbatim. */
if (seg->segName[0]) {
    sym = SymFind(seg->segName);
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
        /* Both flavours can satisfy requests in practice — ORCA/C
         * tends to emit routine entry points as LOCAL and still
         * expects cross-file resolution.  We match symbols solely
         * on the REQUESTED-but-not-RESOLVED bit regardless of which
         * record type defined them. */
        char name[NAME_MAX];
        Symbol *sym;
        OmfReadPString(fp, name, NAME_MAX);
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
        fseek(fp, 4, SEEK_CUR);  /* skip attrs */
        SkipExpr(fp, EXPR_PHASE_COLLECT);
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
        SkipExpr(fp, EXPR_PHASE_COLLECT);
        }
    else if (op == OP_RELEXPR) {
        fgetc(fp);
        fseek(fp, 4, SEEK_CUR);
        SkipExpr(fp, EXPR_PHASE_COLLECT);
        }
    else if (op == OP_ENTRY) {
        /* ENTRY defines a named entry point within the segment.
         * If a requested symbol matches this name, the segment
         * satisfies the request — pull it. */
        char   name[NAME_MAX];
        word   reserved;
        dword  value;
        Symbol *sym;
        OmfReadWord(fp, &reserved);
        OmfReadDword(fp, &value);
        OmfReadPString(fp, name, NAME_MAX);
        sym = SymFind(name);
        if (sym && (sym->flags & SYM_PASS1_REQUESTED) &&
                   !(sym->flags & SYM_PASS1_RESOLVED))
            return 1;
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
   Legacy full-rescan library search

   Used only as a fallback for library files that lack a dictionary
   segment (KIND=$08).  Real ORCA libraries all carry one; this path
   mostly exists to accept hand-built library files.
   ---------------------------------------------------------- */
static void LibrarySearchLegacy(LibFile *lf, int *seqCounter)
{
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
    if (diskLen == 0) { free(seg); break; }

    if (SegDefinesNeededSym(lf->fp, seg)) {
        LibFile *prev;

        inf = (InputFile *)malloc(sizeof(InputFile));
        if (!inf) FatalError("out of memory (LibInputFile)");
        memset(inf, 0, sizeof(InputFile));
        inf->fp      = lf->fp;
        inf->fileNum = ++(*seqCounter);
        strncpy(inf->name, lf->path, PATH_MAX - 1);
        inf->segs    = seg;

        if (!inputFiles) inputFiles = inf;
        else             inputTail->next = inf;
        inputTail = inf;

        prev = currentRequestLib;
        currentRequestLib = lf;
        Pass1Seg(inf, seg);
        currentRequestLib = prev;
        }
    else free(seg);

    segOff += diskLen;
    }

fseek(lf->fp, 0L, SEEK_SET);
}

/* Dictionary-driven pull: fetch one specific segment from lf by its
 * header file offset, add it to inputFiles, and pass-1 it.  While the
 * segment is being processed we set (currentRequestLib, currentRequestFileNum)
 * so any SymRequest calls from within that segment's body get tagged
 * with the MakeLib-internal file that contained the segment — this
 * lets LibrarySearch match private dict entries under iix's scoping
 * rule. */
static void PullLibSegment(LibFile *lf, long segOff, int libFileNum,
                           int *seqCounter)
{
InSeg     *seg;
InputFile *inf;
LibFile   *prevLib;
int        prevFile;

seg = (InSeg *)malloc(sizeof(InSeg));
if (!seg) FatalError("out of memory (LibSeg)");
memset(seg, 0, sizeof(InSeg));

if (!OmfReadHeader(lf->fp, seg, segOff)) {
    LinkError("library dictionary points at invalid segment offset",
              lf->path);
    free(seg);
    return;
    }

inf = (InputFile *)malloc(sizeof(InputFile));
if (!inf) FatalError("out of memory (LibInputFile)");
memset(inf, 0, sizeof(InputFile));
inf->fp      = lf->fp;
inf->fileNum = ++(*seqCounter);
strncpy(inf->name, lf->path, PATH_MAX - 1);
inf->segs    = seg;

if (!inputFiles) inputFiles = inf;
else             inputTail->next = inf;
inputTail = inf;

prevLib  = currentRequestLib;
prevFile = currentRequestFileNum;
currentRequestLib     = lf;
currentRequestFileNum = libFileNum;
Pass1Seg(inf, seg);
currentRequestLib     = prevLib;
currentRequestFileNum = prevFile;
}

/* ----------------------------------------------------------
   LibrarySearch

   Preferred path: consult each library's dictionary segment ($08)
   and pull the exact defining segment for each SYM_PASS1_REQUESTED
   symbol, iterating until no new requests appear.  Libraries without
   a dictionary fall back to LibrarySearchLegacy (full rescan).
   ---------------------------------------------------------- */
void LibrarySearch(void)
{
BOOLEAN    changed;
LibFile   *lf;
Symbol    *sym;
int        i;
static int libFileSeq = 0;

if (!libFiles) return;

/* Eagerly load every library's dictionary; any without one are
 * handled by the legacy full-rescan below. */
for (lf = libFiles; lf; lf = lf->next) LibDictInit(lf);

/* Match stock ORCA/M linker's NextLibrarySeg iteration (seg.asm:644):
 *
 *   for each library (in link-command order):
 *       for each dict entry (in MakeLib-original order):
 *           if that entry's symbol is currently requested but unresolved:
 *               pull the defining segment
 *       if we pulled any segment from this lib, restart its dict scan
 *   if we pulled any segment from any lib, restart the whole outer loop
 *
 * The dict-order iteration is what makes the merged output layout match
 * stock. The previous implementation iterated clinker's symbol HASH
 * table (bucket-then-chain order), which scrambled library-pull order
 * and placed every pulled library segment at a different merged offset
 * than stock — cascading into ~5K cross-linker byte differences in
 * seg 4's library region. See LibDictAtSeq. */
do {
    changed = FALSE;
    for (lf = libFiles; lf; lf = lf->next) {
        BOOLEAN pulled_from_this_lib;
        if (!lf->dictValid) continue;      /* legacy path covers these */
        do {
            pulled_from_this_lib = FALSE;
            for (i = 0; i < lf->numSyms; i++) {
                const LibSymEntry *e = LibDictAtSeq(lf, i);
                if (!e) break;
                sym = SymFind(e->name);
                if (!sym) continue;
                if (!(sym->flags & SYM_PASS1_REQUESTED)) continue;
                /* Skip only if resolved AND globally visible — see note
                 * in the old hash-iteration code about LOCAL/GLOBAL
                 * shadowing in clinker's flat hash table. */
                if ((sym->flags & SYM_PASS1_RESOLVED) &&
                    (sym->flags & SYM_IS_GLOBAL))
                    continue;
                /* Public dict entries always satisfy the request. Private
                 * entries match only when the request came from within
                 * the SAME MakeLib-internal file as the dict entry (iix's
                 * per-file scoping rule). */
                if (e->isPrivate) {
                    if (sym->reqLib != lf)             continue;
                    if (sym->reqFileNum != e->fileNum) continue;
                    }
                PullLibSegment(lf, e->segOffset, e->fileNum, &libFileSeq);
                pulled_from_this_lib = TRUE;
                changed = TRUE;
                }
            } while (pulled_from_this_lib);
        }
    } while (changed);

/* Legacy full-rescan fallback.  Library dictionaries list only a
 * subset of the symbols a library actually exports (typically the
 * well-known entry points); symbols that the ORCA/M compiler emits
 * as LOCAL inside a pulled segment body are not indexed in the dict
 * but still need to satisfy cross-segment references during the
 * link.  The reference linker handles this by walking the raw OMF
 * record stream and filling in the hash table as it goes — we
 * approximate by body-scanning every library until no new segment
 * gets pulled.  This runs after the dict pass so we only sweep
 * libraries when there are truly-unresolved symbols left. */
do {
    BOOLEAN anyPending = FALSE;
    InputFile *before, *p;
    long beforeCount = 0, afterCount;

    for (i = 0; i < SYM_HASH_SIZE && !anyPending; i++)
        for (sym = symHash[i]; sym; sym = sym->next)
            if ((sym->flags & SYM_PASS1_REQUESTED) &&
                !(sym->flags & SYM_PASS1_RESOLVED)) {
                anyPending = TRUE;
                break;
                }
    if (!anyPending) break;

    for (before = inputFiles; before; before = before->next) beforeCount++;
    changed = FALSE;
    for (lf = libFiles; lf; lf = lf->next) {
        if (lf->dictValid) continue;     /* dict covers these */
        if (lf->skipLegacy) continue;    /* legacy OMF v1 — skip */
        LibrarySearchLegacy(lf, &libFileSeq);
        }
    afterCount = 0;
    for (p = inputFiles; p; p = p->next) afterCount++;
    if (afterCount > beforeCount) changed = TRUE;
    } while (changed);
}

