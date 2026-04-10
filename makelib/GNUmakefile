#
# GNUmakefile — Build iix MakeLib 2.0 using ORCA/C 2.2.2 via GoldenGate
#
# Usage (run from repo root):
#   make              # compile + link → MakeLib binary
#   make clean        # remove build artifacts
#   make install      # back up existing binary, then install to GoldenGate
#   make test         # run test suite against the installed binary
#
# Output: MakeLib  (OMF executable, ProDOS EXE type $B5)
#
# Original build (ORCA shell 'make' script):
#   cmpl +t +e +m makelib.cc
#   filetype makelib EXE $DB01
#   rename makelib MakeLib
#
# iix equivalent:
#   iix compile src/MakeLib.cc   → MakeLib.a + MakeLib.root
#   iix link -o MakeLib MakeLib  → executable
#
# Notes:
#   - NOT --gno: MakeLib is an ORCA Shell program, not GNO. No __GNO__ needed.
#   - +T (terminal on first error) is intentionally omitted: the source has
#     #pragma lint -1 (all lint = 0xFFFF), which causes lint warnings to abort
#     compilation with +T.  The warnings are pre-existing in the 1991 source.
#   - #pragma optimize 9 is present in source.
#   - PDosInt declared inline in MakeLib.cc; no shell.h needed.
#   - Standard headers only: stdlib.h stdio.h ctype.h string.h
#

CC          := iix compile
LINK        := iix link
OBJ_DIR     := obj
INSTALL_DIR := $(HOME)/Library/GoldenGate/Utilities

.PHONY: all clean install test

all: MakeLib

$(OBJ_DIR):
	mkdir -p $@

$(OBJ_DIR)/MakeLib.a: src/MakeLib.cc | $(OBJ_DIR)
	cd $(OBJ_DIR) && $(CC) ../src/MakeLib.cc; \
	test -f MakeLib.a || (echo "Compile failed: no output produced"; exit 1)

MakeLib: $(OBJ_DIR)/MakeLib.a
	cd $(OBJ_DIR) && $(LINK) -o ../MakeLib MakeLib

clean:
	rm -rf $(OBJ_DIR) MakeLib MakeLib.root

# Install with automatic backup.  The existing binary is timestamped and kept
# alongside the new one so a bad build can always be rolled back manually.
install: MakeLib
	@if [ -f $(INSTALL_DIR)/MakeLib ]; then \
		BACKUP="$(INSTALL_DIR)/MakeLib.bak.$$(date +%Y%m%d_%H%M%S)"; \
		cp $(INSTALL_DIR)/MakeLib "$$BACKUP"; \
		echo "Backed up existing binary to $$BACKUP"; \
	fi
	cp MakeLib $(INSTALL_DIR)/MakeLib
	@echo "Installed MakeLib → $(INSTALL_DIR)/MakeLib"

# Run the test suite against the currently installed binary (iix makelib).
# To test a freshly built binary: run 'make install' first, then 'make test'.
test:
	@chmod +x tests/run_tests.sh
	tests/run_tests.sh
