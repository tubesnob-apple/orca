# clinker/tests/Makefile — byte-diff harness for clinker against iix link
#
# For each test case:
#   1. Assemble / compile the inputs under obj/<case>/
#   2. Link the same inputs twice: once with `iix link`, once with clinker
#   3. Byte-compare the outputs; report PASS/FAIL
#
# Targets:
#   make              - run every test case; exits non-zero on any FAIL
#   make test-<case>  - run a single test case
#   make clean        - wipe build outputs
#
# Test cases are defined in cases/<name>/ (see cases/minimal/ for the shape).
#
# Conventions:
#   - Inputs under cases/<name>/inputs/ (source files).
#   - Per-case build artifacts go under obj/<name>/.
#   - Linked outputs go under out/<name>.{iix,clinker}.
#   - Each test case defines INPUT_TARGETS (what to feed the linker, no
#     extension) and LIBS (extra lib= args). See per-case sections below.
#
# Both linkers are invoked with -X to disable ExpressLoad emission. The
# reference linker's default emits ExpressLoad; clinker does not implement
# it today, so emission would force byte divergence unrelated to any real
# linker semantics. (linker.asm's recognised flag is `-X`; `+X` is silently
# ignored by the reference linker. Our clinker accepts both spellings.)

SHELL := /bin/bash

MAKEFILE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
REPO_ROOT    := $(abspath $(MAKEFILE_DIR)../..)
CLINKER      := $(abspath $(MAKEFILE_DIR)../goldengate/clinker)

OBJ := $(MAKEFILE_DIR)obj
OUT := $(MAKEFILE_DIR)out

# ── Test case registry ──────────────────────────────────────────────────────
TESTS := minimal

.PHONY: all clean $(TESTS:%=test-%) help
.DEFAULT_GOAL := all

all: $(TESTS:%=test-%)
	@echo ""
	@echo "All $(words $(TESTS)) test(s) complete."

help:
	@echo "Test cases: $(TESTS)"
	@echo "Run a single case:  make test-<name>"
	@echo "Example:            make test-minimal"

clean:
	rm -rf $(OBJ) $(OUT)

# ── Shared compare macro ────────────────────────────────────────────────────
#
# compare(case_name) — runs after both out/<case>.iix and out/<case>.clinker
# exist. Byte-compares them; on mismatch, emits hex diffs and exits 1.
define compare
	@echo ""
	@echo "=== compare $(1) ==="
	@if cmp -s $(OUT)/$(1).iix $(OUT)/$(1).clinker; then \
	  sz=$$(wc -c < $(OUT)/$(1).iix | tr -d ' '); \
	  echo "PASS $(1) ($${sz} bytes)"; \
	else \
	  echo "FAIL $(1): outputs differ"; \
	  ref_sz=$$(wc -c < $(OUT)/$(1).iix | tr -d ' '); \
	  our_sz=$$(wc -c < $(OUT)/$(1).clinker | tr -d ' '); \
	  echo "  iix  : $${ref_sz} bytes"; \
	  echo "  clink: $${our_sz} bytes"; \
	  xxd $(OUT)/$(1).iix     > $(OUT)/$(1).iix.hex; \
	  xxd $(OUT)/$(1).clinker > $(OUT)/$(1).clinker.hex; \
	  echo "  --- first differing bytes ---"; \
	  diff -u $(OUT)/$(1).iix.hex $(OUT)/$(1).clinker.hex | head -40 || true; \
	  exit 1; \
	fi
endef

# ─── minimal: single code segment, CONST records only, no libs ──────────────
#
# Assembles cases/minimal/inputs/minimal.asm with ORCA/M. The assembler emits
# minimal.ROOT (a complete OMF OBJ for our single-segment source). We rename
# it to minimal.a under obj/minimal/ so clinker doesn't auto-pick up a
# sibling .ROOT (both linkers should see exactly one input file).

MINIMAL_OBJ := $(OBJ)/minimal
MINIMAL_SRC := $(MAKEFILE_DIR)cases/minimal/inputs/minimal.asm

$(MINIMAL_OBJ)/minimal.a: $(MINIMAL_SRC)
	@mkdir -p $(MINIMAL_OBJ)
	@cp $(MINIMAL_SRC) $(MINIMAL_OBJ)/minimal.asm
	@iix chtyp -l asm $(MINIMAL_OBJ)/minimal.asm
	cd $(MINIMAL_OBJ) && iix assemble minimal.asm >/dev/null
	@mv $(MINIMAL_OBJ)/minimal.ROOT $(MINIMAL_OBJ)/minimal.a
	@iix chtyp -t obj $(MINIMAL_OBJ)/minimal.a

$(OUT)/minimal.iix: $(MINIMAL_OBJ)/minimal.a
	@mkdir -p $(OUT)
	@echo "--- iix link minimal ---"
	cd $(MINIMAL_OBJ) && iix link -X minimal keep=$(OUT)/minimal.iix

$(OUT)/minimal.clinker: $(MINIMAL_OBJ)/minimal.a
	@mkdir -p $(OUT)
	@echo "--- clinker minimal ---"
	cd $(MINIMAL_OBJ) && iix $(CLINKER) -X minimal keep=$(OUT)/minimal.clinker

test-minimal: $(OUT)/minimal.iix $(OUT)/minimal.clinker
	$(call compare,minimal)
