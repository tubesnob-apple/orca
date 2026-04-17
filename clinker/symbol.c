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

Symbol *symHash[SYM_HASH_SIZE];

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

Symbol *SymFind(const char *name)
{
Symbol *s;
unsigned int h = SymHash(name);
for (s = symHash[h]; s; s = s->next)
    if (strcmp(s->name, name) == 0)
        return s;
return NULL;
}

Symbol *SymDefine(const char *name, long value, int segNum, int flags)
{
Symbol *s;
unsigned int h;

s = SymFind(name);
if (!s) {
    s = (Symbol *)malloc(sizeof(Symbol));
    if (!s) FatalError("out of memory (symbol table)");
    memset(s, 0, sizeof(Symbol));
    strncpy(s->name, name, NAME_MAX - 1);
    h = SymHash(name);
    s->next = symHash[h];
    symHash[h] = s;
    }
s->value  = value;
s->segNum = segNum;
s->flags |= flags;
return s;
}

void SymRequest(const char *name, int pass)
{
Symbol *s = SymFind(name);
if (!s) {
    s = (Symbol *)malloc(sizeof(Symbol));
    if (!s) FatalError("out of memory (symbol table)");
    memset(s, 0, sizeof(Symbol));
    strncpy(s->name, name, NAME_MAX - 1);
    unsigned int h = SymHash(name);
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

void SymDump(void)
{
int i;
Symbol *s;
puts("Symbol Table:");
for (i = 0; i < SYM_HASH_SIZE; i++)
    for (s = symHash[i]; s; s = s->next) {
        if (s->flags & SYM_IS_SEGMENT) continue;
        printf("  %-30s seg=%d val=%08lX flags=%02X\n",
               s->name, s->segNum, s->value, s->flags);
        }
}
