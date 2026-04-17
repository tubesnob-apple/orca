# goldengate/Makefile -- build clinker using GoldenGate iix toolchain
#
# Targets:
#   make              -- compile + link (local build)
#   make install      -- compile + link + copy to GoldenGate Languages/
#   make clean        -- remove objects and linked binary

MAKEFILE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
REPO_ROOT    := $(abspath $(MAKEFILE_DIR)/../..)
SRC_DIR      := $(abspath $(MAKEFILE_DIR)/..)
OBJ_DIR      := $(SRC_DIR)/obj
BIN_DIR      ?= $(MAKEFILE_DIR)
INSTALL_SH   ?= $(MAKEFILE_DIR)/../../goldengate/install.sh

CC       := iix compile
ifeq ($(USE_CLINKER),1)
LINK      := iix clinker
LINK_LIBS := lib=$(GG_ROOT)/Libraries/ORCALib lib=$(GG_ROOT)/Libraries/SysFloat lib=$(GG_ROOT)/Libraries/SysLib
else
LINK      := iix link
LINK_LIBS :=
endif

# -- GoldenGate root ----------------------------------------------------------
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

UTILITIES_DIR := $(GG_ROOT)/Utilities

BIN_OUT := $(BIN_DIR)/clinker

# -- Source modules -----------------------------------------------------------
C_MODULES := clinker symbol omf pass1 pass2 expr out gsplus

C_OBJS := $(foreach m,$(C_MODULES),$(OBJ_DIR)/$(m).a)

.PHONY: all install clean

all: $(BIN_OUT)

# -- Compile rules ------------------------------------------------------------
# ORCA/C compile: cd to SRC_DIR so #include "clinker.h" resolves, then
# move the resulting .a (and optional .root) into OBJ_DIR.
$(OBJ_DIR)/%.a: $(SRC_DIR)/%.c | $(OBJ_DIR)
	@echo "--- compile $*.c ---"
	cd $(SRC_DIR) && $(CC) $*.c
	mv $(SRC_DIR)/$*.a $@
	-mv $(SRC_DIR)/$*.root $(OBJ_DIR)/$*.root 2>/dev/null; true

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# -- Link ---------------------------------------------------------------------
$(BIN_OUT): $(C_OBJS) | $(BIN_DIR)
	@echo "=== link clinker ==="
	cd $(OBJ_DIR) && $(LINK) \
		$(foreach m,$(C_MODULES),$(m)) \
		$(LINK_LIBS) \
		keep=$(BIN_OUT)
	iix chtyp -t exe -a 0xDB01 $(BIN_OUT)

# -- Install ------------------------------------------------------------------
# clinker is invoked as `iix clinker ...` (not `iix link ...`).
# Install it to Languages/ alongside Linker.
install: all
	@echo "--- install clinker -> $(UTILITIES_DIR)/clinker ---"
	$(INSTALL_SH) $(BIN_OUT) $(UTILITIES_DIR)/clinker $(GG_ROOT)

# -- Clean --------------------------------------------------------------------
clean:
	rm -f $(C_OBJS)
	rm -f $(BIN_OUT)
	rm -f $(MAKEFILE_DIR)$(notdir $(BIN_OUT)) $(SRC_DIR)/$(notdir $(BIN_OUT))
	@echo "Clean complete."
