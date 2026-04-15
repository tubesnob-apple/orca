#!/bin/sh
#
# install.sh — backup-and-install for GoldenGate binaries
#
# Usage: install.sh <source> <destination> <gg_root>
#
# Before copying <source> to <destination>, backs up the existing file at
# <destination> to $TMPDIR/goldenGate/<relative_path>/<filename>_YYYYMMDDHHMMSS
# where <relative_path> is the path from <gg_root> to the destination directory.
#
# Example:
#   install.sh bin_obj/cc /Library/GoldenGate/Languages/cc /Library/GoldenGate
#   → backs up to $TMPDIR/goldenGate/Languages/cc_20260409170300
#   → then copies bin_obj/cc to /Library/GoldenGate/Languages/cc

set -e

SRC="$1"
DEST="$2"
GG_ROOT="$3"

if [ -z "$SRC" ] || [ -z "$DEST" ] || [ -z "$GG_ROOT" ]; then
    echo "Usage: install.sh <source> <destination> <gg_root>" >&2
    exit 1
fi

BACKUP_BASE="${TMPDIR:-/tmp}/goldenGate"
TIMESTAMP=$(date +%Y%m%d%H%M%S)
FILENAME=$(basename "$DEST")

# Compute relative directory from GG_ROOT to destination
DEST_DIR=$(dirname "$DEST")
REL_DIR="${DEST_DIR#$GG_ROOT}"
REL_DIR="${REL_DIR#/}"

BACKUP_DIR="$BACKUP_BASE/$REL_DIR"

# Backup existing file if it exists
if [ -f "$DEST" ]; then
    mkdir -p "$BACKUP_DIR"
    BACKUP_FILE="$BACKUP_DIR/${FILENAME}_${TIMESTAMP}"
    cp "$DEST" "$BACKUP_FILE"
    echo "  Backed up $DEST -> $BACKUP_FILE"
fi

# Install (create destination directory if it doesn't exist — makes this
# usable against a fresh staging tree as well as an existing GoldenGate root)
mkdir -p "$DEST_DIR"
cp "$SRC" "$DEST"
echo "  Installed $SRC -> $DEST"
