/**************************************************************
*
*  clinker.h
*
*  ORCA/C reimplementation of the ORCA/M linker.
*  Common types, constants, and forward declarations.
*
**************************************************************/

#ifndef CLINKER_H
#define CLINKER_H

#pragma lint 0
/* Large memory model: compiler emits JSL + 24-bit long-absolute
 * addressing for every call and data reference. Required so functions
 * placed in a separate load segment (via `segment "NAME";`) can be
 * called from other segments when the loader places them in different
 * banks. Slower for intra-bank calls, but we don't care about
 * per-instruction performance here.
 *
 * NOTE: repo's Docs/pragma-reference.md has 0/1 backwards. Source of
 * truth is Scanner.pas:3670 `smallMemoryModel := expressionValue = 0`
 * with default `smallMemoryModel := true` (CGI.pas:873) — so 0 = small
 * (default), 1 = large. */
#pragma memorymodel 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---------------------------------------------------------- */
/* Basic types                                                */
/* ---------------------------------------------------------- */

/* Match ORCACDefs/types.h so clinker.c can pull in <shell.h> (which
 * transitively includes types.h) without a duplicate-typedef conflict. */
#ifndef __TYPES__
typedef unsigned int BOOLEAN;
#endif
#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif

typedef unsigned char  byte;
typedef unsigned short word;
typedef unsigned long  dword;

/* ---------------------------------------------------------- */
/* Limits                                                     */
/* ---------------------------------------------------------- */

#define SYM_HASH_SIZE  1021   /* prime hash table size */
#define NAME_MAX       32      /* max symbol/segment name bytes */
#define PATH_MAX       256     /* max file path */

/* ---------------------------------------------------------- */
/* OMF record opcodes                                         */
/* ---------------------------------------------------------- */

#define OP_END        0x00
#define OP_ALIGN      0xE0
#define OP_ORG        0xE1
#define OP_RELOC      0xE2
#define OP_INTERSEG   0xE3
#define OP_USING      0xE4
#define OP_STRONG     0xE5
#define OP_GLOBAL     0xE6
#define OP_GEQU       0xE7
#define OP_MEM        0xE8
#define OP_EXPR       0xEB
#define OP_ZEXPR      0xEC
#define OP_BEXPR      0xED
#define OP_RELEXPR    0xEE
#define OP_LOCAL      0xEF
#define OP_EQU        0xF0
#define OP_DS         0xF1
#define OP_LCONST     0xF2
#define OP_LEXPR      0xF3
#define OP_ENTRY      0xF4
#define OP_CRELOC     0xF5
#define OP_CINTERSEG  0xF6
#define OP_SUPER      0xF7

/* Expression-stream opcodes used by name. Arithmetic opcodes (0x01–0x15)
 * are decoded by raw number inside expr.c — no #define for them. */
#define EXPR_END      0x00
#define EXPR_PC       0x80
#define EXPR_CONST    0x81
#define EXPR_WEAK     0x82
#define EXPR_STRONG   0x83
#define EXPR_SEGDISP  0x87

/* Expression evaluator phases (see expr.c) */
#define EXPR_PHASE_COLLECT 1  /* pass 1: mark unresolved for lib search */
#define EXPR_PHASE_RESOLVE 2  /* pass 2: error on unresolved STRONG     */

/* ---------------------------------------------------------- */
/* OMF segment type codes (low 5 bits)                        */
/* ---------------------------------------------------------- */

#define SEGTYPE_CODE    0x00
#define SEGTYPE_DATA    0x01
#define SEGTYPE_EXPRESS 0x8001 /* express-load dynamic segment */

/* Full type word flags */
#define SEGKIND_PRIVATE 0x4000

/* ---------------------------------------------------------- */
/* Symbol flags                                               */
/* ---------------------------------------------------------- */

#define SYM_PASS1_RESOLVED  0x01
#define SYM_PASS1_REQUESTED 0x04
#define SYM_PASS2_REQUESTED 0x08
#define SYM_IS_CONSTANT     0x10
#define SYM_IS_SEGMENT      0x40
#define SYM_IS_GLOBAL       0x80  /* OP_GLOBAL/GEQU/ENTRY/SEGNAME defined it */

/* ---------------------------------------------------------- */
/* Relocation record                                          */
/* ---------------------------------------------------------- */

typedef struct RelocRec {
    long            pc;       /* offset within output segment */
    byte            patchLen; /* bytes to patch (1–4) */
    byte            shift;    /* right-shift before storing */
    long            value;    /* addend */
    int             type;     /* 0=RELOC, 1=INTERSEG */
    int             segNum;   /* INTERSEG: target segment number */
    int             fileNum;  /* INTERSEG: target file number */
    struct RelocRec *next;
} RelocRec;

/* ---------------------------------------------------------- */
/* Output segment                                             */
/* ---------------------------------------------------------- */

typedef struct OutSeg {
    char  loadName[NAME_MAX]; /* load (output file) name */
    char  segName[NAME_MAX];  /* segment name */
    int   segNum;             /* output segment number */
    word  segType;            /* low 5 bits of KIND (enum-style type) */
    word  kind;                /* full 16-bit KIND word: type + flags */
    long  length;             /* total byte length of combined data */
    long  org;                /* origin address (usually 0) */
    long  banksize;           /* bank size (usually $10000) */
    long  align;              /* alignment factor */

    byte *data;               /* assembled data bytes */
    long  dataLen;            /* bytes written to data[] */
    long  dataAlloc;          /* allocated size of data[] */

    RelocRec *relocHead;      /* first reloc record */
    RelocRec *relocTail;      /* last reloc record (for O(1) append) */

    struct OutSeg *next;
} OutSeg;

/* ---------------------------------------------------------- */
/* Symbol table entry                                         */
/* ---------------------------------------------------------- */

struct LibFile;             /* forward tag — full struct below */

typedef struct Symbol {
    char name[NAME_MAX];
    long value;
    int  segNum;
    int  flags;
    struct LibFile *reqLib;   /* first library that requested this symbol */
    int   reqFileNum;         /* MakeLib-internal file # of the requesting
                               * lib segment (0 = external / no scope).
                               * Paired with reqLib to match iix's private-
                               * dict scoping: two symbols match when both
                               * share the same (reqLib, reqFileNum). */
    struct Symbol *next;      /* hash chain */
} Symbol;

/* ---------------------------------------------------------- */
/* Input segment: one OMF segment from an input file          */
/* ---------------------------------------------------------- */

typedef struct InSeg {
    char  loadName[NAME_MAX];
    char  segName[NAME_MAX];
    word  segType;
    long  banksize;
    long  org;
    long  align;
    word  segkind;        /* raw kind word from header */
    long  resspc;         /* RESSPC: trailing zero-fill bytes (inlined at merge) */
    long  fileBodyOffset; /* file offset of first body byte */
    long  bodyLen;        /* length of body in the input file */
    long  measuredLen;    /* length computed by pass-1 body walk */
    int   outSegNum;      /* which output segment this maps to */
    long  baseOffset;     /* offset of this input seg within output seg */
    int   inputFileNum;   /* which input file */
    struct InSeg *next;
} InSeg;

/* ---------------------------------------------------------- */
/* Input file                                                 */
/* ---------------------------------------------------------- */

typedef struct InputFile {
    char  name[PATH_MAX];
    FILE *fp;
    int   fileNum;        /* 1-based file index */
    InSeg *segs;          /* linked list of segments in this file */
    struct InputFile *next;
} InputFile;

/* ---------------------------------------------------------- */
/* Linker state (globals)                                     */
/* ---------------------------------------------------------- */

/* CLI options */
extern BOOLEAN opt_list;       /* +L: list segment info */
extern BOOLEAN opt_symbols;    /* +S: list symbol table */
extern BOOLEAN opt_express;    /* +X: express load (default on, -X turns off) */
extern BOOLEAN opt_memory;     /* +M: memory-only link */
extern BOOLEAN opt_compact;    /* -C: disable compact records (default compact) */
extern BOOLEAN opt_progress;   /* -P: disable progress (default on) */
extern BOOLEAN opt_gsplus;     /* gsplusSymbols shell var set */
extern char   *keepName;   /* keep= file name (malloc'd PATH_MAX in main) */
extern char   *baseName;   /* basename of keepName (malloc'd PATH_MAX) */

/* Parsed entry from a library dictionary ($08) segment.
 * See GS/OS Reference Appendix F for the on-disk format. */
typedef struct LibSymEntry {
    char    name[NAME_MAX];  /* uppercase symbol name */
    long    segOffset;       /* file offset of the defining segment's header */
    int     fileNum;         /* MakeLib-internal file number (1..N) */
    BOOLEAN isPrivate;       /* private_flag = 1 in the dictionary */
} LibSymEntry;

/* Library file (for deferred symbol extraction) */
typedef struct LibFile {
    FILE           *fp;
    char            path[PATH_MAX];

    /* Library-dictionary cache. Lazily initialised on first lookup.
     * dictLoaded is set unconditionally so we don't re-scan on miss;
     * dictValid is set only if a KIND=$08 segment was actually parsed. */
    LibSymEntry    *syms;
    int             numSyms;
    BOOLEAN         dictLoaded;
    BOOLEAN         dictValid;
    BOOLEAN         skipLegacy;  /* not OMF v2 — can't scan segment-by-segment */

    struct LibFile *next;
} LibFile;

/* Global state */
extern int       numErrors;
extern InputFile *inputFiles;   /* linked list of input files */
extern InputFile *inputTail;    /* last entry in inputFiles */
extern LibFile   *libFiles;     /* library files for symbol extraction */
extern OutSeg    *outSegs;      /* linked list of output segments */
extern int        numOutSegs;
extern Symbol   **symHash;      /* allocated by SymInit() */
void SymInit(void);

/* GSplus signature */
extern dword     sfSig;         /* 32-bit link signature */

/* ---------------------------------------------------------- */
/* Function prototypes                                        */
/* ---------------------------------------------------------- */

/* symbol.c */
Symbol *SymFind(const char *name);
Symbol *SymDefine(const char *name, long value, int segNum, int flags);
void    SymRequest(const char *name, int pass);
void    SymDump(void);
unsigned int SymHash(const char *name);

/* omf.c */
int     OmfReadByte(FILE *fp);
int     OmfReadWord(FILE *fp, word *out);
int     OmfReadDword(FILE *fp, dword *out);
int     OmfReadPString(FILE *fp, char *out, int maxLen);
int     OmfReadHeader(FILE *fp, InSeg *seg, long startOff);
void    OmfWriteByte(FILE *fp, int v);
void    OmfWriteWord(FILE *fp, int v);
void    OmfWriteDword(FILE *fp, long v);
void    OmfWritePString(FILE *fp, const char *s);
long    OmfWriteSegHeader(FILE *fp, OutSeg *seg, long bodyLen, int segNum,
                          const char *loadName);
long    OmfRelocSize(const RelocRec *r);
long    OmfSuperBytes(const OutSeg *seg);
void    OmfWriteReloc(FILE *fp, const RelocRec *r);
void    OmfPrepareSuper(OutSeg *seg);
void    OmfWriteSuper(FILE *fp, OutSeg *seg);

/* pass1.c */
int  Pass1(void);
int  Pass1Seg(InputFile *inf, InSeg *seg);
long MeasureBody(FILE *fp, InSeg *seg);
void LibrarySearch(void);

/* libdict.c — library dictionary cache + lookup */
/* Input-file helpers (defined in clinker.c, called from linfo.c) */
BOOLEAN AddInputFile(const char *arg);
BOOLEAN AddLibFile(const char *path);
void    SetBaseName(void);

/* iix-link LangInfo path (defined in linfo.c, called from clinker.c) */
BOOLEAN LoadFromLInfo(void);
void    ReportToShell(void);

void LibDictInit(LibFile *lf);
/* LibDictFind: public-only name lookup (private dict entries are rejected). */
long LibDictFind(LibFile *lf, const char *name);  /* -1 on miss */
/* LibDictLookup: richer lookup — returns the LEFTMOST matching entry
 * (or NULL on miss). Caller checks isPrivate / fileNum, and iterates
 * same-name duplicates via LibDictNext. Duplicates occur because
 * MakeLib emits one dict entry per (file, symbol) pair, and compiler-
 * generated private names like `~0001buf` recur across source files. */
const LibSymEntry *LibDictLookup(LibFile *lf, const char *name);
const LibSymEntry *LibDictNext(LibFile *lf, const LibSymEntry *cur);

/* Set by pass1 while it processes a library-sourced segment.  SymRequest
 * reads these to tag new symbol requests with their originating library
 * and MakeLib-internal file number, which LibrarySearch then uses to
 * scope private-dict matches exactly like iix link does. */
extern LibFile *currentRequestLib;
extern int      currentRequestFileNum;

/* pass2.c */
int  Pass2(void);
int  Pass2Seg(InputFile *inf, InSeg *seg, OutSeg *out);

/* expr.c — unified expression walker used by both passes.
 * phase is EXPR_PHASE_COLLECT (pass 1) or EXPR_PHASE_RESOLVE (pass 2).
 * Any of the output pointers may be NULL for "don't care".
 *
 * Pass2Seg must set exprSegStartPc = seg->baseOffset before walking a
 * segment so the SegDisp ($87) opcode can push (startpc + operand) to
 * match stock exp.asm semantics. */
extern long exprSegStartPc;
int  EvalExpr(FILE *fp, long pc, long *result, int *segOut, int *fileOut,
              BOOLEAN *needsReloc, int phase,
              int *shiftOut, long *unshiftedOut);
void SkipExpr(FILE *fp, int phase);

/* clinker.c (helpers used by other modules) */
void   LinkError(const char *msg, const char *name);
void   FatalError(const char *msg);
OutSeg *FindOrCreateOutSeg(const char *loadName, const char *segName,
                            word kind);
OutSeg *OutSegByNum(int n);
void   AppendReloc(OutSeg *seg, long pc, byte patchLen, byte shift,
                   long value, int type, int segNum, int fileNum);
void   GrowData(OutSeg *seg, long need);
void   EmitData(OutSeg *seg, const byte *src, long len);
void   EmitZero(OutSeg *seg, long len);
void   InitGsplusSymbols(void);
void   WriteSymbolFile(void);
void   WriteSym65File(void);

/* out.c */
int    WriteOutput(void);

#endif /* CLINKER_H */
