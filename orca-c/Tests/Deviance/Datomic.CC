/* Deviance Test: _Atomic is not supported by ORCA/C.
 *
 * C11 defines _Atomic as a type qualifier and type specifier for atomic
 * types.  ORCA/C does not implement atomic operations or the stdatomic.h
 * header.  Any program that uses _Atomic should fail to compile or
 * produce a diagnostic.
 *
 * Expected behavior: compiler error or diagnostic on use of _Atomic.
 */
#include <stdatomic.h>

int main(void) {
    _Atomic int x = 0;
    atomic_fetch_add(&x, 1);
    return atomic_load(&x);
}
