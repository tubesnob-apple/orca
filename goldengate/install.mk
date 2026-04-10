# install.mk — shared install macro for all ORCA sub-Makefiles
#
# Include this file and use: $(call gg-install,<source>,<destination>)
# Requires GG_ROOT to be set by the including Makefile.

INSTALL_SH := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))install.sh

define gg-install
	$(INSTALL_SH) $(1) $(2) $(GG_ROOT)
endef
