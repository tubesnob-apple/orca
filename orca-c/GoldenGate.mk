# GoldenGate.mk — build ORCA/C using GoldenGate iix toolchain
#
# Usage (from repo root):
#   make           — compile all modules to obj/
#   make link      — compile + link to goldengate/cc (local binary)
#   make install   — compile + link, installing to GoldenGate Languages/cc
#   make clean     — remove compiled objects
#
# Module set follows linkit2 (the smake build path for 2.2.1):
#   ccommon mm cgi scanner symbol header2 expression cgc asm parser
#   cc objout2 native2 gen dag2   (table is the special asm/pascal case)
#
# Header2/ObjOut2/Native2/DAG2 replace their non-2 counterparts.
# They are compiled with keep=obj/<original_name> so that uses clauses
# (uses Header, uses ObjOut, etc.) can find them by filename.
# Printf and Charset are compiled for interface lookup only — NOT linked.
#
# iix compile --keep stem writes stem.a (Pascal) or stem.int/.B
# iix assemble --keep stem writes stem.A (uppercase, assembler)
# iix link takes stems WITHOUT suffix; keep=16/cc installs to GoldenGate Languages/

IIX      := iix
CHTYP    := iix chtyp
PASFLAGS := +T -P
ASMFLAGS := +T -P
OBJ      := obj
BIN_DIR  ?= goldengate

# Compiled objects — linkit2 link set (16 modules) + charset/printf (interface only)
OBJS := \
	$(OBJ)/ccommon.a   \
	$(OBJ)/table.A     \
	$(OBJ)/mm.a        \
	$(OBJ)/cgi.a       \
	$(OBJ)/cgc.a       \
	$(OBJ)/charset.a   \
	$(OBJ)/scanner.a   \
	$(OBJ)/symbol.a    \
	$(OBJ)/header.a    \
	$(OBJ)/printf.a    \
	$(OBJ)/expression.a \
	$(OBJ)/asm.a       \
	$(OBJ)/parser.a    \
	$(OBJ)/objout.a    \
	$(OBJ)/native.a    \
	$(OBJ)/gen.a       \
	$(OBJ)/dag.a       \
	$(OBJ)/cc.a

.PHONY: all link install clean

all: $(OBJS)

link: $(OBJS)
	$(IIX) link \
	  $(OBJ)/cc $(OBJ)/symbol $(OBJ)/parser $(OBJ)/expression \
	  $(OBJ)/scanner $(OBJ)/mm $(OBJ)/ccommon $(OBJ)/cgi $(OBJ)/cgc \
	  $(OBJ)/asm $(OBJ)/table $(OBJ)/objout $(OBJ)/native \
	  $(OBJ)/dag $(OBJ)/gen $(OBJ)/header \
	  $(OBJ)/charset $(OBJ)/printf \
	  keep=$(BIN_DIR)/cc

install: $(OBJS)
	$(IIX) link \
	  $(OBJ)/cc $(OBJ)/symbol $(OBJ)/parser $(OBJ)/expression \
	  $(OBJ)/scanner $(OBJ)/mm $(OBJ)/ccommon $(OBJ)/cgi $(OBJ)/cgc \
	  $(OBJ)/asm $(OBJ)/table $(OBJ)/objout $(OBJ)/native \
	  $(OBJ)/dag $(OBJ)/gen $(OBJ)/header \
	  $(OBJ)/charset $(OBJ)/printf \
	  keep=16/cc

$(OBJ):
	mkdir -p $(OBJ)

# ── Foundation ────────────────────────────────────────────────────────────────

$(OBJ)/ccommon.a: CCommon.pas CCommon.asm CCommon.macros | $(OBJ)
	$(CHTYP) -t SRC -a 5 CCommon.pas
	$(CHTYP) -t SRC -a 3 CCommon.asm
	$(IIX) compile $(PASFLAGS) --keep=$(basename $@) CCommon.pas

# Table: Pascal compile produces table.a; ASM compile produces table.A.
# Delete the Pascal root segment (table.root) — only the asm object is linked.
$(OBJ)/table.A: Table.pas Table.asm Table.macros $(OBJ)/ccommon.a | $(OBJ)
	$(CHTYP) -t SRC -a 5 Table.pas
	$(CHTYP) -t SRC -a 3 Table.asm
	$(IIX) compile $(PASFLAGS) --keep=$(OBJ)/table Table.pas
	$(IIX) assemble $(ASMFLAGS) --keep=$(OBJ)/table Table.asm
	rm -f $(OBJ)/table.root

$(OBJ)/mm.a: MM.pas MM.asm MM.macros $(OBJ)/ccommon.a | $(OBJ)
	$(CHTYP) -t SRC -a 5 MM.pas
	$(CHTYP) -t SRC -a 3 MM.asm
	$(IIX) compile $(PASFLAGS) --keep=$(basename $@) MM.pas

$(OBJ)/cgi.a: CGI.pas CGI.Comments CGI.Debug $(OBJ)/ccommon.a | $(OBJ)
	$(CHTYP) -t SRC -a 5 CGI.pas
	$(CHTYP) -t SRC -a 5 CGI.Comments
	$(CHTYP) -t SRC -a 5 CGI.Debug
	$(IIX) compile $(PASFLAGS) --keep=$(basename $@) CGI.pas

$(OBJ)/cgc.a: CGC.pas CGC.asm CGC.macros $(OBJ)/ccommon.a $(OBJ)/cgi.a | $(OBJ)
	$(CHTYP) -t SRC -a 5 CGC.pas
	$(CHTYP) -t SRC -a 3 CGC.asm
	$(IIX) compile $(PASFLAGS) --keep=$(basename $@) CGC.pas

# ── Charset: needed for interface lookup by Scanner (not linked separately) ───

$(OBJ)/charset.a: Charset.pas $(OBJ)/ccommon.a $(OBJ)/table.A | $(OBJ)
	$(CHTYP) -t SRC -a 5 Charset.pas
	$(IIX) compile $(PASFLAGS) --keep=$(basename $@) Charset.pas

# ── Scanner-layer ─────────────────────────────────────────────────────────────

$(OBJ)/scanner.a: Scanner.pas Scanner.asm Scanner.macros Scanner.debug \
    $(OBJ)/ccommon.a $(OBJ)/cgi.a $(OBJ)/mm.a $(OBJ)/table.A $(OBJ)/charset.a | $(OBJ)
	$(CHTYP) -t SRC -a 5 Scanner.pas
	$(CHTYP) -t SRC -a 3 Scanner.asm
	$(CHTYP) -t SRC -a 5 Scanner.debug
	$(IIX) compile $(PASFLAGS) --keep=$(basename $@) Scanner.pas

$(OBJ)/symbol.a: Symbol.pas Symbol.asm Symbol.macros Symbol.Print \
    $(OBJ)/ccommon.a $(OBJ)/cgi.a $(OBJ)/mm.a $(OBJ)/scanner.a | $(OBJ)
	$(CHTYP) -t SRC -a 5 Symbol.pas
	$(CHTYP) -t SRC -a 3 Symbol.asm
	$(CHTYP) -t SRC -a 5 Symbol.Print
	$(IIX) compile $(PASFLAGS) --keep=$(basename $@) Symbol.pas

# Header2 replaces Header; compiled as obj/header so 'uses Header' finds it.
$(OBJ)/header.a: Header2.pas Header.pas \
    $(OBJ)/ccommon.a $(OBJ)/cgi.a $(OBJ)/mm.a $(OBJ)/scanner.a $(OBJ)/symbol.a | $(OBJ)
	$(CHTYP) -t SRC -a 5 Header2.pas
	$(CHTYP) -t SRC -a 5 Header.pas
	$(IIX) compile $(PASFLAGS) --keep=$(basename $@) Header2.pas

# Printf: needed for interface lookup by Expression (not linked separately)
$(OBJ)/printf.a: Printf.pas $(OBJ)/ccommon.a $(OBJ)/scanner.a | $(OBJ)
	$(CHTYP) -t SRC -a 5 Printf.pas
	$(IIX) compile $(PASFLAGS) --keep=$(basename $@) Printf.pas

$(OBJ)/expression.a: Expression.pas Expression.asm Exp.macros \
    $(OBJ)/ccommon.a $(OBJ)/cgi.a $(OBJ)/mm.a \
    $(OBJ)/scanner.a $(OBJ)/symbol.a $(OBJ)/printf.a | $(OBJ)
	$(CHTYP) -t SRC -a 5 Expression.pas
	$(CHTYP) -t SRC -a 3 Expression.asm
	$(IIX) compile $(PASFLAGS) --keep=$(basename $@) Expression.pas

# ── Parser and inline assembler ────────────────────────────────────────────────

$(OBJ)/asm.a: Asm.pas \
    $(OBJ)/ccommon.a $(OBJ)/cgi.a $(OBJ)/cgc.a $(OBJ)/expression.a \
    $(OBJ)/mm.a $(OBJ)/scanner.a $(OBJ)/symbol.a | $(OBJ)
	$(CHTYP) -t SRC -a 5 Asm.pas
	$(IIX) compile $(PASFLAGS) --keep=$(basename $@) Asm.pas

$(OBJ)/parser.a: Parser.pas \
    $(OBJ)/ccommon.a $(OBJ)/cgi.a $(OBJ)/cgc.a $(OBJ)/expression.a \
    $(OBJ)/header.a $(OBJ)/mm.a \
    $(OBJ)/scanner.a $(OBJ)/symbol.a | $(OBJ)
	$(CHTYP) -t SRC -a 5 Parser.pas
	$(IIX) compile $(PASFLAGS) --keep=$(basename $@) Parser.pas

# ── Code generator chain (linkit2 variants) ───────────────────────────────────

# ObjOut2 replaces ObjOut; compiled as obj/objout so 'uses ObjOut' finds it.
$(OBJ)/objout.a: ObjOut2.pas ObjOut2.asm ObjOut.macros ObjOut.pas ObjOut.asm \
    $(OBJ)/ccommon.a $(OBJ)/cgi.a $(OBJ)/cgc.a | $(OBJ)
	$(CHTYP) -t SRC -a 5 ObjOut2.pas
	$(CHTYP) -t SRC -a 5 ObjOut.pas
	$(CHTYP) -t SRC -a 3 ObjOut2.asm
	$(CHTYP) -t SRC -a 3 ObjOut.asm
	$(IIX) compile $(PASFLAGS) --keep=$(basename $@) ObjOut2.pas

# Native2 replaces Native; compiled as obj/native so 'uses Native' finds it.
$(OBJ)/native.a: Native2.pas Native.pas Native.asm Native.macros \
    $(OBJ)/ccommon.a $(OBJ)/cgi.a $(OBJ)/cgc.a $(OBJ)/objout.a | $(OBJ)
	$(CHTYP) -t SRC -a 5 Native2.pas
	$(CHTYP) -t SRC -a 5 Native.pas
	$(CHTYP) -t SRC -a 3 Native.asm
	$(IIX) compile $(PASFLAGS) --keep=$(basename $@) Native2.pas

# Gen before DAG (DAG uses Gen's interface)
$(OBJ)/gen.a: Gen.pas \
    $(OBJ)/ccommon.a $(OBJ)/cgi.a $(OBJ)/native.a | $(OBJ)
	$(CHTYP) -t SRC -a 5 Gen.pas
	$(IIX) compile $(PASFLAGS) --keep=$(basename $@) Gen.pas

# DAG2 replaces DAG; compiled as obj/dag.
$(OBJ)/dag.a: DAG2.pas DAG.pas \
    $(OBJ)/ccommon.a $(OBJ)/cgi.a $(OBJ)/native.a $(OBJ)/gen.a | $(OBJ)
	$(CHTYP) -t SRC -a 5 DAG2.pas
	$(CHTYP) -t SRC -a 5 DAG.pas
	$(IIX) compile $(PASFLAGS) --keep=$(basename $@) DAG2.pas

# ── Main entry point ───────────────────────────────────────────────────────────

$(OBJ)/cc.a: CC.pas \
    $(OBJ)/ccommon.a $(OBJ)/mm.a $(OBJ)/cgi.a $(OBJ)/cgc.a \
    $(OBJ)/table.A \
    $(OBJ)/scanner.a $(OBJ)/symbol.a $(OBJ)/header.a \
    $(OBJ)/expression.a $(OBJ)/asm.a $(OBJ)/parser.a \
    $(OBJ)/objout.a $(OBJ)/native.a $(OBJ)/gen.a $(OBJ)/dag.a \
    | $(OBJ)
	$(CHTYP) -t SRC -a 5 CC.pas
	$(IIX) compile $(PASFLAGS) --keep=$(basename $@) CC.pas

# ── Maintenance ────────────────────────────────────────────────────────────────

clean:
	rm -f $(OBJ)/*.a $(OBJ)/*.A $(OBJ)/*.B $(OBJ)/*.int $(OBJ)/*.root
