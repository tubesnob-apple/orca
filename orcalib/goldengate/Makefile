#
# goldengate/Makefile — Build ORCALib with the GoldenGate iix toolchain
#
# Supports two targets:
#   TARGET=standard  (default) — full ORCALib for ORCA/C outside GNO/ME
#   TARGET=gno                 — reduced ORCALib for use under GNO/ME 2.0.6
#                                (stdio, fcntl, setjmp, signal provided by GNO libc)
#
# Usage (from repo root or goldengate/):
#   make -f goldengate/Makefile                    # standard build
#   make -f goldengate/Makefile TARGET=gno         # GNO/ME build
#   make -f goldengate/Makefile TARGET=gno assert  # GNO assert.A (separate from liborca)
#   make -f goldengate/Makefile TARGET=gno install
#   make -f goldengate/Makefile TARGET=gno clean
#
# Source layout:
#   *.asm          — standard sources (all modules)
#   gno/*.asm      — GNO-specific overrides for: assert, cc, string, vars
#   gno/signal2.c  — GNO-specific raise() implementation (replaces signal.asm)
#
# GNO override mechanism:
#   Two pattern rules are defined for GNO builds. Make tries gno/%.asm first;
#   if that file does not exist, it falls back to the root %.asm.
#   Only assert, cc, string, and vars have gno/ overrides.
#
# Notes on iix toolchain:
#   - iix assemble ignores the path prefix in "keep obj/<module>" and always
#     writes <module>.A and <module>.ROOT to CWD. The Makefile moves them into
#     obj/<TARGET>/ after assembly to keep the source tree clean.
#   - Object files are stored in obj/standard/ and obj/gno/ to prevent cross-
#     contamination between targets (GNO uses different sources for 4 modules).
#   - GoldenGate sets ProDOS type $B0 for assembled files outside /tmp;
#     makelib requires $B1. Fix: patch the ProDOS metadata xattr after assembly.
#     The mechanism differs by host OS — see set-obj-xattr below.
#   - iix compile sets $B1 automatically — no xattr patch needed for .c files.
#   - makelib is called once per module to stay under the ~256-char CLI limit.
#     The library output is deleted first so each call appends cleanly.
#

TARGET  ?= standard
VERSION := 2.2.7

MAKEFILE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
REPO_ROOT    := $(abspath $(MAKEFILE_DIR)/..)
SRC_DIR      := $(REPO_ROOT)
OBJ_DIR      := $(REPO_ROOT)/obj/$(TARGET)
BIN_DIR      ?= $(MAKEFILE_DIR)

# ── GoldenGate root — platform-aware ──────────────────────────────────────────
# Respects $GOLDEN_GATE / $ORCA_ROOT first, then falls back to platform default.
#   macOS:   ~/Library/GoldenGate  (installer default)
#   Linux:   /usr/local/share/GoldenGate  (typical system install)
#   Windows: no default — $GOLDEN_GATE must be set
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
$(error GoldenGate root not found. Set $$GOLDEN_GATE or $$ORCA_ROOT to the GoldenGate installation directory.)
endif

AS           := iix assemble
CC           := iix --gno compile
MAKELIB      := iix makelib
CHTYP        := iix chtyp
ASFLAGS      := +T

# ── Target-specific configuration ─────────────────────────────────────────────

ifeq ($(TARGET),standard)

# Full module set — matches original ORCA make script order
ASM_MODULES  := vars assert cc setjmp ctype string stdlib time signal toolglue orca fcntl stdio int64 locale uchar version
LIB_OUT      := $(SRC_DIR)/orcalib
INSTALL_DIR  := $(GG_ROOT)/Libraries/ORCALib
VERSION_LABEL := ORCALib

else ifeq ($(TARGET),gno)

# GNO module set — stdio/fcntl/setjmp/signal.asm excluded (GNO libc provides them)
# assert excluded from liborca (used separately by libc)
ASM_MODULES  := vars cc ctype string stdlib time toolglue orca int64 locale uchar version
LIB_OUT      := $(SRC_DIR)/liborca
INSTALL_DIR  := $(GG_ROOT)/lib/ORCALib
VERSION_LABEL := ORCALib(GNO)

else
$(error Unknown TARGET "$(TARGET)". Use TARGET=standard or TARGET=gno)
endif

ASM_OBJS := $(foreach m,$(ASM_MODULES),$(OBJ_DIR)/$(m).A)

# ── Default target ─────────────────────────────────────────────────────────────
.PHONY: all
all: $(LIB_OUT)
	@if [ "$(BIN_DIR)" != "$(MAKEFILE_DIR)" ]; then \
		mkdir -p $(BIN_DIR); \
		cp -p $(LIB_OUT) $(BIN_DIR)/$(notdir $(LIB_OUT)); \
	fi

# ── Library ────────────────────────────────────────────────────────────────────
# makelib is called once per module to stay under the ~256-char CLI limit.
# The library is deleted first so repeated calls append into a fresh archive.
ifeq ($(TARGET),gno)
$(LIB_OUT): $(ASM_OBJS) $(OBJ_DIR)/signal2.a
	@echo "=== makelib $(notdir $(LIB_OUT)) (TARGET=$(TARGET)) ==="
	rm -f $(LIB_OUT)
	@for m in $(ASM_MODULES); do \
	  cd $(SRC_DIR) && $(MAKELIB) $(notdir $(LIB_OUT)) +obj/$(TARGET)/$$m.a; \
	done
	cd $(SRC_DIR) && $(MAKELIB) $(notdir $(LIB_OUT)) +obj/$(TARGET)/signal2.a
else
$(LIB_OUT): $(ASM_OBJS)
	@echo "=== makelib $(notdir $(LIB_OUT)) (TARGET=$(TARGET)) ==="
	rm -f $(LIB_OUT)
	@for m in $(ASM_MODULES); do \
	  cd $(SRC_DIR) && $(MAKELIB) $(notdir $(LIB_OUT)) +obj/$(TARGET)/$$m.a; \
	done
endif

# ── Version module (generated, always rebuilt) ────────────────────────────────
# Produces obj/<TARGET>/version.asm with an embedded timestamp, then assembles
# it.  The FORCE dependency ensures the timestamp is refreshed on every build.
.PHONY: FORCE
FORCE:

$(OBJ_DIR)/version.asm: FORCE | $(OBJ_DIR)
	@printf '%s\n' \
	  '         keep  obj/version' \
	  'Dummy    start' \
	  '         end' \
	  '~Version start' \
	  "         dc    c'$(VERSION_LABEL) v$(VERSION) $$(date +%Y:%m:%d\ %H:%M:%S)',i1'0'" \
	  '         end' \
	  > $@

$(OBJ_DIR)/version.A: $(OBJ_DIR)/version.asm | $(OBJ_DIR)
	@echo "--- assemble version.asm (generated) ---"
	cd $(SRC_DIR) && $(AS) $(ASFLAGS) obj/$(TARGET)/version.asm
	mv $(SRC_DIR)/version.A $@
	-mv $(SRC_DIR)/version.ROOT $(OBJ_DIR)/version.ROOT
	$(CHTYP) -t OBJ $@

# ── Assembly rules ─────────────────────────────────────────────────────────────
# iix assemble ignores the "keep obj/<name>" path prefix and always writes
# <name>.A + <name>.ROOT to CWD. Move them into obj/<TARGET>/ after assembly.

ifeq ($(TARGET),gno)
# GNO: try gno/ override first; fall back to root if no gno/ version exists.
# Make uses the first pattern rule whose prerequisites are satisfiable.
$(OBJ_DIR)/%.A: $(SRC_DIR)/gno/%.asm $(SRC_DIR)/equates.asm | $(OBJ_DIR)
	@echo "--- assemble gno/$*.asm ---"
	cd $(SRC_DIR) && $(AS) $(ASFLAGS) gno/$*.asm
	mv $(SRC_DIR)/$*.A $@
	-mv $(SRC_DIR)/$*.ROOT $(OBJ_DIR)/$*.ROOT
	$(CHTYP) -t OBJ $@
endif

$(OBJ_DIR)/%.A: $(SRC_DIR)/%.asm $(SRC_DIR)/equates.asm | $(OBJ_DIR)
	@echo "--- assemble $*.asm ---"
	cd $(SRC_DIR) && $(AS) $(ASFLAGS) $*.asm
	mv $(SRC_DIR)/$*.A $@
	-mv $(SRC_DIR)/$*.ROOT $(OBJ_DIR)/$*.ROOT
	$(CHTYP) -t OBJ $@

# ── signal2.c (GNO only) ──────────────────────────────────────────────────────
# Replaces signal.asm: raise() calls the GNO kernel directly via Kkill/Kgetpid.
# iix compile sets $B1 automatically — no xattr patch needed.
# cd to OBJ_DIR so the output lands there (iix compile writes to CWD).
$(OBJ_DIR)/signal2.a: $(SRC_DIR)/gno/signal2.c | $(OBJ_DIR)
	@echo "--- compile gno/signal2.c ---"
	cd $(OBJ_DIR) && $(CC) $(SRC_DIR)/gno/signal2.c

# ── assert (GNO only — separate from liborca, used by libc) ───────────────────
.PHONY: assert
assert: $(OBJ_DIR)/assert.A

ifeq ($(TARGET),gno)
$(OBJ_DIR)/assert.A: $(SRC_DIR)/gno/assert.asm $(SRC_DIR)/equates.asm | $(OBJ_DIR)
	@echo "--- assemble gno/assert.asm ---"
	cd $(SRC_DIR) && $(AS) $(ASFLAGS) gno/assert.asm
	mv $(SRC_DIR)/assert.A $@
	-mv $(SRC_DIR)/assert.ROOT $(OBJ_DIR)/assert.ROOT
	$(CHTYP) -t OBJ $@
endif

# ── Create obj/<TARGET>/ if needed ────────────────────────────────────────────
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# ── Install ────────────────────────────────────────────────────────────────────
ifeq ($(TARGET),gno)
INSTALL_DEPS := $(LIB_OUT) $(OBJ_DIR)/assert.A
else
INSTALL_DEPS := $(LIB_OUT)
endif

.PHONY: install
INSTALL_SH   ?= $(MAKEFILE_DIR)/../../goldengate/install.sh

install: $(INSTALL_DEPS)
	@echo "--- install $(notdir $(LIB_OUT)) -> $(INSTALL_DIR) ---"
	$(INSTALL_SH) $(LIB_OUT) $(INSTALL_DIR) $(GG_ROOT)
ifeq ($(TARGET),gno)
	@echo "--- install assert.A -> $(GG_ROOT)/lib/assert.A ---"
	$(INSTALL_SH) $(OBJ_DIR)/assert.A $(GG_ROOT)/lib/assert.A $(GG_ROOT)
endif

# ── Clean ──────────────────────────────────────────────────────────────────────
.PHONY: clean
clean:
	rm -f $(ASM_OBJS) $(foreach m,$(ASM_MODULES),$(OBJ_DIR)/$(m).ROOT)
	rm -f $(OBJ_DIR)/assert.A $(OBJ_DIR)/assert.ROOT
	rm -f $(OBJ_DIR)/signal2.a $(OBJ_DIR)/signal2.root
	rm -f $(OBJ_DIR)/version.asm
	rm -f $(LIB_OUT)
	rm -f $(SRC_DIR)/*.A $(SRC_DIR)/*.ROOT
	@echo "Clean complete (TARGET=$(TARGET))."
