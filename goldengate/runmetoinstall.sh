#!/bin/sh
#
# runmetoinstall.sh — drop ORCA toolchain files into a GoldenGate install.
#
# This script ships inside the ORCA-GoldenGate-<version>.zip package. Unzip
# the package anywhere, cd into the extracted directory, and run:
#
#     ./runmetoinstall.sh              # auto-detect target, install
#     ./runmetoinstall.sh --dry-run    # show what would happen, change nothing
#     ./runmetoinstall.sh --target /path/to/GoldenGate
#
# It:
#   1. Detects host OS (macOS / Linux / Windows-MSYS2)
#   2. Locates the GoldenGate install root (asks iix if it's in PATH, else
#      $GOLDEN_GATE, else per-OS defaults)
#   3. Copies every file alongside the script into the matching path under
#      the target root
#   4. Sets ProDOS file type metadata (FinderInfo) on each binary/library so
#      iix recognises the file types correctly
#
# Header files under Libraries/ORCACDefs/ need no metadata — GoldenGate infers
# $B0/SRC/$0008 from the .h extension.

set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
TARGET=""
DRY_RUN=0

while [ $# -gt 0 ]; do
    case "$1" in
        --dry-run) DRY_RUN=1 ;;
        --target)
            [ -z "$2" ] && { echo "--target requires a path" >&2; exit 1; }
            TARGET="$2"; shift ;;
        -h|--help)
            sed -n '3,27p' "$0"; exit 0 ;;
        *)
            echo "unknown option: $1" >&2; exit 1 ;;
    esac
    shift
done

# ── OS detection ─────────────────────────────────────────────────────────────
case "$(uname -s 2>/dev/null || echo Windows)" in
    Darwin)               OS=macos ;;
    Linux)                OS=linux ;;
    MINGW*|MSYS*|CYGWIN*) OS=windows ;;
    *)                    OS=unknown ;;
esac

echo "OS detected: $OS"
if [ "$OS" = "unknown" ]; then
    echo "error: unsupported platform" >&2
    exit 1
fi

# ── Target detection ─────────────────────────────────────────────────────────
if [ -z "$TARGET" ] && command -v iix >/dev/null 2>&1; then
    # iix --version prints a line like: "root: /Library/GoldenGate"
    TARGET=$(iix --version 2>/dev/null | awk '/^root:/ { print $2; exit }')
fi
if [ -z "$TARGET" ] && [ -n "$GOLDEN_GATE" ]; then
    TARGET="$GOLDEN_GATE"
fi
if [ -z "$TARGET" ]; then
    case "$OS" in
        macos)
            for p in /Library/GoldenGate "$HOME/Library/GoldenGate"; do
                [ -d "$p" ] && TARGET="$p" && break
            done ;;
        linux)
            for p in /usr/local/share/GoldenGate /usr/share/GoldenGate; do
                [ -d "$p" ] && TARGET="$p" && break
            done ;;
        windows)
            : # Windows has no standard default — require $GOLDEN_GATE or --target
            ;;
    esac
fi

if [ -z "$TARGET" ]; then
    echo "error: cannot locate GoldenGate install." >&2
    echo "       set \$GOLDEN_GATE or pass --target <path>." >&2
    exit 1
fi
if [ ! -d "$TARGET" ]; then
    echo "error: target '$TARGET' is not a directory." >&2
    exit 1
fi

echo "Target:      $TARGET"
[ "$DRY_RUN" = "1" ] && echo "(dry run — no files will be written)"
echo ""

# ── Manifest ─────────────────────────────────────────────────────────────────
# relative_path|prodos_type_hex|aux_type_hex_big_endian
#   $B5 = RUN executable
#   $B2 = LIB library
#   (.h files need no metadata — extension fallback handles them.)
MANIFEST="
Languages/cc|B5|0100
Languages/pascal|B5|DB01
Libraries/ORCALib|B2|0000
Libraries/PasLib|B2|0000
Libraries/SysLib|B2|0000
Libraries/SysFloat|B2|0000
Libraries/SysFPEFloat|B2|0000
lib/ORCALib|B2|0000
Utilities/linker|B5|DB01
Utilities/DumpObj|B5|DB01
Utilities/MakeLib|B5|0100
"

# ── Cross-platform ProDOS-type setter ────────────────────────────────────────
set_prodos_type() {
    FILE="$1"; TYPE_HEX="$2"; AUX_HEX="$3"
    AUXH=$(printf '%s' "$AUX_HEX" | cut -c1-2)
    AUXL=$(printf '%s' "$AUX_HEX" | cut -c3-4)
    # FinderInfo (32 bytes): p TT AH AL p d o s + 24 × 0x00
    FINFO="70${TYPE_HEX}${AUXH}${AUXL}70646F73000000000000000000000000000000000000000000000000"
    case "$OS" in
        macos)
            xattr -wx com.apple.FinderInfo "$FINFO" "$FILE"
            ;;
        linux)
            python3 -c '
import os, sys
os.setxattr(sys.argv[1], "user.com.apple.FinderInfo", bytes.fromhex(sys.argv[2]))
' "$FILE" "$FINFO"
            ;;
        windows)
            # 60-byte AFP_Info: magic + version + fileid + backup_date + finder_info(32)
            #                 + prodos_type(2 LE) + prodos_aux(4 LE) + reserved(6)
            AFP_PREFIX="41465000000001000000000000000000"
            AFP_SUFFIX="${TYPE_HEX}00${AUXL}${AUXH}0000000000000000"
            AFP="${AFP_PREFIX}${FINFO}${AFP_SUFFIX}"
            python3 -c '
import sys
open(sys.argv[1] + ":AFP_AfpInfo", "wb").write(bytes.fromhex(sys.argv[2]))
' "$FILE" "$AFP"
            ;;
    esac
}

# ── Install binaries + libraries with type metadata ──────────────────────────
installed=0
skipped=0
printf '%s\n' "$MANIFEST" | while IFS='|' read -r REL TYPE AUX; do
    [ -z "$REL" ] && continue
    SRC="$SCRIPT_DIR/$REL"
    DEST="$TARGET/$REL"
    DEST_DIR=$(dirname "$DEST")
    if [ ! -f "$SRC" ]; then
        printf '  skip  (not in package): %s\n' "$REL"
        continue
    fi
    if [ "$DRY_RUN" = "1" ]; then
        printf '  would install: %-30s type=%s aux=%s\n' "$REL" "$TYPE" "$AUX"
        continue
    fi
    mkdir -p "$DEST_DIR"
    cp "$SRC" "$DEST"
    set_prodos_type "$DEST" "$TYPE" "$AUX"
    printf '  installed:     %-30s type=%s aux=%s\n' "$REL" "$TYPE" "$AUX"
done

# ── Install ORCACDefs headers (no metadata needed) ───────────────────────────
if [ -d "$SCRIPT_DIR/Libraries/ORCACDefs" ]; then
    COUNT=$(find "$SCRIPT_DIR/Libraries/ORCACDefs" -type f | wc -l | tr -d ' ')
    if [ "$DRY_RUN" = "1" ]; then
        printf '  would install: Libraries/ORCACDefs/  (%s header files)\n' "$COUNT"
    else
        mkdir -p "$TARGET/Libraries/ORCACDefs"
        cp -R "$SCRIPT_DIR/Libraries/ORCACDefs/." "$TARGET/Libraries/ORCACDefs/"
        printf '  installed:     Libraries/ORCACDefs/  (%s header files)\n' "$COUNT"
    fi
fi

echo ""
if [ "$DRY_RUN" = "1" ]; then
    echo "Dry run complete."
else
    echo "Install complete. Run 'iix --version' to verify."
fi
