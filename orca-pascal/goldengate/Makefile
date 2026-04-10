# goldengate/Makefile — build ORCA/Pascal using GoldenGate iix toolchain
#
# Targets:
#   make                — compile + link (local build)
#   make install        — compile + link + copy to GoldenGate Languages/
#   make clean          — remove compiled objects and linked binary
#
# Build structure:
#   12 Pascal modules compiled in dependency order with iix compile.
#   Some modules have paired .asm/.macros files; iix compile produces separate
#   .a (Pascal) and .B (ASM) object files. The GoldenGate linker only reads .a,
#   so each .B is concatenated into its .a after compilation (OMF is concatenable).
#   Compile order: pcommon cgi cgc objout native gen dag scanner symbols call parser pascal.
#   Link order:    pascal call parser native cgi symbols scanner dag cgc gen objout pcommon.

MAKEFILE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
REPO_ROOT    := $(abspath $(MAKEFILE_DIR)/..)
SRC_DIR      := $(REPO_ROOT)
OBJ_DIR      := $(REPO_ROOT)/obj
BIN_DIR      ?= $(MAKEFILE_DIR)
INSTALL_SH   ?= $(MAKEFILE_DIR)/../../goldengate/install.sh

IIX      := iix
CHTYP    := iix chtyp
PASFLAGS := +T -P

# ── GoldenGate root ──────────────────────────────────────────────────────────
OS := $(shell uname -s 2>/dev/null || echo Windows)
ifeq ($(OS),Darwin)
_GG_DEFAULT := $(HOME)/Library/GoldenGate
else ifeq ($(OS),Linux)
_GG_DEFAULT := /usr/local/share/GoldenGate
else
_GG_DEFAULT :=
endif
GG_ROOT := $(or $(GOLDEN_GATE),$(ORCA_ROOT),$(_GG_DEFAULT))
ifeq ($(GG_ROOT),)
$(error GoldenGate root not found. Set $$GOLDEN_GATE or $$ORCA_ROOT.)
endif

LANGUAGES_DIR := $(GG_ROOT)/Languages

BIN_OUT := $(BIN_DIR)/pascal

# Helper: compile a Pascal module, then merge .B into .a if it exists.
# GoldenGate's iix compile produces separate .a (Pascal) and .B (ASM) objects
# for modules with paired .asm files. The linker only reads .a for a given stem,
# so we concatenate .B into .a (OMF segments are concatenable).
# $(1) = module name
define compile-pascal
	$(CHTYP) -t src -a 5 $(SRC_DIR)/$(1).pas
	cd $(SRC_DIR) && $(IIX) compile $(PASFLAGS) --keep=obj/$(1) $(1).pas
	@if [ -f $(OBJ_DIR)/$(1).B ]; then \
		cat $(OBJ_DIR)/$(1).B >> $(OBJ_DIR)/$(1).a; \
		$(CHTYP) -t obj $(OBJ_DIR)/$(1).a; \
	fi
endef

# ── Compiled objects ──────────────────────────────────────────────────────────
OBJS := \
	$(OBJ_DIR)/pcommon.a \
	$(OBJ_DIR)/cgi.a     \
	$(OBJ_DIR)/cgc.a     \
	$(OBJ_DIR)/objout.a  \
	$(OBJ_DIR)/native.a  \
	$(OBJ_DIR)/gen.a     \
	$(OBJ_DIR)/dag.a     \
	$(OBJ_DIR)/scanner.a \
	$(OBJ_DIR)/symbols.a \
	$(OBJ_DIR)/call.a    \
	$(OBJ_DIR)/parser.a  \
	$(OBJ_DIR)/pascal.a

.PHONY: all install clean setup

all: setup $(BIN_OUT)

# ── Setup: set file types on companion files ──────────────────────────────────
setup:
	@for f in $(SRC_DIR)/*.asm $(SRC_DIR)/*.macros; do \
		$(CHTYP) -t src -a 3 "$$f"; \
	done
	@$(CHTYP) -t src -a 5 $(SRC_DIR)/cgi.comments
	@$(CHTYP) -t src -a 3 $(SRC_DIR)/smac

# ── Link ──────────────────────────────────────────────────────────────────────
$(BIN_OUT): $(OBJS) | $(BIN_DIR)
	@echo "=== link pascal ==="
	cd $(SRC_DIR) && $(IIX) link \
		obj/pascal obj/call obj/parser obj/native obj/cgi obj/symbols \
		obj/scanner obj/dag obj/cgc obj/gen obj/objout obj/pcommon \
		keep=$(BIN_OUT)
	iix chtyp -t exe -a 0xDB01 $(BIN_OUT)

# ── Foundation ────────────────────────────────────────────────────────────────

$(OBJ_DIR)/pcommon.a: $(SRC_DIR)/pcommon.pas $(SRC_DIR)/pcommon.asm $(SRC_DIR)/pcommon.macros | $(OBJ_DIR)
	$(call compile-pascal,pcommon)

$(OBJ_DIR)/cgi.a: $(SRC_DIR)/cgi.pas $(SRC_DIR)/cgi.asm $(SRC_DIR)/cgi.comments \
    $(OBJ_DIR)/pcommon.a | $(OBJ_DIR)
	$(call compile-pascal,cgi)

$(OBJ_DIR)/cgc.a: $(SRC_DIR)/cgc.pas $(SRC_DIR)/cgc.asm $(SRC_DIR)/cgc.macros \
    $(OBJ_DIR)/pcommon.a $(OBJ_DIR)/cgi.a | $(OBJ_DIR)
	$(call compile-pascal,cgc)

$(OBJ_DIR)/objout.a: $(SRC_DIR)/objout.pas $(SRC_DIR)/objout.asm $(SRC_DIR)/objout.macros \
    $(OBJ_DIR)/pcommon.a $(OBJ_DIR)/cgi.a $(OBJ_DIR)/cgc.a | $(OBJ_DIR)
	$(call compile-pascal,objout)

$(OBJ_DIR)/native.a: $(SRC_DIR)/native.pas $(SRC_DIR)/native.asm $(SRC_DIR)/native.macros \
    $(OBJ_DIR)/pcommon.a $(OBJ_DIR)/cgi.a $(OBJ_DIR)/cgc.a $(OBJ_DIR)/objout.a | $(OBJ_DIR)
	$(call compile-pascal,native)

$(OBJ_DIR)/gen.a: $(SRC_DIR)/gen.pas \
    $(OBJ_DIR)/pcommon.a $(OBJ_DIR)/cgi.a $(OBJ_DIR)/native.a | $(OBJ_DIR)
	$(call compile-pascal,gen)

$(OBJ_DIR)/dag.a: $(SRC_DIR)/dag.pas $(SRC_DIR)/dag.asm $(SRC_DIR)/dag.macros \
    $(OBJ_DIR)/pcommon.a $(OBJ_DIR)/cgi.a $(OBJ_DIR)/native.a $(OBJ_DIR)/gen.a | $(OBJ_DIR)
	$(call compile-pascal,dag)

# ── Scanner-layer ─────────────────────────────────────────────────────────────

$(OBJ_DIR)/scanner.a: $(SRC_DIR)/scanner.pas $(SRC_DIR)/scanner.asm $(SRC_DIR)/scanner.macros \
    $(OBJ_DIR)/pcommon.a $(OBJ_DIR)/cgi.a | $(OBJ_DIR)
	$(call compile-pascal,scanner)

$(OBJ_DIR)/symbols.a: $(SRC_DIR)/symbols.pas $(SRC_DIR)/symbols.asm $(SRC_DIR)/symbols.macros \
    $(OBJ_DIR)/pcommon.a $(OBJ_DIR)/cgi.a $(OBJ_DIR)/scanner.a | $(OBJ_DIR)
	$(call compile-pascal,symbols)

$(OBJ_DIR)/call.a: $(SRC_DIR)/call.pas \
    $(OBJ_DIR)/pcommon.a $(OBJ_DIR)/cgi.a $(OBJ_DIR)/cgc.a \
    $(OBJ_DIR)/scanner.a $(OBJ_DIR)/symbols.a | $(OBJ_DIR)
	$(call compile-pascal,call)

$(OBJ_DIR)/parser.a: $(SRC_DIR)/parser.pas \
    $(OBJ_DIR)/pcommon.a $(OBJ_DIR)/cgi.a $(OBJ_DIR)/cgc.a \
    $(OBJ_DIR)/scanner.a $(OBJ_DIR)/symbols.a $(OBJ_DIR)/call.a | $(OBJ_DIR)
	$(call compile-pascal,parser)

# ── Main entry point ──────────────────────────────────────────────────────────

$(OBJ_DIR)/pascal.a: $(SRC_DIR)/pascal.pas \
    $(OBJ_DIR)/pcommon.a $(OBJ_DIR)/cgi.a $(OBJ_DIR)/cgc.a \
    $(OBJ_DIR)/scanner.a $(OBJ_DIR)/symbols.a $(OBJ_DIR)/call.a $(OBJ_DIR)/parser.a | $(OBJ_DIR)
	$(call compile-pascal,pascal)

# ── Directories ───────────────────────────────────────────────────────────────

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# ── Install ───────────────────────────────────────────────────────────────────
install: all
	@echo "--- install pascal -> $(LANGUAGES_DIR)/pascal ---"
	$(INSTALL_SH) $(BIN_OUT) $(LANGUAGES_DIR)/pascal $(GG_ROOT)

# ── Clean ─────────────────────────────────────────────────────────────────────
clean:
	rm -f $(OBJ_DIR)/*.a $(OBJ_DIR)/*.A $(OBJ_DIR)/*.B $(OBJ_DIR)/*.int $(OBJ_DIR)/*.root
	rm -f $(BIN_OUT)
	@echo "Clean complete."
