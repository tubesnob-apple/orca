# goldengate/Makefile — build DumpOBJ using GoldenGate iix toolchain
#
# Targets:
#   make                — compile + link (local build)
#   make install        — compile + link + copy to GoldenGate Utilities/
#   make clean          — remove compiled objects and linked binary
#
# Build structure:
#   Single C source file (DumpOBJ.cc), compiled with iix compile, linked
#   with iix link. Compile runs from OBJ_DIR so the linker can find
#   both .a and .root files for the same stem.

MAKEFILE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
REPO_ROOT    := $(abspath $(MAKEFILE_DIR)/..)
SRC_DIR      := $(REPO_ROOT)
OBJ_DIR      := $(REPO_ROOT)/obj
BIN_DIR      ?= $(MAKEFILE_DIR)
INSTALL_SH   ?= $(MAKEFILE_DIR)/../../goldengate/install.sh

CC       := iix compile
LINK     := iix link

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

UTILITIES_DIR := $(GG_ROOT)/Utilities

BIN_OUT := $(BIN_DIR)/DumpObj

.PHONY: all install clean

all: $(BIN_OUT)

# ── Compile ───────────────────────────────────────────────────────────────────
$(OBJ_DIR)/dumpobj.a: $(SRC_DIR)/DumpOBJ.cc | $(OBJ_DIR)
	@echo "=== compile DumpOBJ.cc ==="
	cd $(OBJ_DIR) && $(CC) $(SRC_DIR)/DumpOBJ.cc

# ── Link ──────────────────────────────────────────────────────────────────────
$(BIN_OUT): $(OBJ_DIR)/dumpobj.a | $(BIN_DIR)
	@echo "=== link DumpObj ==="
	cd $(OBJ_DIR) && $(LINK) -o $(BIN_OUT) dumpobj
	iix chtyp -t exe -a 0xDB01 $(BIN_OUT)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# ── Install ───────────────────────────────────────────────────────────────────
install: all
	@echo "--- install DumpObj -> $(UTILITIES_DIR)/DumpObj ---"
	$(INSTALL_SH) $(BIN_OUT) $(UTILITIES_DIR)/DumpObj $(GG_ROOT)

# ── Clean ─────────────────────────────────────────────────────────────────────
clean:
	rm -rf $(OBJ_DIR)
	rm -f $(BIN_OUT)
	rm -f $(MAKEFILE_DIR)$(notdir $(BIN_OUT)) $(SRC_DIR)/$(notdir $(BIN_OUT))
	@echo "Clean complete."
