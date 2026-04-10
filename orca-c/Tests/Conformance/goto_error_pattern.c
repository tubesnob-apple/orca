/*
 * goto_error_pattern.c -- regression test for goto with shared error label.
 *
 * Tests a function with many conditional branches converging on a single
 * "goto error" label, exercising the label table and segment buffer across
 * a realistic error-handling pattern.  Previously triggered segment buffer
 * overflow when the function produced enough code to exceed the 16-bit
 * blkcnt/segDisp fields (fixed in 2.2.2).
 */

struct S { int a; int b; int c; };

static int helper(struct S *s) { return 0; }

int test(struct S *s) {
    if (s->a) goto error;
    if (s->b) goto error;
    if (s->c) { if (helper(s)) goto error; s->a++; }
    if (s->a) { if (helper(s)) goto error; s->a++; }
    if (s->b) { if (helper(s)) goto error; s->a++; }
    if (s->c) { if (helper(s)) goto error; s->a++; }
    if (s->a) { if (helper(s)) goto error; s->a++; }
    if (s->b) { if (helper(s)) goto error; s->a++; }
    if (s->c) { if (helper(s)) goto error; s->a++; }
    if (s->a) { if (helper(s)) goto error; s->a++; }
    if (s->b) { if (helper(s)) goto error; s->a++; }
    if (s->c) { if (helper(s)) goto error; s->a++; }
    if (s->a) { if (helper(s)) goto error; s->a++; }
    if (s->b) { if (helper(s)) goto error; s->a++; }
    if (s->c) { if (helper(s)) goto error; s->a++; }
    if (s->a) { if (helper(s)) goto error; s->a++; }
    if (s->b) { if (helper(s)) goto error; s->a++; }
    if (s->c) { if (helper(s)) goto error; s->a++; }
    if (s->a) { if (helper(s)) goto error; s->a++; }
    if (s->b) { if (helper(s)) goto error; s->a++; }
    if (s->c) { if (helper(s)) goto error; s->a++; }
    if (s->a) { if (helper(s)) goto error; s->a++; }
    if (s->b) { if (helper(s)) goto error; s->a++; }
    if (s->c) { if (helper(s)) goto error; s->a++; }
    if (s->a) { if (helper(s)) goto error; s->a++; }
    if (s->b) { if (helper(s)) goto error; s->a++; }
    if (s->c) { if (helper(s)) goto error; s->a++; }
    if (s->a) { if (helper(s)) goto error; s->a++; }
    if (s->b) { if (helper(s)) goto error; s->a++; }
    return s->a;
error:
    return -1;
}
