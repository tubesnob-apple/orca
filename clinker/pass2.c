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

/* Expression handling lives in expr.c — pass 2 uses EXPR_PHASE_RESOLVE
 * so that unresolved STRONG references become link-time errors. */

/* ----------------------------------------------------------
   SUPER record unpacker

   SUPER ($F7) is a packed form of RELOC/cRELOC/INTERSEG/cINTERSEG
   that encodes only the patch offsets — the value to be relocated
   is read back from the already-emitted LCONST bytes in the output
   segment at the patch location.

   Layout (opcode already consumed by the caller):
     dword length    — size of the rest of the record
     byte  type      — 0..37 (see table below)
     ...             — packed subrecord stream, length - 1 bytes

   Each subrecord starts with a Count byte:
     Count & $80 set  — skip (Count & $7F) 256-byte pages of the segment
     Count & $80 clr  — (Count + 1) in-page 1-byte offsets follow; the
                        current page advances by one after processing

   SUPER type classification (GS/OS Reference Appendix F p.463):

     0          RELOC2      2-byte patch, shift=0, intra-segment
     1          RELOC3      3-byte patch, shift=0, intra-segment
     2          INTERSEG1   3-byte, shift=0, file=1,  seg in LCONST byte 2
     3..13      INTERSEG2..12    3-byte, shift=0, file=type-1, seg in byte 2
     14..25     INTERSEG13..24   2-byte, shift=0,  file=1, seg=type-13
     26..37     INTERSEG25..36   2-byte, shift=-16, file=1, seg=type-25

   For every patch emitted, OffsetReference is read from out->data at
   the patch location using patchLen bytes; for INTERSEG1..12 the
   target-segment byte is at patchLocation+2.
   ---------------------------------------------------------- */

/* Classify a SUPER type byte into the patch shape it describes. */
typedef struct {
    byte patchLen;     /* 2 or 3 */
    byte shift;        /* 0 or 0xF0 (= -16 in two's complement) */
    int  fileNum;      /* 0 for intra-seg RELOC, else file number */
    int  fixedSeg;     /* 0 = read seg from LCONST byte 2; >0 = use this */
    int  isInterseg;   /* 1 if we emit INTERSEG, 0 if plain RELOC */
    int  valid;        /* 0 = unknown subtype */
} SuperShape;

static SuperShape ClassifySuper(byte type)
{
SuperShape s = { 0, 0, 0, 0, 0, 0 };

if (type == 0x00)        { s.patchLen = 2; s.isInterseg = 0; s.valid = 1; }
else if (type == 0x01)   { s.patchLen = 3; s.isInterseg = 0; s.valid = 1; }
else if (type <= 0x0D)   { s.patchLen = 3; s.isInterseg = 1;
                           s.fileNum  = type - 1;
                           s.valid    = 1; }
else if (type <= 0x19)   { s.patchLen = 2; s.isInterseg = 1;
                           s.fileNum  = 1;
                           /* type encodes POST-ExpressLoad segnum
                            * (`segment = type - 12` per loader spec).
                            * Convert to clinker's pre-remap segNum. */
                           s.fixedSeg = (type - 12)
                                        - (opt_express ? 1 : 0);
                           s.valid    = 1; }
else if (type <= 0x25)   { s.patchLen = 2; s.isInterseg = 1;
                           s.shift    = 0xF0;   /* right-shift 16, bank byte */
                           s.fileNum  = 1;
                           s.fixedSeg = (type - 24)
                                        - (opt_express ? 1 : 0);
                           s.valid    = 1; }

return s;
}

/* Read `len` little-endian bytes from out->data at absPatch; clamp to
 * the segment buffer for safety. */
static long ReadLConstValue(OutSeg *out, long absPatch, int len)
{
long v = 0;
int  i;

for (i = 0; i < len; i++) {
    if (absPatch + i < 0 || absPatch + i >= out->dataLen)
        return 0;
    v |= ((long)out->data[absPatch + i]) << (i * 8);
    }
return v;
}

/* Emit one unpacked patch from a SUPER subrecord. */
static void EmitSuperPatch(OutSeg *out, const SuperShape *s,
                           long absPatch)
{
long value;
int  tgtSeg;

if (absPatch + s->patchLen > out->dataLen) {
    LinkError("SUPER offset past end of LCONST", out->segName);
    return;
    }

value = ReadLConstValue(out, absPatch, s->patchLen);

if (!s->isInterseg) {
    /* SUPER RELOC2/RELOC3: intra-segment */
    AppendReloc(out, absPatch, s->patchLen, s->shift, value, 0, 0, 0);
    return;
    }

if (s->fixedSeg > 0) {
    /* INTERSEG13..36: segment # is fixed by type */
    tgtSeg = s->fixedSeg;
    }
else {
    /* INTERSEG1..12: POST-ExpressLoad segnum is in the third byte of
     * the patch. Convert to pre-remap for RelocRec. */
    tgtSeg = (int)out->data[absPatch + 2] - (opt_express ? 1 : 0);
    /* Value is the 16-bit offset (low bytes); drop the segment byte */
    value &= 0xFFFFL;
    }

AppendReloc(out, absPatch, s->patchLen, s->shift, value, 1,
            tgtSeg, s->fileNum);
}

/*
 * Pass2Super — consume one SUPER record from fp (the opcode has already
 * been consumed by the caller) and append the unpacked relocations to
 * `out`.  `baseOffset` is the offset of the current input segment
 * within `out` (so an in-segment patch offset O becomes an output
 * address of baseOffset+O).
 */
static void Pass2Super(FILE *fp, OutSeg *out, long baseOffset)
{
dword      totalLen;
byte       typeByte;
long       endPos;
int        page = 0;
SuperShape shape;

OmfReadDword(fp, &totalLen);
endPos = ftell(fp) + (long)totalLen;

typeByte = (byte)OmfReadByte(fp);
shape    = ClassifySuper(typeByte);

if (!shape.valid) {
    LinkError("unknown SUPER subtype", out->segName);
    fseek(fp, endPos, SEEK_SET);
    return;
    }

while (ftell(fp) < endPos) {
    byte count = (byte)OmfReadByte(fp);

    if (count & 0x80) {
        page += (count & 0x7F);
        continue;
        }

    /* Non-skip subrecord: (count + 1) in-page 1-byte offsets follow. */
    {
    int n = (int)count + 1;
    int i;
    for (i = 0; i < n; i++) {
        byte  inPage    = (byte)OmfReadByte(fp);
        long  offset    = ((long)page << 8) | (long)inPage;
        long  absPatch  = offset + baseOffset;
        EmitSuperPatch(out, &shape, absPatch);
        }
    }
    page++;
    }

/* Defensive: if the stream didn't exactly hit endPos, realign. */
fseek(fp, endPos, SEEK_SET);
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
        /* INTERSEG: pLen(1) shift(1) pc(4) fileNum(2) segNum(2) addend(4) */
        byte pLen  = (byte)OmfReadByte(inf->fp);
        byte shift = (byte)OmfReadByte(inf->fp);
        dword rpc, addend;
        word  iFileNum, iSegNum;
        OmfReadDword(inf->fp, &rpc);
        OmfReadWord(inf->fp, &iFileNum);
        OmfReadWord(inf->fp, &iSegNum);
        OmfReadDword(inf->fp, &addend);
        AppendReloc(out, (long)rpc + seg->baseOffset,
                    pLen, shift, (long)addend, 1, (int)iSegNum, (int)iFileNum);
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
        /* cINTERSEG: pLen(1) shift(1) pc16(2) segNum(1) value16(2).
         * fileNum is implicitly 1 per the spec (cINTERSEG omits the
         * file-number word to save space), so we record 1 here; this
         * lets the record qualify for cINTERSEG/SUPER INTERSEG13..36
         * re-emission later. */
        byte pLen  = (byte)OmfReadByte(inf->fp);
        byte shift = (byte)OmfReadByte(inf->fp);
        word rpc;
        byte iSegNum;
        word value;
        OmfReadWord(inf->fp, &rpc);
        iSegNum = (byte)OmfReadByte(inf->fp);
        OmfReadWord(inf->fp, &value);
        AppendReloc(out, (long)(word)rpc + seg->baseOffset,
                    pLen, shift, (long)(word)value, 1, (int)iSegNum, 1);
        }
    else if (op == OP_EXPR || op == OP_ZEXPR || op == OP_BEXPR) {
        /* Evaluate the expression and emit pLen bytes into the LCONST
         * image.  If the result is relocatable, record the reference
         * value in the RELOC/INTERSEG entry so the loader patches
         * (segment_base + value). For a shifted expression (e.g.
         * `FOO >> 16` for bank byte), stock iix link stores the
         * UNSHIFTED reference in the record + LCONST and tags the
         * reloc with the shift count, so the loader re-applies the
         * shift at load time. Match that. */
        byte pLen = (byte)OmfReadByte(inf->fp);
        byte buf[4] = {0, 0, 0, 0};
        int  i;
        int  shiftCount = 0;
        long unshifted  = 0;
        long storeVal;
        byte shiftByte;
        EvalExpr(inf->fp, pc, &result, &segOut, &fileOut, &needsReloc,
                 EXPR_PHASE_RESOLVE, &shiftCount, &unshifted);
        storeVal  = (shiftCount != 0) ? unshifted : result;
        shiftByte = (byte)(shiftCount & 0xFF);
        for (i = 0; i < pLen && i < 4; i++)
            buf[i] = (byte)(storeVal >> (i * 8));
        EmitData(out, buf, (long)pLen);
        if (needsReloc) {
            if (segOut != 0 && segOut != out->segNum)
                AppendReloc(out, pc, pLen, shiftByte, storeVal, 1, segOut, 1);
            else
                AppendReloc(out, pc, pLen, shiftByte, storeVal, 0, 0, 0);
            }
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
        EvalExpr(inf->fp, pc, &result, &segOut, &fileOut, &needsReloc,
                 EXPR_PHASE_RESOLVE, NULL, NULL);
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
        EvalExpr(inf->fp, pc, &result, &segOut, &fileOut, &needsReloc,
                 EXPR_PHASE_RESOLVE, NULL, NULL);
        }
    else if (op == OP_LEXPR) {
        /* LEXPR ($F3): patch in outer segment; same layout as EXPR. */
        byte pLen = (byte)OmfReadByte(inf->fp);
        byte buf[4] = {0, 0, 0, 0};
        int  i;
        int  shiftCount = 0;
        long unshifted  = 0;
        long storeVal;
        byte shiftByte;
        EvalExpr(inf->fp, pc, &result, &segOut, &fileOut, &needsReloc,
                 EXPR_PHASE_RESOLVE, &shiftCount, &unshifted);
        storeVal  = (shiftCount != 0) ? unshifted : result;
        shiftByte = (byte)(shiftCount & 0xFF);
        for (i = 0; i < pLen && i < 4; i++)
            buf[i] = (byte)(storeVal >> (i * 8));
        EmitData(out, buf, (long)pLen);
        if (needsReloc) {
            if (segOut != 0 && segOut != out->segNum)
                AppendReloc(out, pc, pLen, shiftByte, storeVal, 1, segOut, 1);
            else
                AppendReloc(out, pc, pLen, shiftByte, storeVal, 0, 0, 0);
            }
        pc += pLen;
        }
    else if (op == OP_ENTRY) {
        /* ENTRY ($F4): word(reserved) + dword(value) + pstring(name) — skip */
        word  w;
        dword v;
        int   nlen;
        OmfReadWord(inf->fp, &w);
        OmfReadDword(inf->fp, &v);
        nlen = fgetc(inf->fp);
        if (nlen != EOF) fseek(inf->fp, nlen, SEEK_CUR);
        }
    else if (op == OP_SUPER) {
        /* SUPER: packed RELOC/INTERSEG form; unpack into our RelocRec
         * list.  Reads reference values back from the emitted LCONST
         * bytes in out->data at each patch offset (see Pass2Super). */
        Pass2Super(inf->fp, out, seg->baseOffset);
        }
    else if (op == OP_STRONG || op == OP_USING) {
        int nlen = fgetc(inf->fp);
        if (nlen != EOF) fseek(inf->fp, nlen, SEEK_CUR);
        }
    else if (op == OP_MEM) {
        fseek(inf->fp, 8, SEEK_CUR);  /* two 4-byte numbers */
        }
    else {
        char msg[64];
        sprintf(msg, "unknown OMF record $%02X in pass 2", op);
        LinkError(msg, seg->segName);
        break;
        }
    }

/* RESSPC zero-fill: inline at end of each input seg so the merged
 * output contains the full memory image. Matches pass1's measured
 * += resspc bump, which sized the output buffer. */
if (seg->resspc > 0)
    EmitZero(out, seg->resspc);

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
