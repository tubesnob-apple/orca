#!/bin/sh
#
# gen-version-asm.sh — generate a ~Version assembly segment
#
# Usage: gen-version-asm.sh <label> <version> <output_file>
#
# Produces a small ORCA/M assembly file that defines a ~Version segment
# containing a version string with build timestamp.

LABEL="$1"
VERSION="$2"
OUTPUT="$3"

if [ -z "$LABEL" ] || [ -z "$VERSION" ] || [ -z "$OUTPUT" ]; then
    echo "Usage: gen-version-asm.sh <label> <version> <output_file>" >&2
    exit 1
fi

TIMESTAMP=$(date "+%Y:%m:%d %H:%M:%S")

cat > "$OUTPUT" <<EOF
         keep  obj/version
Dummy    start
         end
~Version start
         dc    c'${LABEL} v${VERSION} ${TIMESTAMP}',i1'0'
         end
EOF
