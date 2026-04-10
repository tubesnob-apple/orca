/*
 * c99llong_edge.c -- long long edge cases on the Apple IIgs.
 *
 * long long is 64-bit (8 bytes) on the IIgs even though int is 16-bit.
 * These tests verify that ORCA/C generates correct 64-bit arithmetic,
 * conversions, and that LLONG_MAX/MIN are representable.
 */
#include <stdio.h>
#include <limits.h>

int main(void) {
    int pass = 1;
    long long ll;
    unsigned long long ull;
    long l;
    int i;

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("FAIL: %s\n", msg); \
            pass = 0; \
        } \
    } while (0)

    /* Basic sizing */
    CHECK(sizeof(long long)          == 8, "sizeof(long long) == 8");
    CHECK(sizeof(unsigned long long) == 8, "sizeof(unsigned long long) == 8");

    /* Boundary values */
    ll = LLONG_MAX;
    CHECK(ll > 0, "LLONG_MAX > 0");
    CHECK(ll == 9223372036854775807LL, "LLONG_MAX value");

    ll = LLONG_MIN;
    CHECK(ll < 0, "LLONG_MIN < 0");
    CHECK(ll == (-9223372036854775807LL - 1), "LLONG_MIN value");

    /* Arithmetic that spans 32-bit boundary */
    ll = 2147483647LL;          /* LONG_MAX */
    ll += 1LL;
    CHECK(ll == 2147483648LL, "long long addition past 32-bit boundary");

    ll = 4294967295LL;          /* ULONG_MAX */
    ll += 1LL;
    CHECK(ll == 4294967296LL, "long long addition past ULONG_MAX");

    /* Multiplication */
    ll = 100000LL * 100000LL;
    CHECK(ll == 10000000000LL, "long long multiply 100000*100000");

    /* Division and modulus */
    ll = 10000000000LL / 100000LL;
    CHECK(ll == 100000LL, "long long divide");

    ll = 10000000003LL % 100000LL;
    CHECK(ll == 3LL, "long long modulus");

    /* Conversion from long long to smaller types.
     * 0x123456789ABCLL = 0x0000123456789ABC
     * Low 32 bits (long):  0x56789ABC
     * Low 16 bits (int):   0x9ABC = -25924 as signed 16-bit */
    ll = 0x123456789ABCLL;
    l  = (long)ll;
    CHECK(l == 0x56789ABCL, "truncate long long to long (low 32 bits)");
    i  = (int)ll;
    CHECK(i == (int)0x9ABCL, "truncate long long to int (low 16 bits)");

    /* Unsigned long long */
    ull = ULLONG_MAX;
    ull++;
    CHECK(ull == 0ULL, "unsigned long long wraps at ULLONG_MAX");

    /* Shift operations */
    ll = 1LL << 32;
    CHECK(ll == 4294967296LL, "long long left shift by 32");

    ll = 4294967296LL >> 32;
    CHECK(ll == 1LL, "long long right shift by 32");

    if (pass)
        printf("c99llong_edge: PASS\n");
    return pass ? 0 : 1;
}
