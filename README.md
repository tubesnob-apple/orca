# ORCA Toolchain Monorepo

A **one-stop shop** for the **ORCA development toolchain** for the Apple IIGS
(65816 processor): all the compilers, libraries, linker, and utilities pulled
together from their scattered upstream repositories into a single tree that
builds and installs into [GoldenGate](https://goldengate.byteworks.us/) with a
single command.

## Goals

The original Byte Works ORCA sources live in a dozen separate GitHub repos,
each with its own build conventions, file-type metadata requirements, and
install quirks. Getting a clean, working ORCA installation on a modern
machine meant cloning each repo individually, figuring out its idiosyncratic
build, manually copying binaries and libraries into the right GoldenGate
subdirectories, and setting ProDOS file types by hand. This monorepo
eliminates that:

- **One checkout.** All ORCA toolchain source in one tree, with each
  subdirectory's upstream git history preserved so blame and bisect still
  work across the split.
- **One build command.** `make -C goldengate install` compiles every
  component with a consistent build system and places the output directly
  into your `~/Library/GoldenGate/` installation (with automatic backups of
  anything it overwrites).
- **No manual file-type fiddling.** ProDOS file type metadata is set via
  `iix chtyp` from within the Makefiles — no more hand-running `xattr` or
  Python helpers after every build.
- **Consistent versioning.** Every binary stamps itself with a shared
  version string from `goldengate/VERSION` plus a build timestamp, so you
  can always tell exactly which build is installed.
- **Cross-platform.** The entire toolchain builds from source using the
  [GoldenGate](https://goldengate.byteworks.us/) `iix` cross-compilation
  environment on macOS, Linux, and Windows — no emulator or on-hardware
  Apple IIGS build step required.

## Components

### Compilers
| Directory | Component | Language | Description |
|-----------|-----------|----------|-------------|
| `orca-c/` | ORCA/C 2.2.7 | Pascal + ASM | ANSI C compiler for the 65816 |
| `orca-pascal/` | ORCA/Pascal 2.2.7 | Pascal + ASM | ISO Pascal compiler for the 65816 |

### Libraries
| Directory | Component | Language | Description |
|-----------|-----------|----------|-------------|
| `orcalib/` | ORCALib 2.2.7 | ASM | C standard library runtime (stdio, stdlib, string, etc.) |
| `paslib/` | PasLib 2.2.7 | ASM | Pascal runtime library |
| `syslib/` | SysLib 2.2.7 | ASM | System library (memory management, I/O, math) |
| `sysfloat/` | SysFloat 2.2.7 | ASM | SANE floating point library |
| `sysfpefloat/` | SysFPEFloat 2.2.7 | ASM | FPE floating point library |

### Linker
| Directory | Component | Language | Description |
|-----------|-----------|----------|-------------|
| `linker/` | Linker 2.1.0 | ASM | ORCA link editor |

### Utilities
| Directory | Component | Language | Description |
|-----------|-----------|----------|-------------|
| `makelib/` | MakeLib 2.2.7 | C | Library archive builder/manager |
| `dumpobj/` | DumpOBJ 2.2.7 | C | OMF object file dumper |

### Deferred (source present, not wired into build)
| Directory | Description |
|-----------|-------------|
| `shell/` | ORCA shell |
| `prizm/` | PRIZM desktop editor |

## Building

Requires the [GoldenGate](https://goldengate.byteworks.us/) `iix` toolchain and GNU Make 3.81+.

```bash
# Build everything (binaries staged in goldengate/bin_obj/)
make -C goldengate

# Build and install to GoldenGate (with automatic backups)
make -C goldengate install

# Build a single component
make -C goldengate orca-c
make -C goldengate orcalib

# Clean everything
make -C goldengate clean

# One-time setup after cloning (sets ProDOS file types on test companion files)
make -C goldengate setup
```

### Build Order

The build system enforces dependency ordering:

1. **orcalib** — C runtime library (no dependencies)
2. **paslib, syslib, sysfloat, sysfpefloat** — additional libraries (no dependencies)
3. **linker** — link editor (self-hosting)
4. **dumpobj, makelib** — C utilities (require orcalib installed)
5. **orca-pascal** — Pascal compiler (self-hosting)
6. **orca-c** — C compiler (requires orcalib installed)

### Versioning

All components share a base version from `goldengate/VERSION`. Each build appends a timestamp:

```
2.2.7-20260409.185919
```

- Libraries embed the version in a `~Version` OMF segment
- Compilers embed it in their startup banner
- C utilities embed it in their printed banner

### Install Backups

`make install` automatically backs up each existing GoldenGate binary before overwriting:

```
$TMPDIR/goldenGate/Languages/cc_20260409185932
$TMPDIR/goldenGate/Libraries/ORCALib_20260409185920
$TMPDIR/goldenGate/Utilities/DumpObj_20260409185921
```

## Testing

### ORCA/C Test Suite
```bash
cd orca-c
# Conformance tests (.c and .CC formats)
for f in Tests/Conformance/*.c; do iix compile "$f" 2>&1 | grep -v "0 errors" && echo "FAIL: $f"; done
for f in Tests/Conformance/*.CC; do iix compile "$f" 2>&1 | grep -v "0 errors" && echo "FAIL: $f"; done

# Spec.Conform tests (must run from within the directory)
cd Tests/Spec.Conform && for f in *.CC; do iix compile "$f" 2>&1 | grep -v "0 errors" && echo "FAIL: $f"; done
```

### MakeLib Tests
```bash
cd makelib && make -f goldengate/Makefile test
```

## Tools

The `goldengate/tools/` directory contains Python utilities for working with ORCA binaries:

- **`cowomfdis.py`** — OMF object file parser and 65816 disassembler
- **`compare_libc.py`** — Compare symbols/segments between two OMF library files
- **`cowdiff.py`** — ProDOS disk image extraction and comparison
- **`cowrez.py`** — Cross-platform Rez resource compiler
- **`cowsettype.py`** — Cross-platform ProDOS file type setter

## Key Technical Notes

### GoldenGate `iix` Quirks

- **`iix assemble`** writes output to CWD regardless of the `keep` path prefix. Makefiles `mv` the output into `obj/` after assembly.
- **`iix assemble`** sets ProDOS type $B0 (SRC) on output — `iix chtyp -t obj` is needed to set $B1 for `makelib` to accept them.
- **`iix compile`** for Pascal produces separate `.a` and `.B` files when the source has a paired `.asm` file. The GoldenGate linker only reads `.a`, so `.B` must be concatenated into `.a` (OMF segments are concatenable). This affects the orca-pascal build.
- **`#pragma lint -1`** in ORCA/C causes the compiler to omit the `~GLOBALS` segment from `.a` output when lint warnings are present, producing unlinkable objects. Workaround: use `#pragma lint 0`.
- **`iix chtyp`** requires type names (`obj`, `exe`, `src`, `lib`), not hex codes.

## Upstream Sources

This monorepo is assembled from authoritative Byte Works, Inc. repositories
published at [github.com/byteworksinc](https://github.com/byteworksinc). Each
subdirectory is imported with its original git history preserved.

| Subdirectory | Upstream repository |
|--------------|---------------------|
| `orcalib/` | [byteworksinc/ORCALib](https://github.com/byteworksinc/ORCALib) |
| `paslib/` | [byteworksinc/PasLib](https://github.com/byteworksinc/PasLib) |
| `syslib/` | [byteworksinc/SysLib](https://github.com/byteworksinc/SysLib) |
| `sysfloat/` | [byteworksinc/SysFloat](https://github.com/byteworksinc/SysFloat) |
| `sysfpefloat/` | [byteworksinc/SysFPEFloat](https://github.com/byteworksinc/SysFPEFloat) |
| `linker/` | [byteworksinc/Linker](https://github.com/byteworksinc/Linker) |
| `dumpobj/` | [byteworksinc/DumpObj](https://github.com/byteworksinc/DumpObj) |
| `makelib/` | [byteworksinc/MakeLib](https://github.com/byteworksinc/MakeLib) |
| `orca-c/` | [byteworksinc/ORCA-C](https://github.com/byteworksinc/ORCA-C) |
| `orca-pascal/` | [byteworksinc/ORCA-Pascal](https://github.com/byteworksinc/ORCA-Pascal) |
| `shell/` | [byteworksinc/Shell](https://github.com/byteworksinc/Shell) |
| `prizm/` | [byteworksinc/Prizm](https://github.com/byteworksinc/Prizm) |

Each upstream is also available as a mirror at
[github.com/ksherlock](https://github.com/ksherlock) for several of these
components (`DumpObj`, `Linker`, `MakeLib`, `ORCA-C`, `ORCA-Pascal`,
`ORCALib`).

## Changes from Upstream

This repository consolidates source from the above repositories and adds a unified GoldenGate cross-build system:

### Source Consolidation
- Combined 12 Byte Works repositories into a single monorepo with preserved git history
- Replaced MakeLib 2.0 with MakeLib 2.2.4 (includes bug fixes for Read4() sign-extension and multi-arg invocation)

### Build System (`goldengate/`)
- Created unified top-level Makefile orchestrating all components
- Consistent versioning across all binaries from `goldengate/VERSION`
- Binary staging in `goldengate/bin_obj/` before installation
- Automatic backup of existing GoldenGate binaries on install
- Replaced platform-specific xattr commands with `iix chtyp`

### ORCA/C (pre-release branch)
- Based on the `v222-memory-fixes` branch with memory management and code generation fixes
- Applied upstream GNO startup support (`#pragma gnostartup`)
- Applied upstream lint error counting rework
- GoldenGate cross-platform build with `goldengate/Makefile`

### ORCA/Pascal
- GoldenGate cross-build with `.B` → `.a` concatenation workaround

### Bug Reports
- **ORCA/C `#pragma lint -1` bug**: When lint warnings are present, the compiler omits the `~GLOBALS` segment from `.a` output, causing "Unresolved reference" errors at link time. Affects DumpOBJ and any legacy source using `#pragma lint -1`.

## Outstanding Issues

- **Shell and PRIZM** source is present but not wired into the build system (not used by `iix`)
- **ORCA/Pascal** produces slightly different output when recompiled with itself (expected for self-hosting compilers)
- **ORCA/C pre-release branch** is based on `v222-memory-fixes`, not `master` — some newer C23 features from master (nullptr, _BitInt, constexpr, etc.) are not included

## Acknowledgments

This project would not be possible without the work of many people over several decades:

- **Mike Westerfield** and **Byte Works, Inc.** — Original author of the entire ORCA toolchain (ORCA/C, ORCA/Pascal, Linker, MakeLib, DumpOBJ, Shell, PRIZM, and all runtime libraries). The ORCA suite was a remarkable achievement in bringing professional development tools to the Apple IIGS platform.

- **Kelvin Sherlock** — Creator of [GoldenGate](https://goldengate.byteworks.us/), the cross-compilation environment that makes it possible to build ORCA software on modern systems. GoldenGate's `iix` toolchain is the foundation that this entire build system rests on.

- **Stephen Heumann** — Extensive contributions to ORCA/C including C11/C17/C23 conformance, 64-bit integer support, numerous bug fixes, and ongoing maintenance of the compiler and libraries. Many of the upstream fixes incorporated here are his work.

- **Jawaid Bazyar** and the **GNO/ME** team — The GNO multitasking environment for the Apple IIGS, whose runtime requirements drove the GNO-specific library variants and startup code in orcalib and orca-c.

- **Phil Montoya** — Original author of DumpOBJ and contributions to the ORCA toolchain.

- The **Apple IIGS community** — For keeping this platform alive and the collective knowledge that makes projects like this possible.

The source code for the ORCA toolchain was released by Byte Works, Inc. and is used here under the terms of the original licenses included in each component directory.

## License

Individual components retain their original licenses from Byte Works, Inc. See the `LICENSE` file in each subdirectory.
