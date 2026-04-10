/* Regression tests for sheumann-orca-c tier-2 fixes ported to 2.2.6.
 *
 * Fix 8 (9251bd0): Function returning function pointer — parameters from the
 *   returned function pointer type were incorrectly added to the lastParameter
 *   list of the enclosing function.  Guarded both sites with
 *   "if not madeFunctionTable" so they are skipped when parameters were
 *   already registered from a prototype.
 *
 * Fix 9 (3003f89): Enum constant name scope — NewSymbol was called before the
 *   value expression was evaluated, so the constant's own name was in scope
 *   during its own initialiser.  Save the token first, evaluate the
 *   expression, then call NewSymbol.  Also handles typedef tokens as enum
 *   constant names (when a typedef from an outer scope is re-used).
 *
 * Fix 10 (76433a6): Nil/garbage pointer dereferences — three files:
 *   Parser.pas: split "(ip=nil) or (ip^.itype^.size=0)" into separate checks
 *   Scanner.pas: split 4-part _Pragma AND into two-stage nil check
 *   Symbol.pas: introduce redeclOK boolean; restructure duplicate-symbol
 *     check so CompTypes is only called after both itype and cs^.itype are
 *     confirmed non-nil.
 */

#pragma keep "test_sheumann_tier2"
#pragma optimize 0

/* -----------------------------------------------------------------------
 * Fix 8: function returning a function pointer.
 * The compiler was adding the returned function-pointer's parameters
 * (int, long) to the outer function's parameter list, causing either
 * spurious "unnamed parameter" errors or wrong stack-position accesses.
 * Simply compiling this without error validates the fix.
 * ----------------------------------------------------------------------- */
void (*fix8_ret_fnptr(void))(int, long);   /* prototype */

void (*fix8_ret_fnptr(void))(int, long) {
    return 0;
}

/* K&R style with function-pointer return type: params must be accessible */
int fix8_kr_sub(a, b)
int a; int b;
{
    return a - b;
}

void test_fix8(void) {
    void (*fp)(int, long) = fix8_ret_fnptr();
    (void)fp;
    int r = fix8_kr_sub(10, 3);
    (void)r;
}

/* -----------------------------------------------------------------------
 * Fix 9: enum constant scope during its own value expression.
 * E_OUTER is defined in an enclosing enum.  An inner-scope enum redefines
 * the same name; the value expression should see the outer E_OUTER = 10.
 * ----------------------------------------------------------------------- */
enum { E_OUTER = 10 };

void test_fix9_scope(void) {
    /* E_OUTER is a typedef-like ident seen from outer scope.
     * Inner block-scope enum shadows it; its value expression must see
     * the outer E_OUTER (= 10), so inner E_OUTER must equal 11. */
    enum { E_OUTER = E_OUTER + 1 };
    int v = (int)E_OUTER;
    (void)v;
}

/* Fix 9: typedef name from outer scope re-used as enum constant name.
 * The scanner tokenises 'my_typedef' as a typedef token; the parser
 * must accept it as an enum constant name after the fix. */
typedef int my_typedef;

void test_fix9_typedef_name(void) {
    enum { my_typedef = 42 };   /* shadows file-scope typedef */
    int v = (int)my_typedef;
    (void)v;
}

/* -----------------------------------------------------------------------
 * Fix 10: _Pragma with well-formed argument still works after the Scanner
 * nil-check split.  Verify the compiler processes it without error.
 * ----------------------------------------------------------------------- */
_Pragma("optimize 0")

/* Fix 10 / Symbol.pas: redeclaring an extern is still allowed. */
extern int ext_var;
extern int ext_var;          /* redeclaration — must not produce error 42 */

int ext_var = 5;

void test_fix10(void) {
    (void)ext_var;
}
