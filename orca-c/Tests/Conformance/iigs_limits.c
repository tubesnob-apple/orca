/*
 * iigs_limits.c -- verify <limits.h> values for the Apple IIgs.
 *
 * int is 16-bit, long is 32-bit on the IIgs.  These limits must match
 * the hardware word sizes, not those of a typical 32-bit or 64-bit host.
 */
#include <stdio.h>
#include <limits.h>

int main(void) {
    int pass = 1;

#define CHECK_EQ(val, expected, name) \
    do { \
        if ((val) != (expected)) { \
            printf("FAIL: " name " = %ld, expected %ld\n", \
                   (long)(val), (long)(expected)); \
            pass = 0; \
        } \
    } while (0)

    /* char */
    CHECK_EQ(CHAR_BIT,   8,      "CHAR_BIT");
    CHECK_EQ(SCHAR_MIN, -128,    "SCHAR_MIN");
    CHECK_EQ(SCHAR_MAX,  127,    "SCHAR_MAX");
    CHECK_EQ(UCHAR_MAX,  255,    "UCHAR_MAX");

    /* short (16-bit) */
    CHECK_EQ(SHRT_MIN,  -32768,  "SHRT_MIN");
    CHECK_EQ(SHRT_MAX,   32767,  "SHRT_MAX");
    CHECK_EQ(USHRT_MAX,  65535U, "USHRT_MAX");

    /* int (16-bit on IIgs) */
    CHECK_EQ(INT_MIN,  -32768,   "INT_MIN");
    CHECK_EQ(INT_MAX,   32767,   "INT_MAX");
    CHECK_EQ(UINT_MAX,  65535U,  "UINT_MAX");

    /* long (32-bit) */
    CHECK_EQ(LONG_MIN,  -2147483648L, "LONG_MIN");
    CHECK_EQ(LONG_MAX,   2147483647L, "LONG_MAX");
    CHECK_EQ(ULONG_MAX,  4294967295UL,"ULONG_MAX");

    /* long long (64-bit) */
    CHECK_EQ(LLONG_MAX,   9223372036854775807LL,  "LLONG_MAX");
    CHECK_EQ(LLONG_MIN,  (-9223372036854775807LL-1), "LLONG_MIN");

    if (pass)
        printf("iigs_limits: PASS\n");
    return pass ? 0 : 1;
}
