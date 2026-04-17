/**************************************************************
*
*  gsplus.c -- GSplus emulator debug integration
*
*  When the shell variable `gsplusSymbols` is set at link time,
*  clinker emits a <target>.symbols JSON sidecar alongside the
*  load file.  The GSplus emulator reads that sidecar to
*  annotate memory addresses with function/variable names in
*  its trace window.
*
*  There is also a 32-bit "link signature" (sfSig) derived from
*  the output basename; pass 2 injects a WDM prologue carrying
*  it into segment 1 so the emulator can bind a running image
*  back to the matching .symbols file.
*
*  See linker/gsplusSymbols.md for the sidecar format spec.
*
**************************************************************/

#pragma keep "gsplus"
#pragma optimize 9

#include "clinker.h"

/*
 * InitGsplusSymbols — consult the gsplusSymbols shell variable and,
 * if set, enable sidecar emission and compute sfSig.  The signature
 * is a rotate-left-and-xor of baseName characters — not cryptographic,
 * just enough to make accidental mismatches between a binary and its
 * .symbols file unlikely.
 */
void InitGsplusSymbols(void)
{
char  *envval;
char  *p;
dword  sig = 0x12345678UL;

envval = getenv("gsplusSymbols");
if (!envval || !envval[0]) {
    opt_gsplus = FALSE;
    return;
    }

opt_gsplus = TRUE;
for (p = baseName; *p; p++) {
    sig = (sig << 7) | (sig >> 25);
    sig ^= (dword)(unsigned char)*p;
    }
sfSig = sig;
}

/* ── .symbols JSON sidecar ─────────────────────────────────────────────── */

static const char *SegTypeString(word segType)
{
switch (segType & 0x1F) {
    case 0x00: return "code";
    case 0x01: return "data";
    case 0x12: return "jumptable";
    default:   return "other";
    }
}

static void EmitSegmentsArray(FILE *fp)
{
OutSeg *seg;
BOOLEAN first = TRUE;

fprintf(fp, "\"segments\":[");
for (seg = outSegs; seg; seg = seg->next) {
    if (!first) fprintf(fp, ",");
    first = FALSE;
    fprintf(fp,
        "{\"number\":\"0x%04X\","
         "\"name\":\"%s\","
         "\"type\":\"%s\","
         "\"org\":\"0x%08lX\","
         "\"length\":\"0x%08lX\"}",
        seg->segNum, seg->segName, SegTypeString(seg->segType),
        seg->org, seg->dataLen);
    }
fprintf(fp, "]");
}

static void EmitSymbolsArray(FILE *fp)
{
Symbol *sym;
BOOLEAN first = TRUE;
int     i;

fprintf(fp, "\"symbols\":[");
for (i = 0; i < SYM_HASH_SIZE; i++) {
    for (sym = symHash[i]; sym; sym = sym->next) {
        if (!(sym->flags & SYM_PASS1_RESOLVED)) continue;
        if (sym->flags & SYM_IS_SEGMENT)        continue;
        if (!first) fprintf(fp, ",");
        first = FALSE;
        fprintf(fp,
            "{\"name\":\"%s\","
             "\"segment\":\"0x%04X\","
             "\"offset\":\"0x%08lX\","
             "\"global\":%s}",
            sym->name, sym->segNum, sym->value,
            (sym->flags & SEGKIND_PRIVATE) ? "false" : "true");
        }
    }
fprintf(fp, "]");
}

void WriteSymbolFile(void)
{
char   symPath[PATH_MAX + 16];
FILE  *fp;

if (!opt_gsplus || !keepName[0]) return;

snprintf(symPath, sizeof(symPath), "%s.symbols", keepName);
fp = fopen(symPath, "w");
if (!fp) {
    LinkError("cannot create symbol file", symPath);
    return;
    }

fprintf(fp, "{");
fprintf(fp, "\"orca_symbols_version\":\"0x0001\",");
fprintf(fp, "\"symsig\":\"0x%08lX\",", sfSig);
fprintf(fp, "\"target\":\"%s\",", baseName);
fprintf(fp, "\"linker\":\"ORCA/M Link Editor 2.3.0 (clinker)\",");
EmitSegmentsArray(fp);
fprintf(fp, ",");
EmitSymbolsArray(fp);
fprintf(fp, "}");

fclose(fp);

if (opt_progress)
    printf("Symbol file: %s\n", symPath);
}
