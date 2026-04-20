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

/* Expression evaluator in its own EXPR load segment. Hot — called from
 * both pass1 and pass2 — but self-contained (only external dep is
 * OmfReadDword/OmfReadPString + symbol table), so cross-segment calls
 * don't ripple further. Large model (clinker.h) handles the JSLs. */
segment "EXPR";

#define EXPR_STACK_DEPTH 32

/* Set by Pass2Seg before walking a segment — mirrors stock exp.asm's
 * `startpc`. Fixed for the entire segment, used by SegDisp ($87). */
long exprSegStartPc = 0;

int EvalExpr(FILE *fp, long pc, long *result,
             int *segOut, int *fileOut,
             BOOLEAN *needsReloc, int phase, int fileNum,
             int *shiftOut, long *unshiftedOut)
{
long stack[EXPR_STACK_DEPTH];
int     top = 0;
int     op;
int     shiftCount  = 0;
long    unshiftedVal = 0;
BOOLEAN shifted      = FALSE;
/* Reloc stack-of-bits mirroring the value stack. Bit 0 is the
 * TOP-of-stack's reloc flag (so shifts are by constants, minimising
 * 65816 code size). Needed so `rel - rel` in Sub cancels (stock
 * exp.asm:1497); without this `(seg+N) - segLabel` emits a bogus
 * RELOC that the GS/OS loader double-base-adds at load time. */
unsigned long relocBits = 0;
int lastSeg = 0;

if (result)       *result       = 0;
if (segOut)       *segOut       = 0;
if (fileOut)      *fileOut      = 0;
if (needsReloc)   *needsReloc   = FALSE;
if (shiftOut)     *shiftOut     = 0;
if (unshiftedOut) *unshiftedOut = 0;

for (;;) {
    op = fgetc(fp);
    if (op == EOF || op == EXPR_END) break;

    /* --- Arithmetic (pop 2-or-1, push 1) -------------------------------
     * Unary ops ($06 UMinus, $0B Not, $15 BNot) pop 1 only. */
    if (op >= 0x01 && op <= 0x15) {
        int  isUnary = (op == 0x06 || op == 0x0B || op == 0x15);
        long b = isUnary ? 0 : ((top > 0) ? stack[--top] : 0);
        unsigned int bR = isUnary ? 0 : (relocBits & 1);
        if (!isUnary) relocBits >>= 1;
        long a = (top > 0) ? stack[--top] : 0;
        unsigned int aR = relocBits & 1;
        relocBits >>= 1;
        unsigned int rR = aR | bR;
        long r = 0;
        switch (op) {
            case 0x01: r = a + b; break;
            case 0x02: r = a - b; if (aR && bR) rR = 0; break;
            case 0x03: r = a * b; rR = 0; break;
            case 0x04: r = b ? a / b : 0; rR = 0; break;
            case 0x05: r = b ? a % b : 0; rR = 0; break;
            case 0x06: r = -a; rR = 0; break;
            case 0x07:
                unshiftedVal = a; shiftCount = (int)b; shifted = TRUE;
                if      (b >=  32) r = 0;
                else if (b >=   0) r = a << (int)b;
                else if (b <= -32) r = 0;
                else               r = (long)((unsigned long)a >> (int)-b);
                rR = aR;
                break;
            case 0x08: r = (a && b) ? 1 : 0; rR = 0; break;
            case 0x09: r = (a || b) ? 1 : 0; rR = 0; break;
            case 0x0A: r = ((!!a) ^ (!!b)) ? 1 : 0; rR = 0; break;
            case 0x0B: r = a ? 0 : 1; rR = 0; break;
            case 0x0C: r = (a <= b) ? 1 : 0; rR = 0; break;
            case 0x0D: r = (a >= b) ? 1 : 0; rR = 0; break;
            case 0x0E: r = (a != b) ? 1 : 0; rR = 0; break;
            case 0x0F: r = (a <  b) ? 1 : 0; rR = 0; break;
            case 0x10: r = (a >  b) ? 1 : 0; rR = 0; break;
            case 0x11: r = (a == b) ? 1 : 0; rR = 0; break;
            case 0x12: r = a & b; rR = 0; break;
            case 0x13: r = a | b; rR = 0; break;
            case 0x14: r = a ^ b; rR = 0; break;
            case 0x15: r = ~a; rR = 0; break;
            }
        if (top < EXPR_STACK_DEPTH) {
            relocBits = (relocBits << 1) | (rR & 1);
            stack[top++] = r;
            }
        continue;
        }

    /* --- Pushes ------------------------------------------------------- */
    if (op == EXPR_PC) {
        if (top < EXPR_STACK_DEPTH) {
            relocBits = relocBits << 1;
            stack[top++] = pc;
            }
        continue;
        }

    if (op == EXPR_CONST) {
        dword v;
        OmfReadDword(fp, &v);
        if (top < EXPR_STACK_DEPTH) {
            relocBits = relocBits << 1;
            stack[top++] = (long)v;
            }
        continue;
        }

    if (op == EXPR_SEGDISP) {
        dword v;
        OmfReadDword(fp, &v);
        if (top < EXPR_STACK_DEPTH) {
            relocBits = (relocBits << 1) | 1;
            /* Stock exp.asm:1403 pushes (startpc + operand). Symbols in
             * our table are also stored as segBase+offset, so
             * (segLabel+N)-segLabel cancels to N via the rel-rel Sub
             * clear in relocBits. */
            stack[top++] = exprSegStartPc + (long)v;
            }
        continue;
        }

    if (op == EXPR_STRONG || op == EXPR_WEAK ||
        op == 0x84 || op == 0x85 || op == 0x86) {
        char    name[NAME_MAX];
        Symbol *sym;
        long    v = 0;
        unsigned int r = 0;

        OmfReadPString(fp, name, NAME_MAX);

        sym = SymFind(name, fileNum);
        if (!sym || !(sym->flags & SYM_PASS1_RESOLVED)) {
            /* WEAK resolves to 0 silently; STRONG triggers library
             * search in pass 1 or link error in pass 2. */
            if (op != EXPR_WEAK) {
                if (phase == EXPR_PHASE_COLLECT) SymRequest(name, 1, fileNum);
                else if (op == EXPR_STRONG) LinkError("undefined symbol", name);
                }
            }
        else {
            v = sym->value;
            if (!(sym->flags & SYM_IS_CONSTANT)) {
                r = 1;
                lastSeg = sym->segNum;
                }
            }
        if (top < EXPR_STACK_DEPTH) {
            relocBits = (relocBits << 1) | (r & 1);
            stack[top++] = v;
            }
        continue;
        }

    /* Unknown opcode: stop walking. */
    break;
    }

{
long tv   = (top > 0) ? stack[top - 1] : 0;
int  topR = (int)(relocBits & 1);
if (result)       *result       = tv;
if (needsReloc)   *needsReloc   = topR ? TRUE : FALSE;
if (segOut)       *segOut       = topR ? lastSeg : 0;
if (shiftOut)     *shiftOut     = shifted ? shiftCount : 0;
if (unshiftedOut) *unshiftedOut = shifted ? unshiftedVal : tv;
}
return 1;
}

/* Convenience wrapper: walk an expression stream purely for its side
 * effects (marking symbols for library search in phase 1). Discards the
 * computed value.  Used by pass-1 callers that need to scan past an
 * expression without caring about its result. */
void SkipExpr(FILE *fp, int phase, int fileNum)
{
long  result;
int   segOut, fileOut;
BOOLEAN needsReloc;
EvalExpr(fp, 0L, &result, &segOut, &fileOut, &needsReloc, phase, fileNum,
         NULL, NULL);
}
