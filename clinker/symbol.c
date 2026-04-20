/**************************************************************
*
*  symbol.c — Symbol table for clinker
*
*  Hash table keyed by uppercase symbol name.  Symbols are
*  never deleted; they are only defined or requested.
*
**************************************************************/

#pragma keep "symbol"
#pragma optimize 9

#include "clinker.h"

segment "SYMBOL";

Symbol **symHash = NULL;

/* +S listing store — append-only. See clinker.h SymListEntry comment. */
SymListEntry *symList = NULL;
static SymListEntry *symListTail = NULL;

/* Allocate and zero the hash table. Called once from main before
 * any symbol-table operation. Moving this ~4KB allocation out of
 * ~_ROOT and onto the heap keeps the data bank under 64KB. */
void SymInit(void)
{
int i;
if (symHash) return;
symHash = (Symbol **)malloc(SYM_HASH_SIZE * sizeof(Symbol *));
if (!symHash) FatalError("out of memory (symHash)");
for (i = 0; i < SYM_HASH_SIZE; i++)
    symHash[i] = NULL;
}

/* Set by pass1's library-search path while processing a lib segment, so
 * SymRequest can tag new requests with their originating library and
 * MakeLib-internal file number. */
LibFile *currentRequestLib     = NULL;
int      currentRequestFileNum = 0;

unsigned int SymHash(const char *name)
{
unsigned int h = 0;
while (*name)
    h = h * 31 + (unsigned char)toupper(*name++);
return h % SYM_HASH_SIZE;
}

/* Copy `src` into `dst` uppercased, NUL-terminated, capped at
 * NAME_MAX-1 bytes. Used to derive the hash/compare key from any
 * caller-supplied (possibly mixed-case) symbol name. */
static void UcaseCopy(char *dst, const char *src)
{
int i;
for (i = 0; i < NAME_MAX - 1 && src[i]; i++)
    dst[i] = (char)toupper((int)src[i]);
dst[i] = 0;
}

Symbol *SymFind(const char *name)
{
Symbol *s;
char upper[NAME_MAX];
unsigned int h;

/* Uppercase the caller's name so we match stored-uppercase `name`
 * entries regardless of caller case. Stock ORCA/M is case-insensitive;
 * clinker has been de facto insensitive only because every caller
 * uppercased before lookup. Centralising the toupper here lets callers
 * hand us original-case names (needed for SymDefine's displayName). */
UcaseCopy(upper, name);
h = SymHash(upper);
for (s = symHash[h]; s; s = s->next)
    if (strcmp(s->name, upper) == 0)
        return s;
return NULL;
}

Symbol *SymDefine(const char *name, long value, int segNum, int flags)
{
Symbol *s;
unsigned int h;
char upper[NAME_MAX];

UcaseCopy(upper, name);
s = SymFind(upper);
if (!s) {
    s = (Symbol *)malloc(sizeof(Symbol));
    if (!s) FatalError("out of memory (symbol table)");
    memset(s, 0, sizeof(Symbol));
    strncpy(s->name,        upper, NAME_MAX - 1);
    /* Preserve the caller's original case for +S output. First-define
     * wins: subsequent redefines (LOCAL overwritten by GLOBAL or vice
     * versa) keep the name as the first caller wrote it — which is
     * what a human grepping kernel listings expects. */
    strncpy(s->displayName, name,  NAME_MAX - 1);
    h = SymHash(upper);
    s->next = symHash[h];
    symHash[h] = s;
    }
/* Precedence: a GLOBAL definition wins over a LOCAL one. A later
 * OP_LOCAL with the same name as an earlier OP_GLOBAL must not
 * downgrade the symbol — ORCA/C emits many file-internal LOCAL
 * helpers whose names collide with other files' GLOBAL exports, and
 * clinker's flat (non-per-file) symbol table would otherwise resolve
 * cross-file references to the wrong segment. The first GLOBAL
 * definition sticks; subsequent LOCAL defines are recorded as a
 * flag-only merge (so request/resolution state is preserved) but do
 * not overwrite value/segNum. */
if ((s->flags & SYM_IS_GLOBAL) && !(flags & SYM_IS_GLOBAL)) {
    s->flags |= flags;
    return s;
    }
s->value  = value;
s->segNum = segNum;
s->flags |= flags;
return s;
}

Symbol *SymDefineData(const char *name, long value, int segNum,
                      int flags, int dataArea)
{
Symbol *s = SymDefine(name, value, segNum, flags);
/* Only record dataArea if this is the definitive define (not a flag-
 * only merge where GLOBAL overrides a LOCAL redefine). Using the same
 * "was this redefine accepted" heuristic as SymDefine: if s->value
 * matches `value`, we took the redefine. */
if (s && s->value == value && s->segNum == segNum)
    s->dataArea = dataArea;
return s;
}

void SymAddListEntry(const char *displayName, long value, int segNum,
                     int dataArea, int flags)
{
SymListEntry *e;

e = (SymListEntry *)malloc(sizeof(SymListEntry));
if (!e) FatalError("out of memory (SymListEntry)");

strncpy(e->displayName, displayName, NAME_MAX - 1);
e->displayName[NAME_MAX - 1] = 0;
e->value    = value;
e->segNum   = segNum;
e->dataArea = dataArea;
e->flags    = flags;
e->next     = NULL;

if (!symList) {
    symList = e;
} else {
    symListTail->next = e;
    }
symListTail = e;
}

void SymRequest(const char *name, int pass)
{
Symbol *s;
char upper[NAME_MAX];

UcaseCopy(upper, name);
s = SymFind(upper);
if (!s) {
    s = (Symbol *)malloc(sizeof(Symbol));
    if (!s) FatalError("out of memory (symbol table)");
    memset(s, 0, sizeof(Symbol));
    strncpy(s->name,        upper, NAME_MAX - 1);
    strncpy(s->displayName, name,  NAME_MAX - 1);
    unsigned int h = SymHash(upper);
    s->next = symHash[h];
    symHash[h] = s;
    }
if (pass == 1)
    s->flags |= SYM_PASS1_REQUESTED;
else
    s->flags |= SYM_PASS2_REQUESTED;

/* Record the originating (library, MakeLib-file) on first internal
 * request so LibrarySearch can match private dict entries against
 * the same scope.  First-wins: once set, the scope doesn't change,
 * which matches iix's per-file symFile tagging. */
if (!s->reqLib && currentRequestLib) {
    s->reqLib     = currentRequestLib;
    s->reqFileNum = currentRequestFileNum;
    }
}

/* qsort comparator: by displayName so the +S listing sorts in the
 * same mixed-case order stock's symAlpha chain produces. */
static int CmpListByName(const void *a, const void *b)
{
SymListEntry *const *sa = (SymListEntry *const *)a;
SymListEntry *const *sb = (SymListEntry *const *)b;
return strcmp((*sa)->displayName, (*sb)->displayName);
}

/* SymDump — +S flag handler. Matches stock PrintSymbols (symbol.asm:1296)
 * output format so clinker and stock produce the same on-screen listing:
 *
 *   Global symbol table:
 *
 *   VVVVVVVV G SS DD name<pad to col2>VVVVVVVV G SS DD name<CR>
 *   ...
 *
 * where VVVVVVVV is the 32-bit symbol value (hex), G or P is the
 * global/private flag, SS is the post-ExpressLoad segment number, DD
 * is the data-area number (clinker doesn't track data areas so this
 * is always 00), and the symbol name is padded with spaces so the next
 * column starts 27 chars after the last byte of the name — or just one
 * space if the name itself is >= 27 chars. Two symbols per line; a
 * final CR closes the last line if it ended in column 1.
 *
 * Segment-name symbols (SYM_IS_SEGMENT) are hidden — those are printed
 * in the separate `+L` segment listing. Unresolved requests are hidden
 * too so the listing shows only symbols that actually have a definition. */
void SymDump(void)
{
SymListEntry **arr;
SymListEntry  *e;
int            i, n, count;
int            col2 = 0;

count = 0;
for (e = symList; e; e = e->next) count++;
if (count == 0) return;

arr = (SymListEntry **)malloc((size_t)count * sizeof(SymListEntry *));
if (!arr) { FatalError("out of memory (SymDump)"); return; }

n = 0;
for (e = symList; e; e = e->next) arr[n++] = e;

qsort(arr, (size_t)n, sizeof(SymListEntry *), CmpListByName);

puts("");
puts("Global symbol table:");
puts("");

for (i = 0; i < n; i++) {
    /* Post-ExpressLoad remap: an ExpressLoad output file has the
     * ExpressLoad stub at seg 1, so every other segment's on-disk
     * number is +1 relative to what pass1/2 track. Inline the same
     * adjustment gsplus.c:PostExprSegNum does. */
    int segOut = opt_express ? (arr[i]->segNum + 1) : arr[i]->segNum;
    char flag  = (arr[i]->flags & SEGKIND_PRIVATE) ? 'P' : 'G';
    const char *nm = arr[i]->displayName;
    int nameLen = (int)strlen(nm);
    int pad;

    printf("%08lX %c %02X %02X %s",
           arr[i]->value, flag, segOut, arr[i]->dataArea, nm);

    if (col2) {
        col2 = 0;
        printf("\n");
        }
    else {
        pad = 27 - nameLen;
        if (pad < 1) pad = 1;
        while (pad--) printf(" ");
        col2 = 1;
        }
    }
if (col2) printf("\n");

free(arr);
}
