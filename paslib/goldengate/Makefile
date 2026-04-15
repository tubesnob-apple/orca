# goldengate/Makefile — build PasLib using GoldenGate iix toolchain

MAKEFILE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
REPO_ROOT    := $(abspath $(MAKEFILE_DIR)/..)
SRC_DIR      := $(REPO_ROOT)
OBJ_DIR      := $(REPO_ROOT)/obj
BIN_DIR      ?= $(MAKEFILE_DIR)
INSTALL_SH   ?= $(MAKEFILE_DIR)/../../goldengate/install.sh
GEN_VERSION  := $(abspath $(MAKEFILE_DIR)/../../goldengate/gen-version-asm.sh)
VERSION      ?= 0.0.0
VERSION_LABEL := PasLib
AS       := iix assemble
MAKELIB  := iix makelib
CHTYP    := iix chtyp
ASFLAGS  := +T
OS := $(shell uname -s 2>/dev/null || echo Windows)
ifeq ($(OS),Darwin)
_GG_DEFAULT := /Library/GoldenGate
else ifeq ($(OS),Linux)
_GG_DEFAULT := /usr/local/share/GoldenGate
else
_GG_DEFAULT :=
endif
GG_ROOT := $(or $(GOLDEN_GATE),$(ORCA_ROOT),$(_GG_DEFAULT))
LIBRARIES_DIR := $(GG_ROOT)/Libraries
LIB_OUT := $(SRC_DIR)/paslib
BIN_OUT := $(BIN_DIR)/paslib

ASM_MODULES := nl objects version

.PHONY: all install clean setup FORCE

all: setup $(BIN_OUT)

FORCE:

setup:
	@$(CHTYP) -t src -a 3 $(SRC_DIR)/nl.smac

# ── Version segment (always regenerated) ──────────────────────────────────────
$(OBJ_DIR)/version.asm: FORCE | $(OBJ_DIR)
	@$(GEN_VERSION) "$(VERSION_LABEL)" "$(VERSION)" $@

$(OBJ_DIR)/version.A: $(OBJ_DIR)/version.asm | $(OBJ_DIR)
	@echo "--- assemble version.asm (generated) ---"
	cd $(SRC_DIR) && $(AS) $(ASFLAGS) obj/version.asm
	mv $(SRC_DIR)/version.A $@
	-mv $(SRC_DIR)/version.ROOT $(OBJ_DIR)/version.ROOT
	$(CHTYP) -t obj $@

# ── Assembly rules ────────────────────────────────────────────────────────────
$(OBJ_DIR)/%.A: $(SRC_DIR)/%.asm $(SRC_DIR)/%.macros | $(OBJ_DIR)
	@echo "--- assemble $*.asm ---"
	cd $(SRC_DIR) && $(AS) $(ASFLAGS) $*.asm
	mv $(SRC_DIR)/$*.A $@
	-mv $(SRC_DIR)/$*.ROOT $(OBJ_DIR)/$*.ROOT
	$(CHTYP) -t obj $@

$(LIB_OUT): $(foreach m,$(ASM_MODULES),$(OBJ_DIR)/$(m).A)
	@echo "=== makelib paslib ==="
	rm -f $(LIB_OUT)
	@for m in $(ASM_MODULES); do \
	  cd $(SRC_DIR) && $(MAKELIB) paslib +obj/$$m.a; \
	done

$(BIN_OUT): $(LIB_OUT) | $(BIN_DIR)
	@if [ "$(BIN_DIR)" != "$(MAKEFILE_DIR)" ] && [ "$(BIN_OUT)" != "$(LIB_OUT)" ]; then \
		cp -p $(LIB_OUT) $(BIN_OUT); \
	fi

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

install: all
	@echo "--- install paslib -> $(LIBRARIES_DIR)/PasLib ---"
	$(INSTALL_SH) $(LIB_OUT) $(LIBRARIES_DIR)/PasLib $(GG_ROOT)

clean:
	rm -f $(foreach m,$(ASM_MODULES),$(OBJ_DIR)/$(m).A $(OBJ_DIR)/$(m).ROOT)
	rm -f $(OBJ_DIR)/version.asm
	rm -f $(LIB_OUT) $(BIN_OUT)
	rm -f $(MAKEFILE_DIR)$(notdir $(BIN_OUT))
	rm -f $(SRC_DIR)/*.A $(SRC_DIR)/*.ROOT
	@echo "Clean complete."
