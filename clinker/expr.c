/**************************************************************
*
*  expr.c -- OMF expression evaluator
*
*  Expressions are reverse-Polish stack streams that appear
*  inside EXPR/ZEXPR/BEXPR/RELEXPR/LEXPR records and inside
*  GEQU/EQU operand fields. The stream is a sequence of opcode
*  bytes terminated by $00. See GS/OS Reference appendix F.
*
*  Supported opcodes:
*      $00          end of expression (terminates the walk)
*      $01-$0B      binary/unary math + logical operators
*                   (we act on $01-$06 and $09-$0B;
*                    others fall through as generic two-operand)
*      $80          push current PC
*      $81          push 4-byte constant (follows opcode)
*      $82-$86      push label value (pstring name follows)
*                   ($82 weak, $83 strong, $84-$86 typed refs)
*      $87          push 4-byte segment displacement (follows);
*                   result needs relocation
*
*  Two calling contexts:
*      EXPR_PHASE_COLLECT (1) — pass 1. Unresolved symbol references
*          are recorded via SymRequest so LibrarySearch can find
*          segments that define them. Unresolved pushes 0.
*      EXPR_PHASE_RESOLVE (2) — pass 2. An unresolved STRONG
*          reference is a link-time error ("undefined symbol");
*          unresolved WEAK pushes 0 without complaint.
*
*  The phase is the only behavioural difference between the two
*  historical evaluators. Everything else — arithmetic, stack
*  mechanics, relocation flagging — is identical.
*
**************************************************************/

#pragma keep "expr"
#pragma optimize 9

#include "clinker.h"

#define EXPR_STACK_DEPTH 32

int EvalExpr(FILE *fp, long pc, long *result, int *segOut, int *fileOut,
             BOOLEAN *needsReloc, int phase,
             int *shiftOut, long *unshiftedOut)
{
long stack[EXPR_STACK_DEPTH];
int     top = 0;
int     op;
int     shiftCount  = 0;
long    unshiftedVal = 0;
BOOLEAN shifted      = FALSE;

if (result)       *result       = 0;
if (segOut)       *segOut       = 0;
if (fileOut)      *fileOut      = 0;
if (needsReloc)   *needsReloc   = FALSE;
if (shiftOut)     *shiftOut     = 0;
if (unshiftedOut) *unshiftedOut = 0;

for (;;) {
    op = fgetc(fp);
    if (op == EOF || op == EXPR_END) break;

    /* --- Binary operators (pop 2, push 1) ----------------------------- */
    if ((op >= 0x01 && op <= 0x05) ||
        (op == 0x07) ||
        (op >= 0x08 && op <= 0x0A) ||
        (op >= 0x0C && op <= 0x14)) {
        long b = (top > 0) ? stack[--top] : 0;
        long a = (top > 0) ? stack[--top] : 0;
        long r = 0;
        switch (op) {
            case 0x01: r = a + b;  break;           /* Add */
            case 0x02: r = a - b;  break;           /* Sub */
            case 0x03: r = a * b;  break;           /* Mul */
            case 0x04: r = b ? a / b : 0; break;    /* Div */
            case 0x05: r = b ? a % b : 0; break;    /* Mod */
            case 0x07: {                            /* Shift: a by b */
                /* b > 0 = left shift; b < 0 = right shift. */
                unshiftedVal = a;
                shiftCount   = (int)b;
                shifted      = TRUE;
                if (b >=  32) r = 0;
                else if (b >=  0)  r = a << (int)b;
                else if (b <= -32) r = 0;
                else               r = (long)((unsigned long)a >> (int)-b);
                break;
                }
            case 0x08: r = (a && b) ? 1 : 0; break; /* And (logical) */
            case 0x09: r = (a || b) ? 1 : 0; break; /* Or  (logical) */
            case 0x0A: r = ((!!a) ^ (!!b)) ? 1 : 0; break; /* Eor (logical) */
            case 0x0C: r = (a <= b) ? 1 : 0; break; /* LE */
            case 0x0D: r = (a >= b) ? 1 : 0; break; /* GE */
            case 0x0E: r = (a != b) ? 1 : 0; break; /* NE */
            case 0x0F: r = (a <  b) ? 1 : 0; break; /* LT */
            case 0x10: r = (a >  b) ? 1 : 0; break; /* GT */
            case 0x11: r = (a == b) ? 1 : 0; break; /* EQ */
            case 0x12: r = a & b;  break;           /* BAnd */
            case 0x13: r = a | b;  break;           /* BOr */
            case 0x14: r = a ^ b;  break;           /* BEor */
            }
        if (top < EXPR_STACK_DEPTH) stack[top++] = r;
        continue;
        }

    /* --- Unary operators (pop 1, push 1) ------------------------------ */
    if (op == 0x06 || op == 0x0B || op == 0x15) {
        long a = (top > 0) ? stack[--top] : 0;
        long r = 0;
        switch (op) {
            case 0x06: r = -a;         break;       /* UMinus */
            case 0x0B: r = a ? 0 : 1;  break;       /* Not (logical) */
            case 0x15: r = ~a;         break;       /* BNot (bitwise) */
            }
        if (top < EXPR_STACK_DEPTH) stack[top++] = r;
        continue;
        }

    /* --- Pushes ------------------------------------------------------- */
    if (op == EXPR_PC) {
        if (top < EXPR_STACK_DEPTH) stack[top++] = pc;
        continue;
        }

    if (op == EXPR_CONST) {
        dword v;
        OmfReadDword(fp, &v);
        if (top < EXPR_STACK_DEPTH) stack[top++] = (long)v;
        continue;
        }

    if (op == EXPR_SEGDISP) {
        dword v;
        OmfReadDword(fp, &v);
        if (top < EXPR_STACK_DEPTH) stack[top++] = (long)v;
        if (needsReloc) *needsReloc = TRUE;
        continue;
        }

    if (op == EXPR_STRONG || op == EXPR_WEAK ||
        op == 0x84 || op == 0x85 || op == 0x86) {
        char    name[NAME_MAX];
        Symbol *sym;
        char   *p;

        OmfReadPString(fp, name, NAME_MAX);
        for (p = name; *p; p++) *p = (char)toupper(*p);

        sym = SymFind(name);
        if (!sym || !(sym->flags & SYM_PASS1_RESOLVED)) {
            /* A WEAK reference ($82) by spec resolves to 0 if unknown
             * and MUST NOT force library search — otherwise we pull
             * library segments that the program would otherwise omit.
             * STRONG ($83) and the attribute references ($84-$86)
             * all require the symbol to exist, so they do trigger
             * library search in phase 1 and error in phase 2. */
            if (op != EXPR_WEAK) {
                if (phase == EXPR_PHASE_COLLECT) {
                    SymRequest(name, 1);
                    }
                else if (op == EXPR_STRONG) {
                    LinkError("undefined symbol", name);
                    }
                }
            if (top < EXPR_STACK_DEPTH) stack[top++] = 0;
            }
        else {
            if (top < EXPR_STACK_DEPTH) stack[top++] = sym->value;
            if (!(sym->flags & SYM_IS_CONSTANT)) {
                if (segOut)     *segOut     = sym->segNum;
                if (needsReloc) *needsReloc = TRUE;
                }
            }
        continue;
        }

    /* Unknown opcode: stop walking rather than skid on.  A well-formed
     * stream always terminates with $00, so reaching here means corrupt
     * input or a missing opcode handler. */
    break;
    }

if (result)       *result       = (top > 0) ? stack[top - 1] : 0;
if (shiftOut)     *shiftOut     = shifted ? shiftCount : 0;
if (unshiftedOut) *unshiftedOut = shifted ? unshiftedVal
                                          : ((top > 0) ? stack[top - 1] : 0);
return 1;
}

/* Convenience wrapper: walk an expression stream purely for its side
 * effects (marking symbols for library search in phase 1). Discards the
 * computed value.  Used by pass-1 callers that need to scan past an
 * expression without caring about its result. */
void SkipExpr(FILE *fp, int phase)
{
long  result;
int   segOut, fileOut;
BOOLEAN needsReloc;
EvalExpr(fp, 0L, &result, &segOut, &fileOut, &needsReloc, phase, NULL, NULL);
}
