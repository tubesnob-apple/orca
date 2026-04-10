/* Test: varargs stack cleanup with optimize 72 (saveStack=false, strictVararg=false).
 *
 * Regression test for the bug where op^.q was set to
 * ord(hasVarargs and strictVararg) rather than ord(hasVarargs).
 * With optimize bit 6 (strictVararg=false), hasVarargs was masked to 0,
 * causing GenCup/GenCui to skip the TSX/STX + LDX/TXS save-restore path.
 * The callee would clean only its declared fixed params via ADC #N in its
 * own epilogue, leaving extra variadic args on the stack unpopped.
 *
 * The fix: always use the save-restore path when hasVarargs is true,
 * regardless of the strictVararg optimization bit.
 *
 * This test verifies the fix compiles cleanly under all relevant optimize
 * levels. Runtime correctness (SP == SP before call) requires an emulator.
 */

/* Test 1: direct call — optimize 0 (default, saveStack=true, strictVararg=true) */
#pragma optimize 0
#pragma memorymodel 1
#pragma keep "test_varargs_cleanup"

int vfunc1(const char *path, int flags, ...);
int result1;
void test_direct_default(void) {
    result1 = vfunc1("/etc/test", 1, 0);
}

/* Test 2: direct call — optimize 64 (strictVararg=false, saveStack=true) */
#pragma optimize 64
int vfunc2(const char *path, int flags, ...);
int result2;
void test_direct_opt64(void) {
    result2 = vfunc2("/etc/test", 1, 0);
}

/* Test 3: direct call — optimize 8 (saveStack=false, strictVararg=true) */
#pragma optimize 8
int vfunc3(const char *path, int flags, ...);
int result3;
void test_direct_opt8(void) {
    result3 = vfunc3("/etc/test", 1, 0);
}

/* Test 4: direct call — optimize 72 (saveStack=false, strictVararg=false) — the bug case */
#pragma optimize 72
int vfunc4(const char *path, int flags, ...);
int result4;
void test_direct_opt72(void) {
    result4 = vfunc4("/etc/test", 1, 0);
}

/* Test 5: indirect (function pointer) call — optimize 72 */
#pragma optimize 72
typedef int (*vfp)(const char *, int, ...);
int result5;
void test_indirect_opt72(vfp fp) {
    result5 = fp("/etc/test", 1, 0);
}

/* Test 6: multiple extra variadic args — optimize 72 */
#pragma optimize 72
int vfunc6(int a, ...);
int result6;
void test_multi_varargs_opt72(void) {
    result6 = vfunc6(1, 2, 3, 4);
}
