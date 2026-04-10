# GoldenGate Platform Guide

This document captures all platform-specific nuances for building and testing ORCA/C
under the GoldenGate `iix` toolchain on macOS, Linux, and Windows. It is intended as
a reference for anyone (human or AI) working on this repo across platforms.

---

## What GoldenGate Is

GoldenGate is an Apple IIGS emulation layer that runs ORCA/M assembler, ORCA Pascal,
ORCA/C, and related Apple IIGS toolchain binaries natively on a host OS. It exposes
GS/OS system calls (file I/O, tool calls, memory management) to the emulated programs
via a translation layer.

The primary command is `iix`, which provides these built-ins:

```
iix compile    — invoke the appropriate language compiler based on file type
iix assemble   — invoke ORCA/M assembler
iix link       — invoke the ORCA/M linker
iix help       — show built-in help
iix man        — show man pages
```

GoldenGate is supported on macOS, Linux, Windows (via MSYS2/Git Bash), and Solaris.

---

## GoldenGate Installation Paths

`iix` searches for the GoldenGate root in this order:

| Priority | Location |
|---|---|
| 1 | `$GOLDEN_GATE` environment variable |
| 2 | `$HOME/Library/GoldenGate` (macOS per-user default) |
| 3 | `/Library/GoldenGate` (macOS system-wide) |
| 4 | `/usr/local/share/GoldenGate` (Linux typical) |
| 5 | `/usr/share/GoldenGate` (Linux system) |

`$ORCA_ROOT` is also recognized as an alias for `$GOLDEN_GATE`.

**macOS** users typically have GoldenGate at `~/Library/GoldenGate` (the installer default).

**Linux** users typically install to `/usr/local/share/GoldenGate` and must set
`$GOLDEN_GATE` if using a non-standard path.

**Windows** (MSYS2/Git Bash) users must set `$GOLDEN_GATE` pointing to the
GoldenGate root directory.

The `goldengate/Makefile` in this repo picks up `$GOLDEN_GATE` automatically:

```makefile
GOLDENGATE := $(or $(GOLDEN_GATE),$(HOME)/Library/GoldenGate)
```

---

## GoldenGate Root Directory Layout

```
$GOLDENGATE/
  Languages/        — compiler binaries (cc, pascal, asm, etc.)
  Libraries/
    ORCACDefs/      — C standard library headers (.h files)
  System/           — GS/OS system files
  bin/              — iix and other host-side tools
  ...
```

`make install` copies the compiled `cc` binary to `Languages/cc` and the headers to
`Libraries/ORCACDefs/`.

---

## ProDOS File Type Metadata — The Core Challenge

GoldenGate maps host filesystem files to Apple IIGS ProDOS files. Every file has a
**ProDOS file type** (1 byte, e.g. `$B0` = SRC) and an **aux type** (2 bytes, e.g.
`$0008` = ORCA/C language number). These determine which tool `iix compile` invokes.

| ProDOS Type | Aux Type | Meaning |
|---|---|---|
| `$B0` (SRC) | `$0005` | ORCA Pascal source |
| `$B0` (SRC) | `$0003` | ORCA/M Assembly source |
| `$B0` (SRC) | `$0008` | ORCA/C source |

### How metadata is stored per platform

The metadata format is a 32-byte Finder Info block encoding type and aux type in
bytes 0–7 as `p<TT><AH><AL>pdos` (generic encoding) or named type strings for
well-known types. ORCA/C source uses: `70 B0 00 08 70 64 6F 73 00 00 ... 00`
(that is: `p` `$B0` `$00` `$08` `p` `d` `o` `s` followed by 24 zero bytes).

#### macOS

Stored in the `com.apple.FinderInfo` extended attribute (32 bytes):

```bash
xattr -wx com.apple.FinderInfo \
  "70 B0 00 08 70 64 6F 73 00 00 00 00 00 00 00 00 \
   00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00" \
  filename
```

#### Linux

Stored in the `user.com.apple.FinderInfo` extended attribute (same 32-byte payload,
mandatory `user.` namespace prefix required by the Linux kernel):

```bash
python3 -c "
import os
os.setxattr('filename', 'user.com.apple.FinderInfo',
    bytes.fromhex('70B0000870646F73000000000000000000000000000000000000000000000000'))
"
```

The `attr` / `setfattr` packages can also set Linux xattrs, but `python3` with
`os.setxattr` is more universally available and handles binary data cleanly.

#### Windows (MSYS2 / Git Bash)

Stored in an NTFS alternate data stream named `filename:AFP_AfpInfo` as a 60-byte
`AFP_Info` structure:

```
Offset  Size  Field
0       4     magic = 0x00504641 → bytes 41 46 50 00
4       4     version = 0x00010000 → bytes 00 00 01 00
8       4     file_id = 0
12      4     backup_date = 0
16      32    finder_info (same 32-byte block as macOS/Linux)
48      2     prodos_file_type (redundant copy) = 0x00B0 → B0 00
50      4     prodos_aux_type (redundant copy) = 0x00000008 → 08 00 00 00
54      6     reserved = 00 00 00 00 00 00
```

Full 60-byte hex for ORCA/C source:
`4146500000000100000000000000000070B0000870646F73000000000000000000000000000000000000000000000000B000080000000000000000`

Set with Python:

```bash
python3 -c "
open(r'filename:AFP_AfpInfo','wb').write(
    bytes.fromhex('4146500000000100000000000000000070B0000870646F7300000000000000000000000000000000000000000000000000B000080000000000000000'))
"
```

### Extension-based fallback (all platforms)

If no metadata is found, GoldenGate falls back to inferring the ProDOS type from
the file extension:

| Extension(s) | FileType | AuxType | Tool invoked |
|---|---|---|---|
| `.c`, `.cc`, `.cpp`, `.h` | `$B0` | `$0008` | ORCA/C compiler |
| `.pas`, `.p` | `$B0` | `$0005` | ORCA Pascal compiler |
| `.asm`, `.s`, `.aii` | `$B0` | `$0003` | ORCA/M assembler |
| `.txt`, `.text` | `$04` | `$0000` | text |
| `.rez`, `.r` | `$B0` | `$0015` | Rez |

**Implication:** `.pas` and `.asm` source files do NOT need explicit metadata set on
Linux or Windows — the extension fallback handles them. On macOS, `git clone` strips
xattrs, so `GoldenGate.mk` sets them explicitly as part of the build. The macros in
`GoldenGate.mk` are no-ops on non-macOS platforms.

**Files without extensions** (e.g. `CFILE1`, `LIBFILE2`) get no fallback and MUST
have metadata set explicitly on all platforms. See `make setup-tests` below.

---

## `make setup-tests` — Extensionless Companion Files

`Tests/Spec.Conform/` contains extensionless companion files (`CFILE1`, `LIBFILE2`,
`UFILE1`, `USERFILE2`, `SPC34021`–`SPC34028`) that are referenced by `#include
<cfile1>` style directives. GoldenGate cannot infer their ProDOS type from the name
alone. `git clone` strips all metadata. Run this once after cloning:

```bash
cd goldengate && make setup-tests
```

The target auto-detects the host OS and sets metadata appropriately:
- **macOS** → `xattr -wx com.apple.FinderInfo`
- **Linux** → `python3 os.setxattr` with `user.com.apple.FinderInfo`
- **Windows** → `python3` writing `filename:AFP_AfpInfo` NTFS alt-stream

Without this, `Tests/Spec.Conform/SPC3.4.0.1.CC` and others that use `#include
<cfile1>` will fail with "file not found" errors.

---

## Building ORCA/C

### Prerequisites

| Platform | Requirement |
|---|---|
| macOS | GoldenGate installed, `iix` in PATH, gmake 4.x (`brew install make`) |
| Linux | GoldenGate installed, `iix` in PATH, `make` ≥ 4.x (system make usually ok), `python3` for `setup-tests` |
| Windows | GoldenGate installed, MSYS2 with `iix` in PATH, `python3` for `setup-tests`, `$GOLDEN_GATE` set |

### Build commands (run from `goldengate/`)

```bash
make install         # Full build + install binary + install headers  ← USE THIS
make install-cc      # Build + install binary only (skips headers — faster iteration)
make install-headers # Copy ORCACDefs/*.h to GoldenGate Libraries/ORCACDefs/
make setup-tests     # Set ProDOS metadata on extensionless Spec.Conform companion files
make clean           # Remove obj/ compiled objects and local cc binary
```

> **CRITICAL:** After ANY change to `.pas` or `.asm` source files, run `make install`
> before testing with `iix compile`. The Makefile always does a full unconditional
> rebuild (`-B`) followed by install. Never test until `make install` completes
> successfully.

### `$GOLDEN_GATE` on Linux/Windows

If GoldenGate is not at `~/Library/GoldenGate`, set the environment variable:

```bash
export GOLDEN_GATE=/path/to/GoldenGate
make install
```

### Compiler binary backup

`make install-cc` automatically backs up the existing installed binary before
overwriting it. Backups go to `$TMPDIR/orca-c-cc-backup/` (macOS) or
`/tmp/orca-c-cc-backup/` (Linux) with a timestamp suffix.

---

## `GoldenGate.mk` — Platform-Aware Build

`GoldenGate.mk` (at the repo root, called from `goldengate/Makefile`) handles the
compile and link steps. It detects the host OS at make-time:

```makefile
OS := $(shell uname -s 2>/dev/null || echo Windows)

ifeq ($(OS),Darwin)
define set-pascal-xattr
    xattr -wx com.apple.FinderInfo "$(PASCAL_TYPE)" $(1)
endef
define set-asm-xattr
    xattr -wx com.apple.FinderInfo "$(ASM_TYPE)" $(1)
endef
else
# Linux/Windows: extension fallback handles .pas/.asm — no-ops
define set-pascal-xattr
endef
define set-asm-xattr
endef
endif
```

All `.pas` and `.asm` source files have their ProDOS type set via these macros before
`iix compile` is invoked. On macOS this is required (git strips xattrs on clone). On
Linux/Windows it is a no-op because the extension fallback handles it automatically.

---

## Known GoldenGate Quirks

### 1. `GetFileInfoGS` returns SRC for nonexistent ORCACDefs paths

On GoldenGate, calling `GetFileInfoGS` (or `GetFileType`) on a path under
`13:ORCACDefs:` may return `$B0` (SRC) even if the file does not exist. This is a
GoldenGate behavior artifact.

**Impact:** In `Scanner.pas`, `GetLibraryName` must check user `libPathList` entries
BEFORE falling back to `13:ORCACDefs:`. Otherwise a nonexistent ORCACDefs path is
reported as found. This is implemented in the current codebase.

### 2. `iix compile` info-string flags

`iix compile` does not accept standard C compiler flags (`-D`, `-I`, `-L`, `-P`).
All compiler parameters go through the GS/OS LInfo info-string:

| Info-string flag | Effect |
|---|---|
| `-d"NAME=VALUE"` | Define preprocessor macro |
| `-i"path"` | Add to `"quoted"` include search path |
| `-l"path"` | Add to `<angle-bracket>` library include search path |
| `-p"file"` | Set custom prefix file |

Use `#pragma` directives in source instead of command-line flags where possible:
- `#pragma path "path"` — adds to quoted include path
- `#pragma libpath "path"` — adds to angle-bracket include path
- `#pragma keep "name"` — sets output file name

### 3. GS/OS relative path `":"`

In GS/OS notation, `":"` means the current working prefix directory. Use
`#pragma libpath ":"` to search the current directory for angle-bracket includes.
This is how `Tests/Spec.Conform/SPC3.4.0.1.CC` finds its companion files when
compiled from within `Tests/Spec.Conform/`.

### 4. ORCA/C language number is 8

When GoldenGate reads a file's aux type to determine the language, ORCA/C is language
number 8 (aux type `$0008`). The Finder Info bytes for an ORCA/C source file are:
`70 B0 00 08 70 64 6F 73 ...` (the `00 08` at bytes 2–3 is the aux type big-endian).
This is required for extensionless companion files — aux type `$0000` causes a
"you cannot change languages" error.

---

## CGC.macros — Critical File Integrity Note

`CGC.macros` must be the full **513-line version**. The file contains all macros used
by `CGC.asm`, including `lla` at line 357 (after the `return` macro, before `ph8`).

**Do not truncate or reorder `CGC.macros`.** A previous editing error truncated it to
307 lines and moved `lla` to the top of the file. This breaks the build with
`CGC.asm: Unidentified Operation` — ORCA/M fails to load the macro correctly when it
appears first in the file.

If the build fails with `CGC.asm: Unidentified Operation`, verify:

```bash
wc -l CGC.macros          # must be 513
grep -n "^&l lla " CGC.macros  # must be line 357
```

To restore if corrupted:

```bash
git show 0cca38e:CGC.macros > CGC.macros
```

---

## Spec.Conform Tests — Setup and Execution

The `Tests/Spec.Conform/` tests must be run **from within the directory** because
`#pragma libpath ":"` resolves relative to the current GS/OS prefix:

```bash
cd Tests/Spec.Conform
for f in *.CC; do
  result=$(iix compile "$f" 2>&1)
  if ! echo "$result" | grep -q "0 errors found"; then echo "FAIL: $f"; fi
done
cd ../..
```

Two prerequisites:
1. `make setup-tests` must have been run after cloning (sets metadata on companion files)
2. `SPC3.4.0.1.CC` contains `#pragma libpath ":"` so `#include <cfile1>` resolves

All 56/56 pass under GoldenGate on macOS with these prerequisites met.

---

## Full Test Baseline (ORCA/C 2.2.4, all platforms)

| Suite | Expected |
|---|---|
| `Tests/Conformance/*.c` | 53/53 compile with 0 errors |
| `Tests/Conformance/*.CC` | 272/272 compile with 0 errors |
| `Tests/Spec.Conform/*.CC` | 56/56 compile with 0 errors (requires `make setup-tests`) |
| `Tests/Deviance/*.CC` | all produce compiler errors (3 compile clean by design) |
| `Tests/Spec.Deviance/*.CC` | 15/15 compile with 0 errors |

The 3 Deviance tests that compile clean by design are:
`D3.3.4.1.CC`, `D4.2.1.1.CC`, `D4.2.2.1.CC`.

---

## Quick Reference: After Cloning on Each Platform

### macOS

```bash
brew install make          # gmake 4.x required
cd goldengate
make setup-tests           # set xattrs on extensionless companion files
make install               # build + install compiler and headers
```

### Linux

```bash
export GOLDEN_GATE=/usr/local/share/GoldenGate   # if not at default path
cd goldengate
make setup-tests           # set user.com.apple.FinderInfo xattrs via python3
make install               # build + install compiler and headers
```

### Windows (MSYS2 / Git Bash)

```bash
export GOLDEN_GATE=/c/path/to/GoldenGate
cd goldengate
make setup-tests           # write AFP_AfpInfo NTFS alt-streams via python3
make install               # build + install compiler and headers
```
