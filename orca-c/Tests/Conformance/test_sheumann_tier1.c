/* Regression tests for sheumann-orca-c tier-1 fixes ported to 2.2.5.
 *
 * Fix 1 (f25c9a9): Null ptr in parameter type checking — reordered nil guard
 *   in the while loop and added nil check before CompTypes in K&R vs
 *   prototyped parameter compatibility checking.
 *
 * Fix 2 (b35bd91): Nil pointer dereferences — ORCA Pascal lacks short-circuit
 *   evaluation; split AND conditions into nested ifs in Parser.pas,
 *   Printf.pas, Scanner.pas.
 *
 * Fix 3 (7f1d96b): unsigned short integer promotion — unary/binary operations
 *   on unsigned short must yield unsigned int, not unsigned short.
 *
 * Fix 4 (df3f364): Comma expression type — result of (a,b) must be exactly
 *   the type of b, not the integer-promoted type.
 *
 * Fix 5 (765ecca): Better 32-bit assignment codegen — pc_cop (copy-assign
 *   used as subexpression) now respects caller's A_X preference, improving
 *   code for chains like a=b=c=val and varargs function entry.
 *
 * Fix 6 (794ca70): Cast to void in integer constant expressions — using
 *   (void)expr as a case label or enum value must be an error.
 *
 * Fix 7 (bb58a1a): K&R parameter stack location — undeclared K&R parameters
 *   now get a proper local label so they are accessed at the right stack
 *   position.
 */

#pragma keep "test_sheumann_tier1"
#pragma optimize 0

/* -----------------------------------------------------------------------
 * Fix 3: unsigned short integer promotions.
 * The type of -us, us+us, us*us etc. must be unsigned int (cgUWord promoted)
 * not unsigned short.  We test by assigning to unsigned int without cast;
 * if the type were wrong the compiler would have promoted differently.
 * ----------------------------------------------------------------------- */
unsigned short us_a = 0xFFFF;
unsigned short us_b = 1;

void test_ushort_promotion(void) {
    unsigned int r1 = -us_a;           /* unary minus on unsigned short   */
    unsigned int r2 = us_a + us_b;     /* binary add on unsigned short    */
    unsigned int r3 = us_a * us_b;     /* binary multiply                 */
    unsigned int r4 = ~us_a;           /* bitwise NOT                     */
    (void)r1; (void)r2; (void)r3; (void)r4;
}

/* -----------------------------------------------------------------------
 * Fix 4: comma expression type.
 * The result type of (a, b) is exactly the type of b.  Using an unsigned
 * short as the right operand of comma must yield unsigned short, not the
 * integer-promoted unsigned int.
 * ----------------------------------------------------------------------- */
void test_comma_type(void) {
    unsigned short us = 42;
    int  i  = 0;
    /* Both of these must compile without implicit-conversion warnings.
       The key is that (i, us) has type unsigned short. */
    unsigned short r1 = (i, us);
    (void)r1;
}

/* -----------------------------------------------------------------------
 * Fix 5: 32-bit assignment used as subexpression (pc_cop codegen).
 * Chain assignments and using the result of a 32-bit assignment as a value.
 * ----------------------------------------------------------------------- */
long la, lb, lc;

void test_long_assign_chain(void) {
    la = lb = lc = 12345678L;          /* chain assignment                */
    long x = (la = 99999L);           /* assignment result as value       */
    (void)x;
}

/* -----------------------------------------------------------------------
 * Fix 6: cast to void in integer constant expressions must be an error.
 * We can only test the *non-error* path here (the compiler test suite
 * checks that 0-error files compile; the error case belongs in Deviance).
 * Verify that a normal (non-void) cast in a case label still compiles.
 * ----------------------------------------------------------------------- */
int test_cast_in_switch(int x) {
    switch (x) {
        case (int)1:   return 10;
        case (int)2:   return 20;
        default:        return 0;
    }
}

/* -----------------------------------------------------------------------
 * Fix 7: K&R-style parameters without explicit declarations.
 * An undeclared K&R parameter defaults to int and must be accessed at the
 * correct stack position.
 * ----------------------------------------------------------------------- */
int kr_func(a, b)   /* K&R style — neither a nor b is declared */
{
    return a + b;   /* both default to int; must read correct stack slots */
}

void test_kr_params(void) {
    int r = kr_func(3, 4);
    (void)r;
}

/* -----------------------------------------------------------------------
 * Fix 1: K&R function definition following a prototyped declaration.
 * The null-pointer-dereference fix is in the compatibility-checking path
 * that runs when a K&R definition follows a prototype.  Providing both
 * exercises that code path.
 * ----------------------------------------------------------------------- */
int proto_then_kr(int x, int y);   /* prototype */
int proto_then_kr(x, y)            /* K&R definition */
int x; int y;
{
    return x - y;
}

void test_proto_then_kr(void) {
    int r = proto_then_kr(10, 3);
    (void)r;
}
