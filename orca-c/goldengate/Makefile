# goldengate/Makefile — build and install ORCA/C into a GoldenGate installation
#
# Targets:
#   make                 — compile + link to goldengate/cc (local, not installed)
#   make install         — compile + link + copy binary and headers to GoldenGate
#   make install-cc      — compile + link + copy binary only (skip headers, faster iteration)
#   make install-headers — copy ORCACDefs headers into GoldenGate Libraries/ORCACDefs/
#   make setup-tests     — set ProDOS file type metadata on extensionless test companion files
#   make clean           — remove compiled objects and local linked binary
#
# Requires: gmake 4.x — macOS: brew install make; Linux: system make is usually sufficient
#
# Notes:
#   setup-tests is required once after cloning.  Extensionless companion files
#   (CFILE1, LIBFILE2, etc.) in Tests/Spec.Conform/ have no file extension, so
#   GoldenGate cannot infer their ProDOS file type from the name alone.  The
#   metadata must be stored externally:
#     macOS   — com.apple.FinderInfo xattr (32 bytes)
#     Linux   — user.com.apple.FinderInfo xattr (same 32-byte format, user. prefix)
#     Windows — filename:AFP_AfpInfo NTFS alternate data stream (60-byte AFP_Info)

# GoldenGate root: honour $GOLDEN_GATE env var if set, else use the macOS
# per-user default.  Linux/Windows users should set GOLDEN_GATE; iix itself
# searches $GOLDEN_GATE, $HOME/Library/GoldenGate, /usr/local/share/GoldenGate,
# and /usr/share/GoldenGate in that order.
GOLDENGATE      := $(or $(GOLDEN_GATE),$(HOME)/Library/GoldenGate)
GG_ROOT         ?= $(GOLDENGATE)
ORCACDEFS_SRC   := ../ORCACDefs
ORCACDEFS_DST   := $(GOLDENGATE)/Libraries/ORCACDefs
BIN_DIR         ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
CC_LOCAL        := $(BIN_DIR)/cc
CC_INSTALLED    := $(GOLDENGATE)/Languages/cc
CC_BACKUP_DIR   := $(or $(TMPDIR),/tmp/)orca-c-cc-backup

# Use Homebrew gmake if available, otherwise fall back to system make
GMAKE := $(shell command -v gmake 2>/dev/null || command -v make)

CHTYP := iix chtyp

# Extensionless companion files in Tests/Spec.Conform/ that need ProDOS file type xattrs
SPEC_CONFORM_COMPANIONS := \
	../Tests/Spec.Conform/CFILE1 \
	../Tests/Spec.Conform/LIBFILE2 \
	../Tests/Spec.Conform/UFILE1 \
	../Tests/Spec.Conform/USERFILE2 \
	../Tests/Spec.Conform/SPC34021 \
	../Tests/Spec.Conform/SPC34022 \
	../Tests/Spec.Conform/SPC34023 \
	../Tests/Spec.Conform/SPC34024 \
	../Tests/Spec.Conform/SPC34025 \
	../Tests/Spec.Conform/SPC34026 \
	../Tests/Spec.Conform/SPC34027 \
	../Tests/Spec.Conform/SPC34028

.PHONY: all install install-cc install-headers setup-tests clean

all:
	$(GMAKE) -B -C .. -f GoldenGate.mk BIN_DIR=$(BIN_DIR) link

install: all install-cc install-headers

INSTALL_SH   ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../goldengate/install.sh)

install-cc: all
	$(INSTALL_SH) $(CC_LOCAL) $(CC_INSTALLED) $(GG_ROOT)

install-headers:
	@echo "Installing ORCACDefs headers to $(ORCACDEFS_DST)"
	@mkdir -p $(ORCACDEFS_DST)
	cp $(ORCACDEFS_SRC)/*.h $(ORCACDEFS_DST)/
	@for subdir in $$(find $(ORCACDEFS_SRC) -mindepth 1 -type d); do \
		reldir=$${subdir#$(ORCACDEFS_SRC)/}; \
		mkdir -p $(ORCACDEFS_DST)/$$reldir; \
		cp $$subdir/*.h $(ORCACDEFS_DST)/$$reldir/ 2>/dev/null || true; \
	done
	@echo "Done — $(shell find $(ORCACDEFS_SRC) -name '*.h' | wc -l | tr -d ' ') headers installed."

setup-tests:
	@echo "Setting ProDOS file type metadata on Spec.Conform companion files..."
	@for f in $(SPEC_CONFORM_COMPANIONS); do \
		$(CHTYP) -t SRC -a 8 "$$f"; \
	done
	@echo "Done."

clean:
	$(GMAKE) -C .. -f GoldenGate.mk BIN_DIR=$(BIN_DIR) clean
	rm -f $(CC_LOCAL)
	rm -f $(abspath $(dir $(lastword $(MAKEFILE_LIST))))/cc $(abspath $(dir $(lastword $(MAKEFILE_LIST))))/../cc
