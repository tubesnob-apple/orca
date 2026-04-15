# goldengate/Makefile — build ORCA Linker using GoldenGate iix toolchain
#
# Targets:
#   make                — assemble + link (local build)
#   make install        — assemble + link + copy to GoldenGate Utilities/
#   make clean          — remove compiled objects and linked binary
#
# Build structure:
#   Nine assembly modules, each with a .asm + .mac pair.
#   All modules share the directpage include (extensionless — needs file type set).
#   Link order matters: linker util file pass1 pass2 seg symbol exp out.

MAKEFILE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
REPO_ROOT    := $(abspath $(MAKEFILE_DIR)/..)
SRC_DIR      := $(REPO_ROOT)
OBJ_DIR      := $(REPO_ROOT)/obj
BIN_DIR      ?= $(MAKEFILE_DIR)
INSTALL_SH   ?= $(MAKEFILE_DIR)/../../goldengate/install.sh

AS       := iix assemble
LINK     := iix link
CHTYP    := iix chtyp
ASFLAGS  := +T

# ── GoldenGate root ──────────────────────────────────────────────────────────
OS := $(shell uname -s 2>/dev/null || echo Windows)
ifeq ($(OS),Darwin)
_GG_DEFAULT := /Library/GoldenGate
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

BIN_OUT := $(BIN_DIR)/linker

# ── Modules ───────────────────────────────────────────────────────────────────
# Link order matters — matches original linkit script.
ASM_MODULES := linker util file pass1 pass2 seg symbol exp out

ASM_OBJS := $(foreach m,$(ASM_MODULES),$(OBJ_DIR)/$(m).A)

.PHONY: all install clean setup

all: setup $(BIN_OUT)

# ── Setup: set file type on extensionless directpage include ──────────────────
setup:
	@$(CHTYP) -t src -a 3 $(SRC_DIR)/directpage

# ── Link ──────────────────────────────────────────────────────────────────────
$(BIN_OUT): $(ASM_OBJS) | $(BIN_DIR)
	@echo "=== link linker ==="
	cd $(SRC_DIR) && $(LINK) \
		$(foreach m,$(ASM_MODULES),obj/$(m)) \
		keep=$(BIN_OUT)
	iix chtyp -t exe -a 0xDB01 $(BIN_OUT)

# ── Assembly rules ────────────────────────────────────────────────────────────
$(OBJ_DIR)/%.A: $(SRC_DIR)/%.asm $(SRC_DIR)/%.mac $(SRC_DIR)/directpage | $(OBJ_DIR)
	@echo "--- assemble $*.asm ---"
	cd $(SRC_DIR) && $(AS) $(ASFLAGS) $*.asm
	mv $(SRC_DIR)/$*.A $@
	-mv $(SRC_DIR)/$*.ROOT $(OBJ_DIR)/$*.ROOT
	$(CHTYP) -t obj $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# ── Install ───────────────────────────────────────────────────────────────────
# The linker is always invoked as `iix link`, which loads it from the
# hardcoded GS/OS path "16/linker" (prefix 16 = <GG_ROOT>/Languages). We
# install it as Languages/Linker (uppercase matches the upstream Byte Works
# naming convention; HFS+/APFS are case-insensitive so iix's lowercase
# lookup still resolves). We deliberately do NOT install into Utilities/ —
# `iix linker ...` is not supported; use `iix link` instead.
install: all
	@echo "--- install Linker -> $(LANGUAGES_DIR)/Linker ---"
	$(INSTALL_SH) $(BIN_OUT) $(LANGUAGES_DIR)/Linker $(GG_ROOT)

# ── Clean ─────────────────────────────────────────────────────────────────────
clean:
	rm -f $(ASM_OBJS) $(foreach m,$(ASM_MODULES),$(OBJ_DIR)/$(m).ROOT)
	rm -f $(BIN_OUT)
	rm -f $(MAKEFILE_DIR)$(notdir $(BIN_OUT)) $(SRC_DIR)/$(notdir $(BIN_OUT))
	rm -f $(SRC_DIR)/*.A $(SRC_DIR)/*.ROOT
	@echo "Clean complete."
