/*
 * iigs_typesize.c -- verify Apple IIgs type sizes.
 *
 * On the Apple IIgs (65816):
 *   char=1, short=2, int=2, long=4, long long=8
 *   float=double=long double=10 (SANE 80-bit extended)
 *   pointer=4 (24-bit address + 8-bit bank, stored as 32-bit)
 *
 * These are IIgs-specific deviations from typical LP64 or ILP32 hosts.
 * This test ensures ORCA/C's type widths remain correct.
 */
#include <stdio.h>
#include <stddef.h>

int main(void) {
    int pass = 1;

#define CHECK(expr, expected) \
    do { \
        if ((long)(expr) != (long)(expected)) { \
            printf("FAIL: " #expr " = %ld, expected %ld\n", \
                   (long)(expr), (long)(expected)); \
            pass = 0; \
        } \
    } while (0)

    /* Integer types */
    CHECK(sizeof(char),           1);
    CHECK(sizeof(short),          2);
    CHECK(sizeof(int),            2);   /* 16-bit on IIgs */
    CHECK(sizeof(long),           4);   /* 32-bit */
    CHECK(sizeof(long long),      8);   /* 64-bit */
    CHECK(sizeof(unsigned char),  1);
    CHECK(sizeof(unsigned short), 2);
    CHECK(sizeof(unsigned int),   2);
    CHECK(sizeof(unsigned long),  4);

    /* Floating-point: float=IEEE 754 single (4), double=IEEE 754 double (8),
     * long double=SANE 80-bit extended (10) */
    CHECK(sizeof(float),          4);
    CHECK(sizeof(double),         8);
    CHECK(sizeof(long double),    10);

    /* Pointers: 32-bit (24-bit address + bank byte) */
    CHECK(sizeof(void *),         4);
    CHECK(sizeof(char *),         4);
    CHECK(sizeof(int *),          4);
    CHECK(sizeof(long *),         4);

    /* ptrdiff_t and size_t must hold pointer differences / sizes */
    CHECK(sizeof(ptrdiff_t) >= sizeof(void *), 1);
    CHECK(sizeof(size_t)    >= sizeof(void *), 1);

    if (pass)
        printf("iigs_typesize: PASS\n");
    return pass ? 0 : 1;
}
