/*
 * iigs_intarith.c -- 16-bit int arithmetic edge cases on the Apple IIgs.
 *
 * Verifies that ORCA/C correctly handles int overflow/wrap-around, signed
 * vs unsigned promotion, and 16-bit boundary arithmetic.  A common source
 * of bugs is assuming int is 32-bit when writing C code.
 */
#include <stdio.h>
#include <limits.h>

int main(void) {
    int pass = 1;
    int i;
    unsigned int u;
    long l;

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("FAIL: %s\n", msg); \
            pass = 0; \
        } \
    } while (0)

    /* int is 16-bit: overflow wraps (undefined by C standard, but
     * ORCA/C compiles for 65816 which wraps at 16-bit) */
    i = INT_MAX;
    i++;
    CHECK(i == INT_MIN, "INT_MAX + 1 wraps to INT_MIN");

    /* Unsigned int wraps at 65535 */
    u = UINT_MAX;      /* 65535 */
    u++;
    CHECK(u == 0, "UINT_MAX + 1 wraps to 0");

    /* long arithmetic is 32-bit — no overflow at 16-bit boundary */
    l = 32767L + 1L;
    CHECK(l == 32768L, "long 32767+1 = 32768");

    l = 65535L + 1L;
    CHECK(l == 65536L, "long 65535+1 = 65536");

    /* Mixing int and long: int promotes to long in binary ops */
    i = 30000;
    l = i + 10000L;
    CHECK(l == 40000L, "int + long promotes to long");

    /* Unsigned promotion: unsigned int * unsigned int stays unsigned */
    u = 300;
    l = (long)(u * u);    /* 300*300=90000 — overflows 16-bit unsigned, wraps */
    CHECK((unsigned int)(u * u) == (unsigned int)90000 % 65536,
          "unsigned int multiply wraps at 16-bit");

    /* Signed right shift: implementation-defined but ORCA/C preserves sign */
    i = -16;
    CHECK((i >> 1) == -8, "signed right shift is arithmetic");
    CHECK((i >> 2) == -4, "signed right shift 2 bits");

    /* Bitwise operations on 16-bit int */
    i = 0x1234;
    CHECK((i & 0xFF00) == 0x1200, "int & mask works at 16-bit");
    CHECK((i | 0x00FF) == 0x12FF, "int | mask works at 16-bit");
    CHECK((i ^ 0xFFFF) == (int)0xEDCB, "int ^ invert is 16-bit");

    if (pass)
        printf("iigs_intarith: PASS\n");
    return pass ? 0 : 1;
}
