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

/* ── .sym65 platform-symbol sidecar (for 6502Bench SourceGen) ──────────── */

/*
 * WriteSym65File — emit a .sym65 platform-symbol file alongside the
 * .symbols JSON.  SourceGen's parser (PlatformSymbols.cs) accepts lines
 * of the form:
 *
 *     LABEL @ $address  [width]  ; comment
 *
 * We can't emit absolute addresses — clinker's output is relocatable,
 * the load address is set at runtime — so each symbol's address here
 * is a synthetic "segment-ordinal bank + in-segment offset" of the
 * form $SSOOOO where SS is the 1-based segment number and OOOO is the
 * in-segment offset.  Users loading our output at a known base can
 * compute absolute addresses as (base + OOOO) for the relevant segment.
 * The comment on each line names the source segment.
 */
void WriteSym65File(void)
{
char   path[PATH_MAX + 16];
FILE  *fp;
int    i;
Symbol *sym;

if (!opt_gsplus || !keepName[0]) return;

snprintf(path, sizeof(path), "%s.sym65", keepName);
fp = fopen(path, "w");
if (!fp) {
    LinkError("cannot create .sym65 file", path);
    return;
    }

fprintf(fp, "; clinker-generated platform-symbol file for SourceGen\n");
fprintf(fp, "; target: %s\n", baseName);
fprintf(fp, "; address encoding: $SSOOOO  (SS = segment #, OOOO = offset)\n");
fprintf(fp, "; symsig: 0x%08lX\n", sfSig);
fprintf(fp, "\n");

for (i = 0; i < SYM_HASH_SIZE; i++) {
    for (sym = symHash[i]; sym; sym = sym->next) {
        long synthetic;
        if (!(sym->flags & SYM_PASS1_RESOLVED)) continue;
        if (sym->flags & SYM_IS_SEGMENT)        continue;
        synthetic = ((long)sym->segNum << 16) | (sym->value & 0xFFFFL);
        fprintf(fp, "%-24s @ $%06lX",
                sym->name, synthetic);
        if (sym->flags & SEGKIND_PRIVATE)
            fprintf(fp, "        ; seg %d, private", sym->segNum);
        else
            fprintf(fp, "        ; seg %d", sym->segNum);
        fprintf(fp, "\n");
        }
    }

fclose(fp);

if (opt_progress)
    printf("Sym65  file: %s\n", path);
}
