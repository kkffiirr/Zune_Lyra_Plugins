/* host-side unit test for flip_core.h: gcc -fshort-wchar test_flip.c && ./a.out */
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "flip_core.h"

static int eq(const unsigned short* a, const unsigned short* b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

#define T(name, in, expect) do { unsigned short o[FLIP_MAX]; int r = rtl_flip(in, o, FLIP_MAX); \
    if (!r || !eq(o, expect)) { printf("FAIL %s\n", name); fails++; } else printf("ok   %s\n", name); } while (0)

int main(void) {
    int fails = 0; unsigned short o[FLIP_MAX];
    T("hebrew word",          (unsigned short*)L"\x05E9\x05DC\x05D5\x05DD",            (unsigned short*)L"\x05DD\x05D5\x05DC\x05E9");
    T("hebrew + English",     (unsigned short*)L"\x05E9\x05DC\x05D5\x05DD World",      (unsigned short*)L"World \x05DD\x05D5\x05DC\x05E9");
    T("English phrase kept",  (unsigned short*)L"Tom Jones \x05E9\x05DC",              (unsigned short*)L"\x05DC\x05E9 Tom Jones");
    T("AC/DC kept",           (unsigned short*)L"AC/DC \x05E9",                        (unsigned short*)L"\x05E9 AC/DC");
    T("number kept",          (unsigned short*)L"\x05E9\x05DC 2024",                   (unsigned short*)L"2024 \x05DC\x05E9");
    T("brackets mirrored",    (unsigned short*)L"(\x05E9\x05DC)",                      (unsigned short*)L"(\x05DC\x05E9)");
    T("two lines separately", (unsigned short*)L"\x05E9\x05DC\nab\x05D0",              (unsigned short*)L"\x05DC\x05E9\n\x05D0" L"ab");
    if (rtl_flip((unsigned short*)L"Hello World", o, FLIP_MAX)) { printf("FAIL latin untouched\n"); fails++; } else printf("ok   latin untouched\n");
    if (rtl_flip((unsigned short*)L"", o, FLIP_MAX))           { printf("FAIL empty\n"); fails++; }           else printf("ok   empty\n");
    { unsigned short x[FLIP_MAX], y[FLIP_MAX]; const unsigned short* in = (unsigned short*)L"\x05E9\x05DC\x05D5\x05DD";
      rtl_flip(in, x, FLIP_MAX); memo_add((wchar_t*)x);
      if (!memo_has((wchar_t*)x)) { printf("FAIL memo miss\n"); fails++; } else printf("ok   memo hit on own output\n");
      if (memo_has((wchar_t*)in)) { printf("FAIL memo hit on logical text\n"); fails++; } else printf("ok   memo miss on logical text\n");
      rtl_flip(in, y, FLIP_MAX); if (!eq(x, y)) { printf("FAIL deterministic\n"); fails++; } else printf("ok   same input flips the same\n"); }
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
