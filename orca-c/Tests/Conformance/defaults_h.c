/*
 * defaults_h.c -- verify that Defaults.h is automatically processed.
 *
 * Defaults.h is the ORCA/C prefix file, silently included before every
 * compilation via InitHeader -> DoDefaultsDotH.  It defines __appleiigs__
 * and __GNO__ so that platform-detection guards (#ifndef __appleiigs__)
 * work correctly without any explicit #include.
 *
 * This test was written to catch the regression where Header2.pas had a
 * stub InitHeader that never called DoDefaultsDotH, leaving the macros
 * undefined for all GoldenGate compilations.
 */
#include <stdio.h>

int main(void) {
    int pass = 1;

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("FAIL: %s\n", msg); \
            pass = 0; \
        } \
    } while (0)

    /* __appleiigs__ must be defined (by Defaults.h, not by user code) */
#ifndef __appleiigs__
    printf("FAIL: __appleiigs__ not defined\n");
    pass = 0;
#else
    CHECK(__appleiigs__ == 1, "__appleiigs__ == 1");
#endif

    /* __GNO__ must be defined */
#ifndef __GNO__
    printf("FAIL: __GNO__ not defined\n");
    pass = 0;
#else
    CHECK(__GNO__ == 1, "__GNO__ == 1");
#endif

    /*
     * #ifndef guard behavior: if Defaults.h is processed first, a
     * subsequent #ifndef __appleiigs__ block must NOT redefine it.
     * The value must remain 1, not 99.
     */
#ifndef __appleiigs__
#define __appleiigs__ 99
#endif
    CHECK(__appleiigs__ == 1, "#ifndef __appleiigs__ does not redefine (guard works)");

#ifndef __GNO__
#define __GNO__ 99
#endif
    CHECK(__GNO__ == 1, "#ifndef __GNO__ does not redefine (guard works)");

    /*
     * #ifdef must see both macros as defined.
     */
#ifdef __appleiigs__
    CHECK(1, "__appleiigs__ visible to #ifdef");
#else
    printf("FAIL: __appleiigs__ not visible to #ifdef\n");
    pass = 0;
#endif

#ifdef __GNO__
    CHECK(1, "__GNO__ visible to #ifdef");
#else
    printf("FAIL: __GNO__ not visible to #ifdef\n");
    pass = 0;
#endif

    if (pass)
        printf("defaults_h: PASS\n");
    return pass ? 0 : 1;
}
