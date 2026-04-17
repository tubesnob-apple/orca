/**************************************************************
*
*  pass2.c -- Pass 2 for clinker
*
*  Reads each input segment body a second time.  Copies Const
*  and LCONST bytes into the output segment data buffer, fills
*  DS regions with zeros, collects RELOC/INTERSEG records, and
*  evaluates EXPR records to produce patched data + reloc
*  entries for the output.
*
**************************************************************/

#pragma keep "pass2"
#pragma optimize 9

#include "clinker.h"

/* ----------------------------------------------------------
   Expression evaluation
   Returns 1 on success.  Sets *result to the computed value.
   Sets *segOut / *fileOut if the result requires relocation
   (i.e., depends on a segment-relative symbol).
   Sets *needsReloc if a RELOC or INTERSEG entry is required.
   ---------------------------------------------------------- */
int EvalExpr(FILE *fp, long pc, long *result, int *segOut, int *fileOut,
             BOOLEAN *needsReloc)
{
long stack[32];
int  top = 0;
int  op;

*result     = 0;
*segOut     = 0;
*fileOut    = 0;
*needsReloc = FALSE;

/* Expression is a stream of opcodes terminated by $00 (EXPR_END) */
for (;;) {
    op = fgetc(fp);
    if (op == EOF || op == EXPR_END) break;

    if (op >= 0x01 && op <= 0x15) {
        long b = (top > 0) ? stack[--top] : 0;
        long a = (top > 0) ? stack[--top] : 0;
        long r = 0;
        switch (op) {
            case 0x01: r = a + b; break;
            case 0x02: r = a - b; break;
            case 0x03: r = a * b; break;
            case 0x04: r = b ? a / b : 0; break;
            case 0x05: r = b ? a % b : 0; break;
            case 0x06: r = ~b;    break;
            case 0x09: r = a & b; break;
            case 0x0A: r = a | b; break;
            case 0x0B: r = a ^ b; break;
            default:   r = a + b; break;
            }
        if (top < 32) stack[top++] = r;
        }
    else if (op == EXPR_PC) {
        if (top < 32) stack[top++] = pc;
        }
    else if (op == EXPR_CONST || op == EXPR_SEGDISP) {
        dword v;
        OmfReadDword(fp, &v);
        if (top < 32) stack[top++] = (long)v;
        if (op == EXPR_SEGDISP) *needsReloc = TRUE;
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
            if (op == EXPR_STRONG)
                LinkError("undefined symbol", name);
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
   Pass2Seg

   Second pass over one input segment body.  Appends data to
   the output segment buffer, resolves expressions, and
   collects relocation records.
   ---------------------------------------------------------- */
int Pass2Seg(InputFile *inf, InSeg *seg, OutSeg *out)
{
long pc = seg->baseOffset;  /* current logical pc in output segment */
int  op;
BOOLEAN needsReloc;
long result;
int  segOut, fileOut;

if (fseek(inf->fp, seg->fileBodyOffset, SEEK_SET) != 0)
    return 0;

/* Ensure output buffer is large enough */
GrowData(out, seg->measuredLen);

for (;;) {
    op = fgetc(inf->fp);
    if (op == EOF) break;
    op &= 0xFF;

    if (op == OP_END) {
        break;
        }
    else if (op >= 0x01 && op <= 0xDF) {
        /* Const: opcode = byte count */
        int cnt = op;
        byte buf[224];
        int  n = fread(buf, 1, (size_t)cnt, inf->fp);
        EmitData(out, buf, (long)n);
        pc += n;
        }
    else if (op == OP_DS) {
        dword len;
        OmfReadDword(inf->fp, &len);
        EmitZero(out, (long)len);
        pc += (long)len;
        }
    else if (op == OP_LCONST) {
        dword len;
        byte *buf;
        OmfReadDword(inf->fp, &len);
        buf = (byte *)malloc((size_t)len);
        if (!buf) FatalError("out of memory (LCONST)");
        fread(buf, 1, (size_t)len, inf->fp);
        EmitData(out, buf, (long)len);
        free(buf);
        pc += (long)len;
        }
    else if (op == OP_ALIGN) {
        dword factor;
        long  mask, aligned;
        OmfReadDword(inf->fp, &factor);
        if (factor > 1) {
            mask    = (long)factor - 1;
            aligned = (pc + mask) & ~mask;
            EmitZero(out, aligned - pc);
            pc = aligned;
            }
        }
    else if (op == OP_ORG) {
        dword newpc;
        OmfReadDword(inf->fp, &newpc);
        pc = (long)newpc + seg->baseOffset;
        }
    else if (op == OP_RELOC) {
        /* RELOC dictionary entry: pLen(1) shift(1) pc16(4) value(4) */
        byte  pLen  = (byte)OmfReadByte(inf->fp);
        byte  shift = (byte)OmfReadByte(inf->fp);
        dword rpc, value;
        OmfReadDword(inf->fp, &rpc);
        OmfReadDword(inf->fp, &value);
        AppendReloc(out, (long)rpc + seg->baseOffset,
                    pLen, shift, (long)value, 0, 0, 0);
        }
    else if (op == OP_INTERSEG) {
        /* INTERSEG: pLen(1) shift(1) unused(5) pc(4) fileNum(2) segNum(2) */
        byte pLen  = (byte)OmfReadByte(inf->fp);
        byte shift = (byte)OmfReadByte(inf->fp);
        dword rpc;
        word  iFileNum, iSegNum;
        fseek(inf->fp, 5, SEEK_CUR);  /* skip 5 unused bytes */
        OmfReadDword(inf->fp, &rpc);
        OmfReadWord(inf->fp, &iFileNum);
        OmfReadWord(inf->fp, &iSegNum);
        AppendReloc(out, (long)rpc + seg->baseOffset,
                    pLen, shift, 0L, 1, (int)iSegNum, (int)iFileNum);
        }
    else if (op == OP_CRELOC) {
        /* cRELOC: pLen(1) shift(1) pc16(2) value16(2) */
        byte pLen  = (byte)OmfReadByte(inf->fp);
        byte shift = (byte)OmfReadByte(inf->fp);
        word rpc, value;
        OmfReadWord(inf->fp, &rpc);
        OmfReadWord(inf->fp, &value);
        AppendReloc(out, (long)(word)rpc + seg->baseOffset,
                    pLen, shift, (long)(word)value, 0, 0, 0);
        }
    else if (op == OP_CINTERSEG) {
        /* cINTERSEG: pLen(1) shift(1) pc16(2) segNum(1) value16(2) */
        byte pLen  = (byte)OmfReadByte(inf->fp);
        byte shift = (byte)OmfReadByte(inf->fp);
        word rpc;
        byte iSegNum;
        word value;
        OmfReadWord(inf->fp, &rpc);
        iSegNum = (byte)OmfReadByte(inf->fp);
        OmfReadWord(inf->fp, &value);
        AppendReloc(out, (long)(word)rpc + seg->baseOffset,
                    pLen, shift, (long)(word)value, 1, (int)iSegNum, 0);
        }
    else if (op == OP_EXPR || op == OP_ZEXPR || op == OP_BEXPR) {
        byte pLen = (byte)OmfReadByte(inf->fp);
        byte buf[4] = {0, 0, 0, 0};
        int  i;
        EvalExpr(inf->fp, pc, &result, &segOut, &fileOut, &needsReloc);
        for (i = 0; i < pLen && i < 4; i++)
            buf[i] = (byte)(result >> (i * 8));
        EmitData(out, buf, (long)pLen);
        if (needsReloc)
            AppendReloc(out, pc, pLen, 0, 0L, 0, 0, 0);
        pc += pLen;
        }
    else if (op == OP_RELEXPR) {
        byte pLen = (byte)OmfReadByte(inf->fp);
        dword disp;
        long base;
        byte buf[4] = {0, 0, 0, 0};
        int  i;
        OmfReadDword(inf->fp, &disp);
        base = (long)disp;
        EvalExpr(inf->fp, pc, &result, &segOut, &fileOut, &needsReloc);
        result -= base;  /* relative displacement */
        for (i = 0; i < pLen && i < 4; i++)
            buf[i] = (byte)(result >> (i * 8));
        EmitData(out, buf, (long)pLen);
        pc += pLen;
        }
    else if (op == OP_GLOBAL || op == OP_LOCAL) {
        /* GLOBAL/LOCAL: pstring name + word lenAttr + byte type + byte priv */
        char name[NAME_MAX];
        word lenAttr;
        OmfReadPString(inf->fp, name, NAME_MAX);
        OmfReadWord(inf->fp, &lenAttr);  /* length attribute */
        fgetc(inf->fp);                   /* type attribute */
        fgetc(inf->fp);                   /* private flag */
        /* value = current PC, nothing more in record */
        }
    else if (op == OP_GEQU || op == OP_EQU) {
        /* EQU/GEQU: pstring name + word lenAttr + byte type + byte priv + expr$00 */
        char name[NAME_MAX];
        word lenAttr;
        OmfReadPString(inf->fp, name, NAME_MAX);
        OmfReadWord(inf->fp, &lenAttr);
        fgetc(inf->fp);
        fgetc(inf->fp);
        EvalExpr(inf->fp, pc, &result, &segOut, &fileOut, &needsReloc);
        }
    else if (op == OP_SUPER) {
        /* SUPER records in input: translate to RELOC/INTERSEG.       *
         * Currently we skip -- proper SUPER parsing is a future item. */
        dword len;
        OmfReadDword(inf->fp, &len);
        fseek(inf->fp, (long)len, SEEK_CUR);
        }
    else if (op == OP_STRONG || op == OP_USING) {
        int nlen = fgetc(inf->fp);
        if (nlen != EOF) fseek(inf->fp, nlen, SEEK_CUR);
        }
    else if (op == OP_MEM) {
        fseek(inf->fp, 8, SEEK_CUR);  /* two 4-byte numbers */
        }
    else {
        LinkError("unknown OMF record in pass 2", seg->segName);
        break;
        }
    }

return 1;
}

/* ----------------------------------------------------------
   Pass2

   Runs pass 2 over all input segments, then writes each
   output segment to the keep file.
   ---------------------------------------------------------- */
int Pass2(void)
{
InputFile *inf;
InSeg     *seg;
OutSeg    *out;

/* Allocate data buffers for all output segments */
for (out = outSegs; out; out = out->next) {
    if (out->length > 0) {
        out->data = (byte *)malloc((size_t)out->length + 8);
        if (!out->data) FatalError("out of memory (segment buffer)");
        memset(out->data, 0, (size_t)out->length + 8);
        out->dataLen   = 0;
        out->dataAlloc = out->length + 8;
        }
    }

/* GSplus: inject WDM prologue into segment 1 */
if (opt_gsplus) {
    OutSeg *first = outSegs;
    if (first && first->data) {
        byte prologue[8];
        prologue[0] = 0x42;                        /* WDM       */
        prologue[1] = 0x0F;                        /* $0F       */
        prologue[2] = 0x80;                        /* BRA       */
        prologue[3] = 0x04;                        /* +4        */
        prologue[4] = (byte)(sfSig);               /* sig lo    */
        prologue[5] = (byte)(sfSig >> 8);
        prologue[6] = (byte)(sfSig >> 16);
        prologue[7] = (byte)(sfSig >> 24);         /* sig hi    */
        EmitData(first, prologue, 8L);
        }
    }

/* Process each input segment */
for (inf = inputFiles; inf; inf = inf->next) {
    for (seg = inf->segs; seg; seg = seg->next) {
        out = OutSegByNum(seg->outSegNum);
        if (!out) continue;
        if (opt_progress)
            printf("Pass 2: %s / %s\n", inf->name, seg->segName);
        Pass2Seg(inf, seg, out);
        }
    }

return (numErrors == 0);
}
