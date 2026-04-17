# goldengate/Makefile — build MakeLib 2.2.4 using GoldenGate iix toolchain
#
# Targets:
#   make                — compile + link (local build)
#   make install        — compile + link + copy to GoldenGate Utilities/
#   make test           — run test suite against the installed binary
#   make clean          — remove compiled objects and linked binary
#
# Build structure:
#   Single C source file (src/MakeLib.cc), compiled with iix compile, linked
#   with iix link. Must NOT use --gno (MakeLib is an ORCA Shell program).
#   Lint warnings are expected — the source has #pragma lint -1 (enables all).

MAKEFILE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
REPO_ROOT    := $(abspath $(MAKEFILE_DIR)/..)
SRC_DIR      := $(REPO_ROOT)/src
OBJ_DIR      := $(REPO_ROOT)/obj
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

BIN_OUT := $(BIN_DIR)/MakeLib

.PHONY: all install test clean

all: $(BIN_OUT)

# ── Compile ───────────────────────────────────────────────────────────────────
# iix compile writes output to CWD, so we cd to OBJ_DIR first.
# The compiler exits 1 on lint warnings despite producing valid output.
$(OBJ_DIR)/MakeLib.a: $(SRC_DIR)/MakeLib.cc | $(OBJ_DIR)
	@echo "=== compile MakeLib.cc ==="
	-cd $(OBJ_DIR) && $(CC) $(SRC_DIR)/MakeLib.cc
	@test -f $@ || { echo "ERROR: compile failed — no output"; exit 1; }

# ── Link ──────────────────────────────────────────────────────────────────────
$(BIN_OUT): $(OBJ_DIR)/MakeLib.a | $(BIN_DIR)
	@echo "=== link MakeLib ==="
	cd $(OBJ_DIR) && $(LINK) -o $(BIN_OUT) MakeLib $(LINK_LIBS)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# ── Install ───────────────────────────────────────────────────────────────────
install: all
	@if [ -f $(UTILITIES_DIR)/MakeLib ]; then \
		BACKUP="$(UTILITIES_DIR)/MakeLib.bak.$$(date +%Y%m%d_%H%M%S)"; \
		cp $(UTILITIES_DIR)/MakeLib "$$BACKUP"; \
		echo "Backed up existing binary to $$BACKUP"; \
	fi
	@echo "--- install MakeLib -> $(UTILITIES_DIR)/MakeLib ---"
	$(INSTALL_SH) $(BIN_OUT) $(UTILITIES_DIR)/MakeLib $(GG_ROOT)

# ── Test ──────────────────────────────────────────────────────────────────────
test:
	@chmod +x $(REPO_ROOT)/tests/run_tests.sh
	cd $(REPO_ROOT) && tests/run_tests.sh

# ── Clean ─────────────────────────────────────────────────────────────────────
clean:
	rm -rf $(OBJ_DIR)
	rm -f $(BIN_OUT)
	rm -f $(MAKEFILE_DIR)$(notdir $(BIN_OUT)) $(SRC_DIR)/$(notdir $(BIN_OUT))
	@echo "Clean complete."
