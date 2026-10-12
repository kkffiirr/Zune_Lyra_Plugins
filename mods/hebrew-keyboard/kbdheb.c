/* kbdheb v0.3: Hebrew letters on the Zune HD on-screen keyboard (firmware 4.5), loaded into servicesd (zhud_serv.dll).
 *
 * The letter keys come from a table in zhud's read-only data: 16-byte entries { wchar normal, shift, ?, ?; ptr popup, ptr popup }.
 * Rows (found by live dump 2026-10-11): q..p at 0x419b2be0 (10 keys), a..l at 0x419b2c90 (9), z..m at 0x419b2d40 (7).
 * Stage file \flash2\automation\kbdheb.stage (first char), default A:
 *   A = verify the table and log it, write nothing
 *   B = A plus a log-only hook on SetPage (zhud 0x419c36f0, the abc/123 toggle), nothing written to the table
 *   D = C plus writing direction (RTL typing in the keyboard text box)
 *   C = B plus the real feature: the abc/123 toggle now cycles English -> 123 -> Hebrew -> 123 -> English
 * Guard: kbdheb.armed exists while we are inside ARM_SECONDS of a write; if it is still there at the next load
 * (the HUD died inside the window) the mod renames it to kbdheb.tripped and stays inert until that file is deleted. */
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "lyra.h"

#define DIR       L"\\flash2\\automation\\"
#define F_LOG     DIR L"kbdheb.log"
#define F_STAGE   DIR L"kbdheb.stage"
#define F_ARMED   DIR L"kbdheb.armed"
#define F_TRIPPED DIR L"kbdheb.tripped"
#define ARM_SECONDS 120

typedef struct { WORD c[4]; DWORD p[2]; } KEY;      /* 16 bytes */
#define ROW0 ((KEY*)0x419b2be0u)
#define ROW1 ((KEY*)0x419b2c90u)
#define ROW2 ((KEY*)0x419b2d40u)

static const char EN0[] = "qwertyuiop", EN1[] = "asdfghjkl", EN2[] = "zxcvbnm";
/* Android-style Hebrew: ק ר א ט ו ן ם פ ת צ / ש ד ג כ ע י ח ל ך / ז ס ב ה נ מ ץ ; ף is the long-press of פ */
static const WORD HE0[10] = {0x5E7,0x5E8,0x5D0,0x5D8,0x5D5,0x5DF,0x5DD,0x5E4,0x5EA,0x5E6};
static const WORD HE1[9]  = {0x5E9,0x5D3,0x5D2,0x5DB,0x5E2,0x5D9,0x5D7,0x5DC,0x5DA};
static const WORD HE2[7]  = {0x5D6,0x5E1,0x5D1,0x5D4,0x5E0,0x5DE,0x5E5};
#define PEH_IDX 7                                    /* פ in row 0 */
static KEY g_popup[10];                              /* permanent: the keyboard keeps the pointer */

static void LG(const char* fmt, ...) {
    char buf[300]; int n; DWORD w; HANDLE h; va_list ap;
    va_start(ap, fmt); n = vsnprintf(buf, sizeof(buf) - 3, fmt, ap); va_end(ap);
    if (n < 0 || n > (int)sizeof(buf) - 3) n = sizeof(buf) - 3;
    buf[n++] = '\r'; buf[n++] = '\n';
    h = CreateFileW(F_LOG, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, NULL, FILE_END);
    WriteFile(h, buf, n, &w, NULL);
    CloseHandle(h);
}
static int g_unstable;
static void write_count(const wchar_t* p, int n) {
    unsigned char c = (unsigned char)n; DWORD w;
    HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) { WriteFile(h, &c, 1, &w, NULL); CloseHandle(h); }
}
static int read_count(const wchar_t* p) {
    unsigned char c = 0; DWORD got = 0;
    HANDLE h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    if (!ReadFile(h, &c, 1, &got, NULL) || got != 1) c = 0;
    CloseHandle(h);
    return c;
}
static int exists(const wchar_t* p) { return GetFileAttributesW(p) != 0xFFFFFFFFu; }
static void touch(const wchar_t* p) {
    HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}
static char read_stage(void) {
    char c = 'A'; DWORD got = 0;
    HANDLE h = CreateFileW(F_STAGE, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 'A';
    if (!ReadFile(h, &c, 1, &got, NULL) || got != 1) c = 'A';
    CloseHandle(h);
    if (c >= 'a' && c <= 'z') c -= 32;
    return (c == 'B' || c == 'C' || c == 'D') ? c : 'A';
}

static int sig_row(const KEY* k, const char* en) {   /* readable, and every entry is {x, X, X, X} for the expected letter */
    int i;
    if (IsBadReadPtr(k, 16 * (int)strlen(en))) return 0;
    for (i = 0; en[i]; i++)
        if (k[i].c[0] != (WORD)en[i] || k[i].c[1] != (WORD)(en[i] - 32)) return 0;
    return 1;
}
static int table_ok(void) { return sig_row(ROW0, EN0) && sig_row(ROW1, EN1) && sig_row(ROW2, EN2); }

static void put_row(KEY* k, const char* en, const WORD* he, int n) {
    int i;
    for (i = 0; i < n; i++) {
        k[i].c[0] = he[i];
        k[i].c[1] = k[i].c[2] = k[i].c[3] = he[i];         /* Shift changes nothing in Hebrew */
        k[i].p[0] = k[i].p[1] = 0;                         /* no accent popups on Hebrew keys */
    }
}

static KEY g_en[26];                                 /* original English rows, saved from the live table */
static volatile int g_lang;                          /* 0 = English letters page, 1 = Hebrew */
static int g_in_num, g_back, g_ready;

static int write_table(int hebrew) {
    /* the page is read-only ROM data: VirtualProtect fails (err 87), Lyra's patcher is the way in */
    KEY src[26]; DWORD proc = lyra_find_proc_struct(GetCurrentProcessId()); int i, bad = 0;
    if (!proc) { LG("no proc struct"); return -1; }
    if (!hebrew) memcpy(src, g_en, sizeof src);
    else {
        put_row(src, EN0, HE0, 10); put_row(src + 10, EN1, HE1, 9); put_row(src + 19, EN2, HE2, 7);
        src[PEH_IDX].p[0] = src[PEH_IDX].p[1] = (DWORD)g_popup;
    }
    for (i = 0; i < 26; i++) {
        KEY* dst = i < 10 ? ROW0 + i : i < 19 ? ROW1 + (i - 10) : ROW2 + (i - 19);
        if (lyra_patch_code(proc, (DWORD)dst, &src[i], 16) != 0) bad++;
    }
    LG("write_table(%s): %d of 26 entries failed; row0[0]=%04x", hebrew ? "Hebrew" : "English", bad, ROW0[0].c[0]);
    g_lang = hebrew;
    return bad ? -1 : 0;
}

static void prepare(void) {
    int i;
    memcpy(g_en, ROW0, 160); memcpy(g_en + 10, ROW1, 144); memcpy(g_en + 19, ROW2, 112);
    for (i = 0; i < 10; i++) { g_popup[i].c[0] = g_popup[i].c[2] = 32; }
    g_popup[0].c[0] = g_popup[0].c[1] = g_popup[0].c[2] = g_popup[0].c[3] = HE0[PEH_IDX];
    g_popup[1].c[0] = g_popup[1].c[1] = g_popup[1].c[2] = g_popup[1].c[3] = 0x5E3;   /* final peh */
}

static void* volatile g_kbd;                         /* the keyboard object, remembered from the hooks */
typedef int (*SetPageFn)(void* self, int page);
static SetPageFn g_next;
static char g_stage;
static volatile LONG g_calls;

/* zhud 0x419c36f0 SetPage(keyboard, page): page 0 = letters, 1 = numbers/symbols. The abc/123 button calls it with (page-1)&1. */
static int H_setpage(void* self, int page) {
    g_kbd = self;
    if (g_calls < 60) { InterlockedIncrement(&g_calls); LG("SetPage(%p, %d) lang=%d in_num=%d", self, page, g_lang, g_in_num); }
    if ((g_stage == 'C' || g_stage == 'D') && g_ready) {
        if (page == 1 && !g_in_num) { g_in_num = 1; g_back = g_lang; }
        else if (page == 0 && g_in_num) {
            g_in_num = 0;
            write_table(!g_back);                         /* cycle: English -> 123 -> Hebrew -> 123 -> English */
            LG("letters page now %s", g_lang ? "Hebrew" : "English");
        }
    }
    return g_next(self, page);
}

/* ---- stage D: writing direction ------------------------------------------------------------
 * The keyboard's text box is a plain left-to-right edit. We keep its text in VISUAL order: a Hebrew run is stored reversed,
 * and each new Hebrew letter (or the space after a word) is inserted at the LEFT end of the run, so the text grows leftwards
 * like real RTL typing. Apps still get LOGICAL text: the get-text path (zhud 0x419c2914 with flag 0) returns a flipped copy.
 * rtlflip must not flip this edit a second time: it asks KbdHebOwnsElem(). */
#define IS_H(x) ((x) >= 0x5D0 && (x) <= 0x5EA)
typedef const wchar_t* (*GetBufFn)(void* self, int flag);
typedef int (*InsFn)(void* self, const wchar_t* ins, int flag);
#define GETBUF   ((GetBufFn)0x419c2914u)
#define CUR_GET  ((int (*)(void*))0x419c4a6cu)
#define CUR_SET  ((int (*)(void*, int))0x419c23d8u)
static GetBufFn g_next_getbuf;
static InsFn g_next_ins;
static wchar_t g_flipbuf[4][300];
static volatile LONG g_flipi;
static volatile LONG g_dir_logs;

static int wlen(const wchar_t* t) { int n = 0; while (n < 280 && t[n]) n++; return n; }

__declspec(dllexport) int KbdHebOwnsElem(void* elem) {
    void* k = g_kbd;
    return elem && k && !IsBadReadPtr((char*)k + 0x38, 4) && elem == *(void**)((char*)k + 0x38);
}

/* run around index c: maximal span of Hebrew letters and spaces, minus spaces that touch a non-Hebrew neighbour */
static void run_bounds(const wchar_t* T, int n, int c, int* ps, int* pe, int* has_h) {
    int s = c, e = c, i;
    while (s > 0 && (IS_H(T[s - 1]) || T[s - 1] == L' ')) s--;
    while (s < c && T[s] == L' ' && s > 0) s++;
    while (e < n && (IS_H(T[e]) || T[e] == L' ')) e++;
    while (e > c && T[e - 1] == L' ' && e < n) e--;
    *has_h = 0; for (i = s; i < e; i++) if (IS_H(T[i])) { *has_h = 1; break; }
    *ps = s; *pe = e;
}

static volatile LONG g_ins_calls;
static int H_insert(void* self, const wchar_t* ins, int flag) {
    g_kbd = self;
    if (g_ins_calls < 12) {
        InterlockedIncrement(&g_ins_calls);
        LG("H_insert self=%p ins=%p ch=%04x flag=%d mode=%lu edit=%p", self, ins, (ins && !IsBadReadPtr(ins, 2)) ? ins[0] : 0, flag,
           IsBadReadPtr((char*)self + 0xc4, 4) ? 0xdeadUL : *(DWORD*)((char*)self + 0xc0), IsBadReadPtr((char*)self + 0x3c, 4) ? NULL : *(void**)((char*)self + 0x38));
    }
    if (ins && !IsBadReadPtr(ins, 2) && !IsBadReadPtr((char*)self + 0xc4, 4)) {
        DWORD mode = *(DWORD*)((char*)self + 0xc0);
        void* edit = *(void**)((char*)self + 0x38);
        if ((mode == 1 || mode == 2) && edit) {
            const wchar_t* T = GETBUF(self, 1);
            if (T && !IsBadReadPtr(T, 2)) {
                int n = wlen(T), c = CUR_GET(edit), s, e, h, target; wchar_t ch = ins[0];
                if (c < 0) c = 0; if (c > n) c = n;
                run_bounds(T, n, c, &s, &e, &h);
                target = c;
                if (h && (IS_H(ch) || ch == L' ' || ch == L'.' || ch == L',' || ch == L'!' || ch == L'?' || ch == L'-')) target = s;
                else if (h && !IS_H(ch)) target = e;
                if (g_dir_logs < 40) { InterlockedIncrement(&g_dir_logs); LG("insert U+%04x n=%d cursor=%d run=[%d,%d) h=%d -> %d", ch, n, c, s, e, h, target); }
                if (target != c) CUR_SET(edit, target);
            }
        }
    }
    return g_next_ins(self, ins, flag);
}


/* The sink for typed text: zhud 0x419c37f8 SetBuffer(keyboard, newText, newCursor). Key presses end up here whichever route they take,
 * so the RTL rule is applied by comparing the new text with the current one: a single inserted character is re-placed. */
typedef int (*SetBufFn)(void* self, const wchar_t* text, int cursor);
static SetBufFn g_next_setbuf;
static wchar_t g_sbbuf[300];
static volatile LONG g_sb_logs;
static int is_neutral(wchar_t ch) { return ch == L' ' || ch == L'.' || ch == L',' || ch == L'!' || ch == L'?' || ch == L'-'; }
static int H_setbuf(void* self, const wchar_t* text, int cursor) {
    g_kbd = self;
    if (text && !IsBadReadPtr(text, 2) && !IsBadReadPtr((char*)self + 0xc4, 4)) {
        DWORD mode = *(DWORD*)((char*)self + 0xc0);
        if (mode == 1 || mode == 2) {
            const wchar_t* old = GETBUF(self, 1);
            if (old && !IsBadReadPtr(old, 2)) {
                int no = wlen(old), nn = wlen(text);
                if (nn == no + 1 && nn < 290) {
                    int p = 0, ok;
                    if (cursor >= 1 && cursor <= nn && memcmp(text, old, (cursor - 1) * 2) == 0 && memcmp(text + cursor, old + cursor - 1, (no - cursor + 1) * 2) == 0) p = cursor - 1;
                    else { while (p < no && old[p] == text[p]) p++; }
                    ok = memcmp(text, old, p * 2) == 0 && memcmp(text + p + 1, old + p, (no - p) * 2) == 0;
                    if (ok) {
                        wchar_t ch = text[p]; int s, e, h, target = p, i;
                        run_bounds(old, no, p, &s, &e, &h);
                        if (h && (IS_H(ch) || is_neutral(ch))) target = s; else if (h && !IS_H(ch)) target = e;
                        if (g_sb_logs < 40) { InterlockedIncrement(&g_sb_logs); LG("setbuf insert U+%04x at %d (n=%d) run=[%d,%d) h=%d -> %d", ch, p, no, s, e, h, target); }
                        if (target != p) {
                            for (i = 0; i < target; i++) g_sbbuf[i] = old[i];
                            g_sbbuf[target] = ch;
                            for (i = target; i < no; i++) g_sbbuf[i + 1] = old[i];
                            g_sbbuf[no + 1] = 0;
                            return g_next_setbuf(self, g_sbbuf, target + 1);
                        }
                    }
                }
            }
        }
    }
    return g_next_setbuf(self, text, cursor);
}

/* The real typing path (found 2026-10-12): KeyInput 0x419c3f78 -> SendKey 0x419c6824(keycode, char, flags, edit) hands the key to the
 * native text box, which inserts at ITS cursor. Moving that cursor first is all RTL typing needs. */
typedef int (*SendKeyFn)(DWORD code, DWORD ch, DWORD flags, void* edit);
static SendKeyFn g_next_sendkey;
static volatile LONG g_sk_calls;
static int H_sendkey(DWORD code, DWORD ch, DWORD flags, void* edit) {
    void* k = g_kbd;
    if (g_sk_calls < 16) { InterlockedIncrement(&g_sk_calls); LG("SendKey code=%lu ch=%04lx flags=%lx edit=%p kbd=%p owns=%d", code, ch, flags, edit, k, KbdHebOwnsElem(edit)); }
    if (k && edit && code == 8 && ch == 0 && KbdHebOwnsElem(edit)) {
        /* Backspace: the newest letter of a Hebrew word is its LEFT end, so aim the native delete (which removes the char before
         * its cursor) there. English at the end of the text, or no Hebrew left, deletes from the end as usual. */
        const wchar_t* T = GETBUF(k, 1);
        if (T && !IsBadReadPtr(T, 2)) {
            int n = wlen(T), s, e, h, target = n;
            run_bounds(T, n, n, &s, &e, &h);
            if (h && n > 0 && (IS_H(T[n - 1]) || T[n - 1] == L' ')) target = s + 1;
            if (g_sb_logs < 60) { InterlockedIncrement(&g_sb_logs); LG("backspace n=%d run=[%d,%d) h=%d -> cursor %d", n, s, e, h, target); }
            CUR_SET(edit, target);
        }
    } else if (k && edit && ch > 0x1f && ch < 0x10000 && KbdHebOwnsElem(edit)) {
        const wchar_t* T = GETBUF(k, 1);
        if (T && !IsBadReadPtr(T, 2)) {
            int n = wlen(T), c = CUR_GET(edit), s, e, h, target; wchar_t chh = (wchar_t)ch;
            if (c < 0) c = 0; if (c > n) c = n;
            run_bounds(T, n, c, &s, &e, &h);
            target = c;
            if (h && (IS_H(chh) || is_neutral(chh))) target = s; else if (h && !IS_H(chh)) target = e;
            if (g_sb_logs < 60) { InterlockedIncrement(&g_sb_logs); LG("sendkey U+%04x n=%d cursor=%d run=[%d,%d) h=%d -> %d", chh, n, c, s, e, h, target); }
            if (target != c) CUR_SET(edit, target);
        } else if (g_sb_logs < 60) { InterlockedIncrement(&g_sb_logs); LG("sendkey U+%04lx: no buffer text (GetBuffer=%p)", ch, T); }
    }
    return g_next_sendkey(code, ch, flags, edit);
}

static void flip_runs(const wchar_t* in, wchar_t* out, int cap) {
    int n = wlen(in), i = 0, a, b, x, y, h;
    if (n >= cap) n = cap - 1;
    for (i = 0; i < n; i++) out[i] = in[i];
    out[n] = 0;
    for (i = 0; i < n; ) {
        if (!(IS_H(in[i]) || in[i] == L' ')) { i++; continue; }
        a = i; b = i; while (b < n && (IS_H(in[b]) || in[b] == L' ')) b++;
        i = b;
        if (a > 0) while (a < b && in[a] == L' ') a++;
        if (b < n) while (b > a && in[b - 1] == L' ') b--;
        h = 0; for (x = a; x < b; x++) if (IS_H(in[x])) { h = 1; break; }
        if (!h) continue;
        for (x = a, y = b - 1; x < b; x++, y--) out[x] = in[y];
    }
}

static const wchar_t* H_getbuf(void* self, int flag) {
    const wchar_t* r = g_next_getbuf(self, flag);
    if (flag == 0 && r && !IsBadReadPtr(r, 2)) {
        int i, n = wlen(r), h = 0;
        for (i = 0; i < n; i++) if (IS_H(r[i])) { h = 1; break; }
        if (h) {
            wchar_t* out = g_flipbuf[InterlockedIncrement(&g_flipi) & 3];
            flip_runs(r, out, 300);
            if (g_dir_logs < 40) { InterlockedIncrement(&g_dir_logs); LG("get-text: %d chars flipped to logical order", n); }
            return out;
        }
    }
    return r;
}

static int chk(DWORD va, DWORD w0, DWORD w1) { return *(volatile DWORD*)va == w0 && *(volatile DWORD*)(va + 4) == w1; }

static DWORD WINAPI Worker(LPVOID arg) {
    int tries;
    g_stage = (char)(INT_PTR)arg;
    for (tries = 0; tries < 150 && !table_ok(); tries++) Sleep(2000);
    if (!table_ok()) { LG("table never matched; nothing written"); return 0; }
    LG("table signature OK (after %d tries); row0[0]=%04x row1[0]=%04x row2[0]=%04x", tries, ROW0[0].c[0], ROW1[0].c[0], ROW2[0].c[0]);
    if (g_stage == 'A') { LG("stage A: read-only, done"); return 0; }
    {
        DWORD w0 = *(volatile DWORD*)0x419c36f0u, w1 = *(volatile DWORD*)0x419c36f4u;
        if (w0 != 0xe92d4010u || w1 != 0xe1a04000u) { LG("ABORT: SetPage bytes %08lx %08lx unexpected", w0, w1); return 0; }
    }
    write_count(F_ARMED, g_unstable);
    if (!exists(F_ARMED)) { LG("cannot create armed flag; refusing to hook"); return 0; }
    prepare(); g_ready = 1;
    { int rc = lyra_hook_install(0x419c36f0u, (void*)&H_setpage, (void**)&g_next); LG("hook SetPage rc=%d", rc);
      if (rc != 0 || !g_next) { DeleteFileW(F_ARMED); return 0; } }
    if (g_stage == 'D') {
        if (chk(0x419c3940u, 0xe92d4ff0u, 0xe24dd01cu) && chk(0x419c2914u, 0xe52de004u, 0xe1a02000u)) {
            int r1 = lyra_hook_install(0x419c2914u, (void*)&H_getbuf, (void**)&g_next_getbuf);
            int r2 = lyra_hook_install(0x419c3940u, (void*)&H_insert, (void**)&g_next_ins);
            LG("direction hooks: GetBuffer rc=%d, InsertText rc=%d", r1, r2);
            if (chk(0x419c37f8u, 0xe92d41f0u, 0xe1a08002u)) { int r3 = lyra_hook_install(0x419c37f8u, (void*)&H_setbuf, (void**)&g_next_setbuf); LG("hook SetBuffer rc=%d", r3); }
            else LG("SetBuffer prologue differs; not hooked");
            if (chk(0x419c6824u, 0xe92d41f0u, 0xe24dd050u)) { int r4 = lyra_hook_install(0x419c6824u, (void*)&H_sendkey, (void**)&g_next_sendkey); LG("hook SendKey rc=%d", r4); }
            else LG("SendKey prologue differs; not hooked");
        } else LG("direction hooks skipped: prologue bytes differ");
    }
    Sleep(ARM_SECONDS * 1000);
    DeleteFileW(F_ARMED);
    LG("stable for %d s, guard cleared", ARM_SECONDS);
    return 0;
}

__declspec(dllexport) int KbdHebInstall(void) {
    char stage; HANDLE t;
    LG("==== kbdheb v0.3 loaded (pid=%lu)", GetCurrentProcessId());
    if (exists(F_TRIPPED)) { LG("INERT: kbdheb.tripped exists (delete it to re-enable)"); return 0; }
    if (exists(F_ARMED)) {                            /* previous boot ended inside the guard window: tolerate a few quick restarts */
        g_unstable = read_count(F_ARMED) + 1;
        if (g_unstable >= 5) { LG("TRIPPED: %d unstable boots in a row", g_unstable); MoveFileW(F_ARMED, F_TRIPPED); return 0; }
        LG("previous boot ended inside the %d s window (unstable boot %d of 5 tolerated)", ARM_SECONDS, g_unstable);
    }
    stage = read_stage(); LG("stage=%c", stage);
    t = CreateThread(NULL, 0, Worker, (LPVOID)(INT_PTR)stage, 0, NULL);
    if (t) CloseHandle(t);
    return 0;
}
BOOL WINAPI DllMain(HANDLE h, DWORD r, LPVOID l) { (void)h; (void)r; (void)l; return TRUE; }
