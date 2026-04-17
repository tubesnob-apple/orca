# goldengate/Makefile — top-level build orchestrator for the orca monorepo
#
# Targets:
#   make                  — build all components (binaries staged in goldengate/bin_obj/)
#   make install          — build + install everything into GoldenGate
#   make package          — build + install into a staging dir + zip it up to
#                           $TMPDIR/ORCA-GoldenGate-<version>-<timestamp>.zip
#                           (does not touch the real GoldenGate install)
#   make clean            — clean all components
#
#   make orcalib          — build orcalib only
#   make orcalib-install  — build + install orcalib only
#   make orcalib-clean    — clean orcalib only
#
#   make linker           — build linker only
#   make linker-install   — build + install linker only
#   make linker-clean     — clean linker only
#
#   make dumpobj           — build dumpobj only
#   make dumpobj-install   — build + install dumpobj only
#   make dumpobj-clean     — clean dumpobj only
#
#   make makelib           — build makelib only
#   make makelib-install   — build + install makelib only
#   make makelib-clean     — clean makelib only
#
#   make orca-pascal       — build orca-pascal only
#   make orca-pascal-install — build + install orca-pascal only
#   make orca-pascal-clean — clean orca-pascal only
#
#   make orca-c           — build orca-c only (requires orcalib installed)
#   make orca-c-install   — build + install orca-c only
#   make orca-c-clean     — clean orca-c only
#
# Binary staging:
#   All linked binaries and libraries are staged in goldengate/bin_obj/.
#   install targets copy from there into the GoldenGate installation.
#
# Version:
#   The base version lives in goldengate/VERSION (e.g. "2.2.7").
#   Each build appends a timestamp to produce a build version like
#   "2.2.7-20260409.143052". This is passed to all sub-builds and stamped
#   into orca-c/CCommon.pas before compilation.
#   To bump the base version: edit goldengate/VERSION, then rebuild.
#
# Build order:
#   1. orcalib  — runtime library (no dependencies)
#   2. linker       — link editor (no dependencies)
#   3. dumpobj      — OMF dump utility (no dependencies)
#   4. makelib      — library archive tool (no dependencies)
#   5. orca-pascal  — Pascal compiler (no dependencies on orcalib)
#   6. orca-c       — C compiler (requires orcalib installed)
#
# Requires: GoldenGate iix toolchain, GNU make 3.81+
#   macOS: brew install make (for gmake 4.x, though 3.81 works)
#   Set $GOLDEN_GATE if not installed in the default location.

MAKEFILE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
REPO_ROOT    := $(abspath $(MAKEFILE_DIR)/..)
BIN_DIR      := $(abspath $(MAKEFILE_DIR)/bin_obj)
INSTALL_SH   := $(abspath $(MAKEFILE_DIR)/install.sh)

# ── GoldenGate root (needed for install backups) ─────────────────────────────
OS := $(shell uname -s 2>/dev/null || echo Windows)
ifeq ($(OS),Darwin)
_GG_DEFAULT := /Library/GoldenGate
else ifeq ($(OS),Linux)
_GG_DEFAULT := /usr/local/share/GoldenGate
else
_GG_DEFAULT :=
endif
GG_ROOT := $(or $(GOLDEN_GATE),$(ORCA_ROOT),$(_GG_DEFAULT))

# ── Version ───────────────────────────────────────────────────────────────────
BASE_VERSION := $(shell cat $(MAKEFILE_DIR)/VERSION | tr -d '[:space:]')
ifeq ($(BASE_VERSION),)
$(error Cannot read version from goldengate/VERSION)
endif
BUILD_TIMESTAMP := $(shell date +%Y%m%d.%H%M%S)
VERSION         := $(BASE_VERSION)-$(BUILD_TIMESTAMP)

# Use gmake if available (macOS Homebrew), otherwise system make
GMAKE := $(shell command -v gmake 2>/dev/null || command -v make)

# Common args passed to all sub-makes
# Set USE_CLINKER=1 to link with iix clinker instead of iix link.
SUBMAKE_ARGS := BIN_DIR=$(BIN_DIR) VERSION=$(VERSION) INSTALL_SH=$(INSTALL_SH) GG_ROOT=$(GG_ROOT) USE_CLINKER=$(USE_CLINKER)

.PHONY: all install clean
.PHONY: orcalib orcalib-install orcalib-clean
.PHONY: linker linker-install linker-clean
.PHONY: dumpobj dumpobj-install dumpobj-clean
.PHONY: makelib makelib-install makelib-clean
.PHONY: paslib paslib-install paslib-clean
.PHONY: syslib syslib-install syslib-clean
.PHONY: sysfloat sysfloat-install sysfloat-clean
.PHONY: sysfpefloat sysfpefloat-install sysfpefloat-clean
.PHONY: orca-pascal orca-pascal-install orca-pascal-clean
.PHONY: orca-c orca-c-install orca-c-clean
.PHONY: clinker clinker-install clinker-clean
.PHONY: stamp-version

# ── Default: build everything ─────────────────────────────────────────────────

all: orcalib paslib syslib sysfloat sysfpefloat linker dumpobj makelib orca-pascal orca-c
	@echo "Build complete — version $(VERSION)"
	@echo "Binaries staged in $(BIN_DIR)/"

# ── Install everything ────────────────────────────────────────────────────────

install: orcalib-install paslib-install syslib-install sysfloat-install sysfpefloat-install linker-install dumpobj-install makelib-install orca-pascal-install orca-c-install
	@echo "Install complete — version $(VERSION)"

# ── Clean everything ──────────────────────────────────────────────────────────

clean: orcalib-clean paslib-clean syslib-clean sysfloat-clean sysfpefloat-clean linker-clean dumpobj-clean makelib-clean orca-pascal-clean orca-c-clean
	rm -rf $(BIN_DIR)

# ── Version stamping ──────────────────────────────────────────────────────────

stamp-version:
	@echo "Stamping version $(VERSION)"
	@sed -i.bak "s/versionStr = '.*';/versionStr = '$(VERSION)';/" \
		$(REPO_ROOT)/orca-c/CCommon.pas
	@rm -f $(REPO_ROOT)/orca-c/CCommon.pas.bak
	@sed -i.bak "s/writeln('ORCA\/Pascal .*');/writeln('ORCA\/Pascal $(VERSION)');/" \
		$(REPO_ROOT)/orca-pascal/pascal.pas
	@rm -f $(REPO_ROOT)/orca-pascal/pascal.pas.bak

# ── orcalib ───────────────────────────────────────────────────────────────────

orcalib:
	$(GMAKE) -C $(REPO_ROOT)/orcalib -f goldengate/Makefile $(SUBMAKE_ARGS)

orcalib-install:
	$(GMAKE) -C $(REPO_ROOT)/orcalib -f goldengate/Makefile $(SUBMAKE_ARGS) install

orcalib-clean:
	$(GMAKE) -C $(REPO_ROOT)/orcalib -f goldengate/Makefile clean

# ── paslib ────────────────────────────────────────────────────────────────────

paslib:
	$(GMAKE) -C $(REPO_ROOT)/paslib/goldengate $(SUBMAKE_ARGS)

paslib-install:
	$(GMAKE) -C $(REPO_ROOT)/paslib/goldengate $(SUBMAKE_ARGS) install

paslib-clean:
	$(GMAKE) -C $(REPO_ROOT)/paslib/goldengate $(SUBMAKE_ARGS) clean

# ── syslib ────────────────────────────────────────────────────────────────────

syslib:
	$(GMAKE) -C $(REPO_ROOT)/syslib/goldengate $(SUBMAKE_ARGS)

syslib-install:
	$(GMAKE) -C $(REPO_ROOT)/syslib/goldengate $(SUBMAKE_ARGS) install

syslib-clean:
	$(GMAKE) -C $(REPO_ROOT)/syslib/goldengate $(SUBMAKE_ARGS) clean

# ── sysfloat ──────────────────────────────────────────────────────────────────

sysfloat:
	$(GMAKE) -C $(REPO_ROOT)/sysfloat/goldengate $(SUBMAKE_ARGS)

sysfloat-install:
	$(GMAKE) -C $(REPO_ROOT)/sysfloat/goldengate $(SUBMAKE_ARGS) install

sysfloat-clean:
	$(GMAKE) -C $(REPO_ROOT)/sysfloat/goldengate $(SUBMAKE_ARGS) clean

# ── sysfpefloat ───────────────────────────────────────────────────────────────

sysfpefloat:
	$(GMAKE) -C $(REPO_ROOT)/sysfpefloat/goldengate $(SUBMAKE_ARGS)

sysfpefloat-install:
	$(GMAKE) -C $(REPO_ROOT)/sysfpefloat/goldengate $(SUBMAKE_ARGS) install

sysfpefloat-clean:
	$(GMAKE) -C $(REPO_ROOT)/sysfpefloat/goldengate $(SUBMAKE_ARGS) clean

# ── linker ────────────────────────────────────────────────────────────────────

linker:
	$(GMAKE) -C $(REPO_ROOT)/linker/goldengate $(SUBMAKE_ARGS)

linker-install:
	$(GMAKE) -C $(REPO_ROOT)/linker/goldengate $(SUBMAKE_ARGS) install

linker-clean:
	$(GMAKE) -C $(REPO_ROOT)/linker/goldengate $(SUBMAKE_ARGS) clean

# ── clinker ───────────────────────────────────────────────────────────────────

clinker:
	$(GMAKE) -C $(REPO_ROOT)/clinker/goldengate $(SUBMAKE_ARGS)

clinker-install:
	$(GMAKE) -C $(REPO_ROOT)/clinker/goldengate $(SUBMAKE_ARGS) install

clinker-clean:
	$(GMAKE) -C $(REPO_ROOT)/clinker/goldengate $(SUBMAKE_ARGS) clean

# ── dumpobj ───────────────────────────────────────────────────────────────────

dumpobj:
	$(GMAKE) -C $(REPO_ROOT)/dumpobj/goldengate $(SUBMAKE_ARGS)

dumpobj-install:
	$(GMAKE) -C $(REPO_ROOT)/dumpobj/goldengate $(SUBMAKE_ARGS) install

dumpobj-clean:
	$(GMAKE) -C $(REPO_ROOT)/dumpobj/goldengate $(SUBMAKE_ARGS) clean

# ── makelib ───────────────────────────────────────────────────────────────────

makelib:
	$(GMAKE) -C $(REPO_ROOT)/makelib/goldengate $(SUBMAKE_ARGS)

makelib-install:
	$(GMAKE) -C $(REPO_ROOT)/makelib/goldengate $(SUBMAKE_ARGS) install

makelib-clean:
	$(GMAKE) -C $(REPO_ROOT)/makelib/goldengate $(SUBMAKE_ARGS) clean

# ── orca-pascal ───────────────────────────────────────────────────────────────

orca-pascal: stamp-version
	$(GMAKE) -C $(REPO_ROOT)/orca-pascal/goldengate $(SUBMAKE_ARGS)

orca-pascal-install: stamp-version
	$(GMAKE) -C $(REPO_ROOT)/orca-pascal/goldengate $(SUBMAKE_ARGS) install

orca-pascal-clean:
	$(GMAKE) -C $(REPO_ROOT)/orca-pascal/goldengate $(SUBMAKE_ARGS) clean

# ── orca-c ────────────────────────────────────────────────────────────────────

orca-c: stamp-version
	$(GMAKE) -C $(REPO_ROOT)/orca-c/goldengate $(SUBMAKE_ARGS)

orca-c-install: stamp-version
	$(GMAKE) -C $(REPO_ROOT)/orca-c/goldengate $(SUBMAKE_ARGS) install

orca-c-clean:
	$(GMAKE) -C $(REPO_ROOT)/orca-c/goldengate $(SUBMAKE_ARGS) clean

# ── Setup (one-time after clone) ──────────────────────────────────────────────
.PHONY: setup
setup:
	$(GMAKE) -C $(REPO_ROOT)/orca-c/goldengate setup-tests

# ── Package (zip a full install tree, without touching the real GG root) ─────
#
# `make package` builds everything, installs into a disposable staging
# directory under $TMPDIR, and produces a zip at:
#
#     $TMPDIR/ORCA-GoldenGate-<base>-<timestamp>.zip
#
# e.g. /var/.../ORCA-GoldenGate-2.3.0-20260415.134102.zip
#
# The zip's top-level layout mirrors the GoldenGate installation
# (Languages/, Libraries/, Utilities/, lib/, ...), so recipients can drop
# its contents directly into their own GoldenGate root.
#
# The real /Library/GoldenGate is NOT touched by this target.
.PHONY: package
PACKAGE_TMP   := $(or $(TMPDIR),/tmp)
PACKAGE_STAGE := $(PACKAGE_TMP)/ORCA-GoldenGate-stage-$(BUILD_TIMESTAMP)
PACKAGE_ZIP   := $(PACKAGE_TMP)/ORCA-GoldenGate-$(VERSION).zip

package:
	@command -v zip >/dev/null 2>&1 || { echo "error: 'zip' command not found in PATH" >&2; exit 1; }
	@rm -rf "$(PACKAGE_STAGE)" "$(PACKAGE_ZIP)"
	@mkdir -p "$(PACKAGE_STAGE)"
	@echo "Staging full install tree under $(PACKAGE_STAGE)"
	@echo ""
	$(GMAKE) -f $(firstword $(MAKEFILE_LIST)) install \
	    GG_ROOT="$(PACKAGE_STAGE)" \
	    BASE_VERSION="$(BASE_VERSION)" \
	    BUILD_TIMESTAMP="$(BUILD_TIMESTAMP)"
	@cp $(MAKEFILE_DIR)runmetoinstall.sh "$(PACKAGE_STAGE)/runmetoinstall.sh"
	@chmod +x "$(PACKAGE_STAGE)/runmetoinstall.sh"
	@echo ""
	@echo "Creating zip: $(PACKAGE_ZIP)"
	@cd "$(PACKAGE_STAGE)" && zip -rq "$(PACKAGE_ZIP)" .
	@rm -rf "$(PACKAGE_STAGE)"
	@echo ""
	@echo "Package created:"
	@ls -lh "$(PACKAGE_ZIP)"
	@echo ""
	@echo "To deploy: unzip this file into your GoldenGate root directory."
