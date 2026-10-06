/* fuzz flip_core.h under ASan/UBSan: gcc -fshort-wchar -fsanitize=address,undefined fuzz_flip.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flip_core.h"
static unsigned short pick(void) {
    static const unsigned short pool[] = {0x05D0,0x05E9,0x05EA,0x05DD,'a','Z','0','9',' ','.',',','-','/','(',')','[',']','<','>','\n','\r','\'','&',0x00E9,0x05B0,0x200F,0x4E2D,'_',':'};
    return pool[rand() % (sizeof pool / sizeof *pool)];
}
int main(void) {
    int fails = 0, flipped = 0; long iters = 300000;
    srand(12345);
    for (long it = 0; it < iters; it++) {
        int len = (rand() % 8 == 0) ? rand() % 460 : rand() % 40;
        unsigned short* in = malloc((len + 1) * 2);          /* exact-size: ASan flags any over-read */
        for (int i = 0; i < len; i++) in[i] = pick();
        in[len] = 0;
        int cap = FLIP_MAX;
        unsigned short* out = malloc(cap * 2);
        memset(out, 0xAB, cap * 2);
        int r = rtl_flip(in, out, cap);
        if (len >= cap) { if (r) { printf("FAIL: accepted len>=cap\n"); fails++; } }
        else if (r) {
            flipped++;
            if (out[len] != 0) { printf("FAIL: no terminator\n"); fails++; }
            /* same multiset of chars, brackets/angles counted as mirrored pairs */
            int ci[0x10000 > 0 ? 1 : 1]; (void)ci;
            long sum_in = 0, sum_out = 0;
            for (int i = 0; i < len; i++) {
                unsigned short a = in[i], b = out[i];
                if (a == ')' || a == '(') a = '(';  if (b == ')' || b == '(') b = '(';
                if (a == ']' || a == '[') a = '[';  if (b == ']' || b == '[') b = '[';
                if (a == '>' || a == '<') a = '<';  if (b == '>' || b == '<') b = '<';
                sum_in += (long)a * a + a; sum_out += (long)b * b + b;
            }
            if (sum_in != sum_out) { printf("FAIL: chars changed/lost len=%d\n", len); fails++; }
            for (int i = len + 1; i < cap; i++) if (out[i] != 0xABAB) { printf("FAIL: wrote past end\n"); fails++; break; }
            for (int i = 0; i < len; i++) if ((in[i] == '\n' || in[i] == '\r') && out[i] != in[i]) { printf("FAIL: newline moved\n"); fails++; break; }
        } else {
            for (int i = 0; i < len; i++) if (IS_HEB(in[i])) { printf("FAIL: hebrew but not flipped\n"); fails++; break; }
            if (out[0] != 0xABAB && len < cap) { printf("FAIL: wrote output when returning 0\n"); fails++; }
        }
        free(in); free(out);
    }
    printf("%ld cases, %d flipped, %d failures\n", iters, flipped, fails);
    return fails != 0;
}
