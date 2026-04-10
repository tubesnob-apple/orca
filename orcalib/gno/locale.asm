         keep  obj/locale
         mcopy locale.macros
         case  on
****************************************************************
*
*  Locale - locale support (GNO/ME variant)
*
*  This currently implements a minimalistic version of the
*  <locale.h> functions, supporting only the "C" locale.
*
*  GNO override: struct lconv field order matches the C standard
*  as declared in GNO's <locale.h> (Berkeley 4.4 origin, 1993).
*  The standard order differs from the non-GNO ORCALib locale.asm:
*
*  C standard / GNO order (used here):
*    decimal_point, thousands_sep, grouping,
*    int_curr_symbol, currency_symbol,
*    mon_decimal_point, mon_thousands_sep, mon_grouping,
*    positive_sign, negative_sign,
*    int_frac_digits, frac_digits,
*    p_cs_precedes, p_sep_by_space,
*    n_cs_precedes, n_sep_by_space,
*    p_sign_posn, n_sign_posn
*
*  The GNO <locale.h> does not include the C99 int_p/n_* fields.
*
****************************************************************
*
Locale   private                        dummy routine
         end

****************************************************************
*
*  char *setlocale(int category, const char *locale);
*
*  Set or query current locale
*
*  Inputs:
*        category - locale category to set or query
*        locale - locale name (or NULL for query)
*
*  Outputs:
*        returns locale string (for relevant category),
*        or NULL if locale cannot be set as requested
*
****************************************************************
*
setlocale start
LC_MAX   equ   5                        maximum valid LC_* value

         csubroutine (2:category,4:locale),0

         lda   category                 if category is invalid
         cmp   #LC_MAX+1
         bge   err                        return NULL
         lda   locale                   if querying the current locale
         ora   locale+2
         beq   good                       return "C"
         lda   [locale]
         cmp   #'C'                     if locale is "C" or "", we are good
         beq   good
         and   #$00FF
         bne   err
good     lda   #C_str                   if successful, return "C"
         sta   locale
         lda   #^C_str
         sta   locale+2
         bra   ret
err      stz   locale                   otherwise, return NULL for error
         stz   locale+2
ret      creturn 4:locale

C_str    dc    c'C',i1'0'
         end

****************************************************************
*
*  struct lconv *localeconv(void);
*
*  Get numeric formatting conventions
*
*  Outputs:
*        returns pointer to a struct lconv containing
*        appropriate values for the current locale
*
*  Field order matches GNO <locale.h> (C standard / POSIX order).
*
****************************************************************
*
localeconv start
CHAR_MAX equ   255

         ldx   #^C_locale_lconv
         lda   #C_locale_lconv
         rtl

C_locale_lconv anop
* C standard field order — matches GNO include/locale.h
decimal_point           dc a4'period'       ; offset 0
thousands_sep           dc a4'emptystr'     ; offset 4
grouping                dc a4'emptystr'     ; offset 8
int_curr_symbol         dc a4'emptystr'     ; offset 12
currency_symbol         dc a4'emptystr'     ; offset 16
mon_decimal_point       dc a4'emptystr'     ; offset 20
mon_thousands_sep       dc a4'emptystr'     ; offset 24
mon_grouping            dc a4'emptystr'     ; offset 28
positive_sign           dc a4'emptystr'     ; offset 32
negative_sign           dc a4'emptystr'     ; offset 36
int_frac_digits         dc i1'CHAR_MAX'     ; offset 40
frac_digits             dc i1'CHAR_MAX'     ; offset 41
p_cs_precedes           dc i1'CHAR_MAX'     ; offset 42
p_sep_by_space          dc i1'CHAR_MAX'     ; offset 43
n_cs_precedes           dc i1'CHAR_MAX'     ; offset 44
n_sep_by_space          dc i1'CHAR_MAX'     ; offset 45
p_sign_posn             dc i1'CHAR_MAX'     ; offset 46
n_sign_posn             dc i1'CHAR_MAX'     ; offset 47

period   dc    c'.',i1'0'
emptystr dc    i1'0'
         end
