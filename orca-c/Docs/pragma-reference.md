# ORCA/C Pragma Reference

This document describes every `#pragma` directive supported by ORCA/C, including its default value, what compiler flags it sets, and the precise impact on generated 65816 code.

> **Note on defaults:** ORCA/C defaults to C17 mode with extensions enabled (`cStd = c17`, `strictMode = false`). Several pragmas have defaults that differ when strict mode or an older standard is selected via the `-S` command-line option. Where this applies it is called out explicitly.

---

## Code-generation pragmas

### `#pragma memorymodel N`

**Default:** `1` (small memory model)

| Value | Meaning |
|-------|---------|
| `0` | Large memory model |
| `1` (or any non-zero) | Small memory model *(default)* |

**Small model** uses 16-bit absolute addressing (`STA abs`, `LDA abs`) and `JSR` for function calls. Data references assume the data bank register already points to the correct bank.

**Large model** uses 24-bit long absolute addressing (`STA long`, `LDA long`) and `JSL` for function calls. This is required when code or data spans more than one 64 KB bank. Throughout `Gen.pas` (100+ call sites) the choice between `absolute` and `longAbs` addressing modes is gated on `smallMemoryModel`.

Use large model for programs whose code or static data exceeds 64 KB, or for ROM/init code that runs before the data bank register is set up.

---

### `#pragma databank N`

**Default:** `0` (disabled)

| Value | Effect |
|-------|--------|
| `0` | No data bank save/restore *(default)* |
| non-zero | Save and restore the data bank register (`B`) on entry/exit |

When enabled, the function prologue emits:
```
PHB           ; save caller's data bank
PHB           ; (pushed twice for alignment)
PLA           ; pop into A
STA dp_save   ; save old bank in direct page
PEA ~GLOBALS  ; push this module's bank
PLB           ; load new data bank
PLB
```
And the epilogue restores:
```
PEI (dp_save) ; push saved bank
PLB
PLB
```

This is required when calling Apple IIgs Toolbox routines that may change the data bank register, or when writing code that must work correctly regardless of what bank the caller had loaded.

---

### `#pragma toolparms N`

**Default:** `0` (disabled)

| Value | Effect |
|-------|--------|
| `0` | Standard ORCA/C calling convention *(default)* |
| non-zero | Apple IIgs Toolbox calling convention |

Changes how return values are placed on the stack. Under the Toolbox convention the return value is written to the stack *above* the return address rather than in registers. For 16-bit values (`Gen.pas:7327`):
```
STA  returnSize+1,S    ; write result above return address
```
For 32-bit/extended values both `A` and `X` are written back. Required when writing glue code for toolbox interface layers that expect the stack-return protocol.

---

### `#pragma rtl`

**Default:** disabled

When set, functions return with the 65816 `RTL` instruction (3-byte return) instead of `RTS` (2-byte return). This is needed when code is called across bank boundaries via a long (`JSL`) call. It also selects different startup and shutdown runtime stubs:

| `rtl` | Startup | Shutdown |
|-------|---------|----------|
| `false` | `~_BWSTARTUP3`, `~C_STARTUP` | `~C_SHUTDOWN` |
| `true` | `~_BWSTARTUP4`, `~C_STARTUP2` | `~C_SHUTDOWN2` |

---

### `#pragma noroot`

**Default:** disabled (a `.root` segment *is* generated)

An OMF `.root` file marks a module as a standalone executable entry point and carries the startup/shutdown records and stack size. Setting `#pragma noroot` suppresses generation of this file. Use it for library units or overlay modules that are linked into an executable but should never be the entry point themselves.

---

### `#pragma segment "name" [, dynamic]`

**Default:** segment name is 10 spaces (the unnamed default segment)

Changes the OMF segment name for all code and data that follows. Names are truncated to 10 characters. The optional `dynamic` keyword sets `segmentKind := $8000`, marking the segment as dynamically loadable — the system loader will not load it at startup; it is loaded on demand at runtime.

Segments give the linker and the GS/OS memory manager a way to group related code for overlay management or dynamic loading.

---

### `#pragma stacksize N`

**Default:** `0` (use launcher's default)

Reserves `N` bytes of stack in a dedicated OMF `~_STACK` segment. A value of `0` means the stack size is not specified and the launcher's default is used. This value is written to the object file as a `DS` (define space) record inside the `~_STACK` segment (`Native2.pas:1321–1334`).

---

### `#pragma keep "filename"`

**Default:** output filename is determined by the shell

Overrides the output object file path. Must appear before any function definitions. Sets the `kFlag` field in the linker DCB, telling the linker to use this filename for the kept object rather than the default derived from the source filename.

---

### `#pragma float card [slot]`

**Default:** `card = 0`, `slot = 0` (use SANE software floating point)

| Card | Meaning |
|------|---------|
| `0` | SANE (Software Arithmetic Numeric Extension) *(default)* |
| `1` | Apple IIgs FPE (Floating Point Engine) hardware card |

The slot number selects which slot the FPE card occupies when `card = 1`. This controls which runtime library entry points the compiler calls for floating-point operations.

---

## Output type pragmas

These pragmas change the type of OMF executable produced. Only one should appear per compilation unit.

### `#pragma cda "menu name" open_fn close_fn`

Marks the output as a **Classic Desk Accessory (CDA)**. Generates the CDA OMF header records. `open_fn` is called when the user selects the CDA from the Apple menu; `close_fn` is called when it is dismissed. Sets `isClassicDeskAcc := true`.

### `#pragma nda open close action init period eventmask "menu name"`

Marks the output as a **New Desk Accessory (NDA)**. Generates NDA OMF records. The parameters specify the four required entry point functions, the refresh period (in ticks), the event mask (which system events to receive), and the menu bar label. Sets `isNewDeskAcc := true`.

### `#pragma cdev open_fn`

Marks the output as a **Control Panel Device (CDEV)**. Generates CDEV OMF records. `open_fn` is the entry point called by the control panel. Sets `isCDev := true`.

### `#pragma xcmd main_fn`

Marks the output as a **HyperCard XCMD** (external command). Generates XCMD OMF records. Sets `isXCMD := true`.

### `#pragma nba main_fn`

Marks the output as an **NBA** (non-bank-addressed) module. Generates NBA OMF records. Sets `isNBA := true`.

---

## Debug pragmas

### `#pragma debug N`

**Default:** all bits `0` (no debug code generated)

Exception: if the `-d` command-line flag is passed, `debugFlag` and `profileFlag` are both set to `true` at startup.

`N` is a bitmask:

| Bit | Value | Flag | Effect |
|-----|-------|------|--------|
| 0 | 1 | `rangeCheck` | Array and string bounds checking; runtime calls inserted before index operations |
| 1 | 2 | `debugFlag` | Emit debugger records in the object file; insert `COP 4` at function exit for breakpoint support; emit debug function-name strings for GSBug/NiftyList |
| 2 | 4 | `profileFlag` | Push/pop a name record on a profiling stack at function entry/exit; also implies `debugFlag` |
| 3 | 8 | `traceBack` | Generate traceback records (`pc_nam`) in the object file for stack-unwinding and crash reporting |
| 4 | 16 | `checkStack` | Emit a runtime call at function entry to detect stack overflow before touching the frame |
| 5 | 32 | `checkNullPointers` | Insert a null-pointer test before every pointer dereference and array/field access through a pointer |
| 15 | 32768 | `debugStrFlag` | Emit GSBug/NiftyList-format inline function name strings (a `BRL` instruction followed by `$7771`, length, and name bytes) immediately after the function entry point |

Bits 1 and 2 together give full profiling with debugger support. Bit 15 (`debugStrFlag`) is specifically for symbolic debugging with GSBug or NiftyList and can be used without bit 1.

---

## Optimization pragmas

### `#pragma optimize N`

**Default:** all bits follow the `-o` command-line flag (`cLineOptimize`). With `-o`, all optimizations are on. Without `-o`, all are off.

`N` is a bitmask:

| Bit | Value | Flag | Effect |
|-----|-------|------|--------|
| 0 | 1 | `peepHole` | Intermediate code peephole pass: collapses redundant load/store pairs and no-op sequences in the intermediate representation before native code is emitted |
| 1 | 2 | `npeepHole` | Native 65816 peephole pass: removes redundant native instructions (e.g. `STA x / LDA x` → `STA x`) after code generation |
| 2 | 4 | `registers` | Register value tracking: remembers what value is in each register and skips reloads when the value is still valid |
| 3 | 8 | ¬`saveStack` | **Inverted.** When this bit is *set*, stack-pointer save/restore around calls is *disabled*. When clear, the compiler emits `TSC/TCS` pairs to preserve the caller's stack register. Default: save/restore is on when `-o` is off, off when `-o` is on |
| 4 | 16 | `commonSubexpression` | Common subexpression elimination: detects expressions computed more than once in a basic block and reuses the cached result |
| 5 | 32 | `loopOptimizations` | Loop-invariant code motion: hoists subexpressions that do not change across loop iterations out of the loop body |
| 6 | 64 | ¬`strictVararg` | **Inverted.** When this bit is *set*, stack cleanup around variadic calls is *relaxed*. When clear, the compiler always repairs the stack after a variadic call. Default: strict when not using `-o` or in strict mode |
| 7 | 128 | `fastMath` | Floating-point optimizations that break strict IEEE 754 rules (e.g. reordering operations, eliminating identity operations). Do not use in code that depends on exact FP rounding or exception behavior |

Bits 3 and 6 are *inverted* — setting them *disables* the named feature.

---

## Lint / diagnostic pragmas

### `#pragma lint N [; E]`

**Default:** `lint = 0` (no checks), `lintIsError = true`

The optional second value `E` sets whether lint violations are treated as errors (`1`) or warnings (`0`). Default is errors.

`N` is a bitmask:

| Bit | Hex | Check |
|-----|-----|-------|
| 0 | `$0001` | Calls to functions that have no declaration in scope (implicit `int` return) |
| 1 | `$0002` | Functions defined without an explicit return type |
| 2 | `$0004` | Calls to functions that have no prototype in scope |
| 3 | `$0008` | Unknown or unrecognized `#pragma` directives |
| 4 | `$0010` | `printf`/`scanf` format string mismatches (argument type vs. format specifier) |
| 5 | `$0020` | Integer constant overflow in expressions |
| 6 | `$0040` | C99 syntax used when compiling in C89/C95 mode |

Note: `lintC99Syntax` (`$0040`) is automatically OR'd into `lint` when `strictMode` is true and `cStd >= c99`.

---

## Language / compatibility pragmas

### `#pragma ignore N`

**Default:** depends on active C standard (see table)

`N` is a bitmask. Defaults shown are for the standard C17 mode (the compiler default). Defaults change when a strict or older standard is selected via `-S`.

| Bit | Value | Flag | Default (C17 mode) | Effect when set |
|-----|-------|------|--------------------|-----------------|
| 0 | 1 | `skipIllegalTokens` | `false` | Don't emit errors for invalid tokens inside skipped `#if 0 ... #endif` blocks |
| 1 | 2 | `allowLongIntChar` | `false` | Accept multi-character `long int` character constants (non-standard) |
| 2 | 4 | `allowTokensAfterEndif` | `false` | Accept extra tokens on the same line as `#endif` |
| 3 | 8 | `allowSlashSlashComments` | **`true`** | Accept C++ `//` line comments |
| 4 | 16 | `allowMixedDeclarations` | **`true`** | Accept mixed declarations and statements; apply C99 block-scope rules for `for`-loop variables |
| 5 | 32 | `looseTypeChecks` | **`true`** | Relax certain standard type-compatibility checks |

**Important:** In the default C17 mode, bits 3, 4, and 5 are *already on*. `#pragma ignore 8` (allow `//` comments) is a no-op in default mode — `//` comments are already accepted. These bits only matter when using an older standard or strict mode:

- `-S c89` / `-S c89compat`: bits 3 and 4 are turned off; `//` comments and mixed declarations require `#pragma ignore 24`
- `-S c17` (strict): bits 3 and 4 stay on, but `extendedKeywords`, `extendedParameters`, and `looseTypeChecks` are turned off

Bit 4 (`allowMixedDeclarations`) cannot be changed after the first function definition has been parsed.

---

### `#pragma extensions N`

**Default:** both bits `1` (both features enabled in C17 mode); both bits `0` in strict mode (`-S c17`, `-S c11`, etc.)

| Bit | Value | Flag | Effect |
|-----|-------|------|--------|
| 0 | 1 | `extendedKeywords` | Recognize ORCA/C-specific keywords beyond the C standard: `pascal`, `segment`, and other Apple IIgs extensions |
| 1 | 2 | `extendedParameters` | Promote all `float` function parameters to `extended` (80-bit) precision rather than `double`. This matches the 65816 FPU's native format and avoids a conversion step |

In strict mode (`-S c17`, `-S c11`, etc.) both flags are forced off regardless of this pragma.

---

### `#pragma unix N`

**Default:** `0` (int is 16 bits)

| Bit | Value | Effect |
|-----|-------|--------|
| 0 | 1 | `unix_1`: treat `int` as 32 bits instead of the native 16-bit IIgs size |

This is a significant ABI-breaking change. When enabled, all `int` arithmetic uses 32-bit operations, `sizeof(int)` returns 4, and data layout changes accordingly. Required for porting code written for 32-bit Unix/POSIX systems that assumes `int` is at least 32 bits.

---

## Preprocessor pragmas

### `#pragma path "directory"`

Appends a directory to the `#include <>` search path. Paths are colon-delimited GS/OS pathnames. Multiple `#pragma path` directives accumulate; they do not replace earlier entries. Equivalent to the `-I` include-path option but applied at the source level, which allows header files to extend the search path for their own dependencies.

### `#pragma expand N`

**Default:** `0` (off)

When non-zero, macro expansions are echoed to the listing file. Useful for diagnosing complex macro interactions.

### `#pragma line N`

Overrides the compiler's current line number counter to `N`. Intended for use by preprocessors or code generators that feed synthesized source to ORCA/C and need error messages to refer to the original source line numbers.

---

## C standard pragmas (`#pragma STDC`)

These follow the C99/C11 standard and take `ON`, `OFF`, or `DEFAULT` as their operand.

| Pragma | Flag | Effect |
|--------|------|--------|
| `#pragma STDC FENV_ACCESS ON` | `fenvAccess` | Tells the compiler the floating-point environment (status flags, rounding mode) may be read or written. Suppresses FP optimizations that would move, merge, or eliminate operations that affect FP state. Also sets `fenvAccessInFunction` when used inside a function body |
| `#pragma STDC FP_CONTRACT ON/OFF` | — | Recognized for standard compliance; not currently acted upon |
| `#pragma STDC CX_LIMITED_RANGE ON/OFF` | — | Recognized for standard compliance; not currently acted upon |

`_Pragma("STDC FENV_ACCESS ON")` (the C99 operator form) is also supported and is equivalent.

---

## Summary of defaults

| Pragma | Default value | Overridden by |
|--------|--------------|---------------|
| `memorymodel` | `1` (small) | — |
| `databank` | `0` | — |
| `toolparms` | `0` | — |
| `rtl` | off | — |
| `noroot` | off (root *is* generated) | — |
| `segment` | `"          "` (10 spaces) | — |
| `stacksize` | `0` (launcher default) | — |
| `keep` | from shell | — |
| `float` | `0, 0` (SANE) | — |
| `debug` | `0` | `-d` flag sets bits 1+2 |
| `optimize` | all bits = `-o` flag | `-o` command-line flag |
| `lint` | `0` | `lintIsError` defaults to `true` |
| `ignore 1` (skipIllegalTokens) | off | — |
| `ignore 2` (allowLongIntChar) | off | — |
| `ignore 4` (allowTokensAfterEndif) | off | — |
| `ignore 8` (allowSlashSlashComments) | **on** in C99+ mode, **off** in C89/C95 | `-S` standard flag |
| `ignore 16` (allowMixedDeclarations) | **on** in C99+ mode, **off** in C89/C95 | `-S` standard flag |
| `ignore 32` (looseTypeChecks) | **on** unless strict mode | `-S c17` / `-S c11` etc. |
| `extensions 1` (extendedKeywords) | **on** unless strict mode | `-S c17` / `-S c11` etc. |
| `extensions 2` (extendedParameters) | **on** unless strict mode | `-S c17` / `-S c11` etc. |
| `unix` | `0` (int = 16 bits) | — |
| `expand` | off | — |
| `STDC FENV_ACCESS` | off | — |
