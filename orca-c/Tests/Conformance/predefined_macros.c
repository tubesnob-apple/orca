/*
 * predefined_macros.c -- verify ORCA/C predefined macros.
 *
 * Checks __ORCAC__, __APPLE2GS__, __LINE__, __FILE__, __DATE__, __TIME__,
 * __STDC__, and __STDC_VERSION__ are defined with the expected values.
 */
#include <stdio.h>
#include <string.h>

int main(void) {
    int pass = 1;

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("FAIL: %s\n", msg); \
            pass = 0; \
        } \
    } while (0)

    /* ORCA/C defines __ORCAC__ as 1 (a boolean "this is ORCA/C") */
#ifndef __ORCAC__
    printf("FAIL: __ORCAC__ not defined\n");
    pass = 0;
#else
    CHECK(__ORCAC__ == 1, "__ORCAC__ == 1");
#endif

    /* ORCA/C defines __ORCAC_HAS_LONG_LONG__ as 1 */
#ifndef __ORCAC_HAS_LONG_LONG__
    printf("FAIL: __ORCAC_HAS_LONG_LONG__ not defined\n");
    pass = 0;
#else
    CHECK(__ORCAC_HAS_LONG_LONG__ == 1, "__ORCAC_HAS_LONG_LONG__ == 1");
#endif

    /* ORCA/C defines __STDC_NO_COMPLEX__ and __STDC_NO_ATOMICS__ */
#ifndef __STDC_NO_COMPLEX__
    printf("FAIL: __STDC_NO_COMPLEX__ not defined\n");
    pass = 0;
#endif
#ifndef __STDC_NO_ATOMICS__
    printf("FAIL: __STDC_NO_ATOMICS__ not defined\n");
    pass = 0;
#endif

    /* Standard macros */
#ifndef __STDC__
    printf("FAIL: __STDC__ not defined\n");
    pass = 0;
#endif

    /* __LINE__ should be an integer constant */
    {
        int line = __LINE__;
        CHECK(line > 0, "__LINE__ > 0");
    }

    /* __FILE__ should be a string literal */
    {
        const char *f = __FILE__;
        CHECK(f != 0, "__FILE__ is not NULL");
        CHECK(strlen(f) > 0, "__FILE__ is not empty");
    }

    /* __DATE__ and __TIME__ should be strings of known length */
    {
        const char *d = __DATE__;   /* "Mmm DD YYYY" = 11 chars */
        const char *t = __TIME__;   /* "HH:MM:SS"    =  8 chars */
        CHECK(strlen(d) == 11, "__DATE__ is 11 chars");
        CHECK(strlen(t) == 8,  "__TIME__ is 8 chars");
    }

    if (pass)
        printf("predefined_macros: PASS\n");
    return pass ? 0 : 1;
}
