/*
 * iigs_ptrarith.c -- 32-bit pointer arithmetic on the Apple IIgs.
 *
 * IIgs pointers are 32-bit (24-bit address + 8-bit bank byte).  Verifies
 * that pointer differences, casts to/from long, and pointer comparison
 * all use 32-bit arithmetic rather than truncating at 16 bits.
 */
#include <stdio.h>
#include <stddef.h>

static char buf[64];    /* static so linker places it; address is 32-bit */

int main(void) {
    int pass = 1;
    char *p, *q;
    ptrdiff_t diff;
    long addr;

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("FAIL: %s\n", msg); \
            pass = 0; \
        } \
    } while (0)

    /* Pointer difference must be 32-bit */
    p = buf;
    q = buf + 63;
    diff = q - p;
    CHECK(diff == 63, "pointer difference is 63");

    /* Pointer can be cast to long and back */
    addr = (long)p;
    CHECK((char *)addr == p, "round-trip cast ptr -> long -> ptr");

    /* Pointer arithmetic across a 16-bit boundary */
    p = buf;
    p += 32;
    CHECK(p == buf + 32, "pointer += 32 works");
    p -= 16;
    CHECK(p == buf + 16, "pointer -= 16 works");

    /* Array indexing is equivalent to pointer arithmetic */
    buf[0] = 'A';
    buf[63] = 'Z';
    p = buf;
    CHECK(p[0] == 'A', "p[0] == buf[0]");
    CHECK(p[63] == 'Z', "p[63] == buf[63]");

    /* sizeof pointer is 4 on IIgs */
    CHECK(sizeof(p) == 4, "sizeof(char *) == 4");
    CHECK(sizeof(void *) == 4, "sizeof(void *) == 4");

    /* NULL pointer is zero */
    p = (void *)0;
    CHECK(p == 0, "NULL == 0");
    CHECK(!p, "!NULL is true");

    if (pass)
        printf("iigs_ptrarith: PASS\n");
    return pass ? 0 : 1;
}
