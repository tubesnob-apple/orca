/* Test: inline asm with sep/rep and absolute-long addressing.
   Verifies that sep #0x30 does not emit an extra $00 byte before
   subsequent absolute-long instructions. */
#pragma optimize 0
#pragma keep "test_sepasm"

void test(void) {
    asm {
        sep #0x30
        lda >0xE0C02D
        rep #0x30
    }
}
