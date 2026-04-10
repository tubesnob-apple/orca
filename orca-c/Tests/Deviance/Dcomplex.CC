/* Deviance Test: _Complex is not supported by ORCA/C.
 *
 * C99 (and later) defines _Complex, float _Complex, double _Complex, etc.
 * ORCA/C recognizes the _Complex keyword (so it does not produce a syntax
 * error on the keyword itself) but does not implement complex arithmetic.
 * Any program that actually uses complex arithmetic should fail to compile
 * or produce a diagnostic.
 *
 * Expected behavior: compiler error or diagnostic on use of _Complex type.
 */
#include <complex.h>

int main(void) {
    double _Complex z = 1.0 + 2.0 * I;
    return (int)creal(z);
}
