/* flip_core.h - visual reordering of Hebrew text for a left-to-right-only renderer.
 * rtl_flip(): if `in` contains Hebrew, writes the visual (left-to-right on screen) order into `out`
 * and returns 1; otherwise returns 0 and leaves `out` alone.
 * Rules: whole line reversed; runs of Latin/digits (with spaces and . , - / ' & : + % between them)
 * are put back in reading order; brackets are mirrored outside those runs; each line handled alone. */
#ifndef FLIP_CORE_H
#define FLIP_CORE_H

#define IS_HEB(c)    ((c) >= 0x0590 && (c) <= 0x05FF)
#define IS_LTRCH(c)  (((c) >= '0' && (c) <= '9') || ((c) >= 'A' && (c) <= 'Z') || ((c) >= 'a' && (c) <= 'z') || \
                      ((c) >= 0x00C0 && (c) < 0x0590))
#define IS_RUNNEUT(c) ((c) == ' ' || (c) == '.' || (c) == ',' || (c) == '-' || (c) == '/' || (c) == '\'' || \
                       (c) == '&' || (c) == ':' || (c) == '+' || (c) == '%' || (c) == '_')

static wchar_t flip_mirror(wchar_t c) {
    switch (c) {
        case '(': return ')'; case ')': return '(';
        case '[': return ']'; case ']': return '[';
        case '{': return '}'; case '}': return '{';
        case '<': return '>'; case '>': return '<';
        default:  return c;
    }
}

static void flip_reverse(wchar_t* a, int lo, int hi) {   /* reverse a[lo..hi] inclusive */
    while (lo < hi) { wchar_t t = a[lo]; a[lo] = a[hi]; a[hi] = t; lo++; hi--; }
}

static void flip_line(const wchar_t* in, wchar_t* out, int n) {
    int i, j, last;
    char mark[512];                                  /* n < cap <= 512 is enforced by the caller */
    for (i = 0; i < n; i++) { out[i] = in[n - 1 - i]; mark[i] = 0; }
    for (i = 0; i < n; ) {
        if (IS_LTRCH(out[i])) {
            last = i;
            for (j = i; j < n; j++) {
                if (IS_LTRCH(out[j])) last = j;
                else if (!IS_RUNNEUT(out[j])) break;
            }
            flip_reverse(out, i, last);
            for (j = i; j <= last; j++) mark[j] = 1;
            i = last + 1;
        } else i++;
    }
    for (i = 0; i < n; i++) if (!mark[i]) out[i] = flip_mirror(out[i]);
}

#define FLIP_MAX 400

static int rtl_flip(const wchar_t* in, wchar_t* out, int cap) {
    int len = 0, i, start, has = 0;
    if (!in) return 0;
    while (in[len]) { if (IS_HEB(in[len])) has = 1; len++; if (len >= cap) return 0; }
    if (!has) return 0;
    start = 0;
    for (i = 0; i <= len; i++) {
        if (i == len || in[i] == '\n' || in[i] == '\r') {
            if (i > start) flip_line(in + start, out + start, i - start);
            if (i < len) out[i] = in[i];
            start = i + 1;
        }
    }
    out[len] = 0;
    return 1;
}

#endif
