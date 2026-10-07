/* rtlflip v0.2: Hebrew visual-order flip for the Zune UI (gemstone, firmware 4.5), staged and self-disarming.
 *
 * Only hooks 0x38434 SetLabelText(elem, text): its first two instructions are position-independent
 * (checked offline with hookcheck.py, and re-verified here against the exact expected words before patching).
 * The old 0x83914 hook is gone: it was a PC-relative import thunk to a generic string copy and crashed the UI.
 *
 * Stage is read from \flash2\automation\rtlflip.stage (first char), default A:
 *   A = guard + log only, NO hooks installed
 *   B = hook installed, text passed through unchanged, Hebrew strings logged
 *   C = hook installed, Hebrew text flipped
 *   G = Q plus: the getter's output for property ids 0x20001/2/3 (title/artist/album, seen in Q logs) is flipped in place when it has Hebrew
 *   Q = R plus a LOG-ONLY hook on gemstone 0x748d0, the metadata-property getter GetStr(obj, propId, buf, cch, extra): after each call
 *       it logs caller, property id and any Hebrew result. Used to learn which property ids are display text.
 *   R = P plus: Hebrew text passing through the copy at an ALLOWED call site (ROW_LR[]) is flipped (once; memo-guarded)
 *   P = C plus a LOG-ONLY probe on the string-copy import (gemstone IAT slot 0x962a8): records which callers (LR) pass Hebrew
 *       text through it. It never changes the text. Used to find which of the 88 call sites are list rows.
 * Guard: before patching, \flash2\automation\rtlflip.armed is created. It is deleted after ARM_SECONDS of uptime.
 * If it still exists at the next load (= the UI died inside that window) no hook is installed, the file is renamed
 * to rtlflip.tripped and the mod stays inert until that file is deleted. */
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include "lyra.h"
#include "flip_core.h"

#define VA_SET_LABEL   0x00038434u
#define WORD0_EXPECT   0xe92d4010u   /* push {r4, lr}          */
#define WORD1_EXPECT   0xe24dd048u   /* sub sp, sp, #0x48      */
#define ARM_SECONDS    180
#define MAX_UNSTABLE   5             /* consecutive unstable boots tolerated before the mod goes inert */
#define LOG_LIMIT      400
#define VA_PROP        0x000748d0u   /* metadata string getter, 5 args, 112 callers; first two instructions checked offline (hookcheck.py) */
#define PROP_W0        0xe92d4030u   /* push {r4, r5, lr} */
#define PROP_W1        0xe24dd00cu   /* sub sp, sp, #0xc  */
#define IAT_COPY       0x000962a8u   /* gemstone import slot -> coredll StringCchCopyW(dest, cch, src), 3 args, checked offline */
#define IAT_COPY_EXPECT 0x4035d744u
#define IAT_COPYEX     0x00096228u   /* -> coredll StringCchCopyExW(dest, cch, src, pEnd, pRemain, flags): 6 args, checked offline */
#define IAT_COPYEX_EXPECT 0x4035d790u
#define RING           32

#define DIR       L"\\flash2\\automation\\"
#define F_LOG     DIR L"rtlflip.log"
#define F_STAGE   DIR L"rtlflip.stage"
#define F_ARMED   DIR L"rtlflip.armed"
#define F_TRIPPED DIR L"rtlflip.tripped"

typedef HRESULT (*LabelFn)(void* elem, const wchar_t* text);
static LabelFn g_next;
static volatile LONG g_logged, g_ring;
static volatile int g_flip;                       /* 1 only in stage C */
static wchar_t g_buf[RING][FLIP_MAX];             /* permanent copies: never freed, so no dangling pointer possible */

static void LG(const char* fmt, ...) {
    char buf[300]; int n; DWORD w; HANDLE h; va_list ap;
    va_start(ap, fmt); n = vsnprintf(buf, sizeof(buf) - 3, fmt, ap); va_end(ap);
    if (n < 0) n = 0;
    buf[n++] = '\r'; buf[n++] = '\n';
    h = CreateFileW(F_LOG, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, NULL, FILE_END);
    WriteFile(h, buf, n, &w, NULL);
    CloseHandle(h);
}

static int exists(const wchar_t* p) { return GetFileAttributesW(p) != 0xFFFFFFFFu; }

static int read_count(const wchar_t* p) {               /* the armed file holds one byte: unstable boots seen so far */
    unsigned char c = 0; DWORD got = 0;
    HANDLE h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    if (!ReadFile(h, &c, 1, &got, NULL) || got != 1) c = 0;
    CloseHandle(h);
    return c;
}

static void write_count(const wchar_t* p, int n) {
    unsigned char c = (unsigned char)n; DWORD w;
    HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) { WriteFile(h, &c, 1, &w, NULL); CloseHandle(h); }
}

static void touch(const wchar_t* p) {
    HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

static char read_stage(void) {
    char c = 'A'; DWORD got = 0;
    HANDLE h = CreateFileW(F_STAGE, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 'A';
    if (ReadFile(h, &c, 1, &got, NULL) && got == 1 && c >= 'a' && c <= 'z') c -= 32;
    CloseHandle(h);
    return (got == 1 && (c == 'B' || c == 'C' || c == 'P' || c == 'R' || c == 'Q' || c == 'G')) ? c : 'A';
}

static volatile LONG g_n_label, g_n_flip, g_n_skip;
static unsigned g_seen[512]; static volatile LONG g_seen_n;
/* returns 1 the first time a key is seen (so each distinct line is logged once) */
static int first_time(unsigned key) {
    int i, n = g_seen_n;
    for (i = 0; i < n && i < 512; i++) if (g_seen[i] == key) return 0;
    if (n >= 512) return 0;
    g_seen[InterlockedIncrement(&g_seen_n) - 1 & 511] = key;
    return 1;
}
static void u8(const wchar_t* w, char* o, int cap) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, o, cap - 1, NULL, NULL);
    if (n < 0) n = 0; o[n] = 0;
}
static void log_text(const wchar_t* in, const wchar_t* out) {
    char a[160], b[160]; int l; unsigned k = memo_hash(in, &l);
    if (!first_time(k) || g_logged >= LOG_LIMIT) return;
    InterlockedIncrement(&g_logged);
    u8(in, a, sizeof a); u8(out, b, sizeof b);
    LG("label[%s] in=\"%s\" out=\"%s\"", g_flip ? "flip" : "pass", a, b);
}

static HRESULT H_label(void* elem, const wchar_t* text) {
    if (InterlockedIncrement(&g_n_label) % 300 == 0) LG("stats label=%ld flip=%ld skip=%ld", g_n_label, g_n_flip, g_n_skip);
    if (text && !((DWORD)text & 1)) {
        /* flip only when there is Hebrew; rtl_flip leaves out[] untouched and returns 0 otherwise */
        wchar_t* out = g_buf[(InterlockedIncrement(&g_ring) & 0x7fffffff) % RING];
        if (memo_has(text)) {                        /* already our output: it is in visual order, leave it */
            InterlockedIncrement(&g_n_skip);
            { int l; unsigned k = memo_hash(text, &l) ^ 0x5a5a5a5au;
              if (first_time(k) && g_logged < LOG_LIMIT) { char a[160]; InterlockedIncrement(&g_logged); u8(text, a, sizeof a); LG("label[skip] already-flipped: \"%s\"", a); } }
        } else if (rtl_flip(text, out, FLIP_MAX)) {
            log_text(text, out);
            if (g_flip) { InterlockedIncrement(&g_n_flip); memo_add(out); return g_next(elem, out); }
        }
    }
    return g_next(elem, text);
}

/* ---- stage P: log-only probe on the string-copy import ---- */
typedef DWORD (*CopyFn)(DWORD dest, DWORD cch, DWORD src);
static CopyFn g_copy_orig;
static volatile int g_row_flip;                       /* stage R */
static const DWORD ROW_LR[] = { 0x00027de8u };        /* return addresses of list-row copy sites (from probe logs) */
static volatile LONG g_n_row;
static wchar_t g_rowbuf[RING][FLIP_MAX];
static volatile LONG g_rowring;
static volatile LONG g_probe_logged;
static void probe(const char* tag, const wchar_t* t, DWORD lr, DWORD cch) {
    int i, heb = 0, n; unsigned k; char a[160];
    if (!t || ((DWORD)t & 1) || (DWORD)t < 0x10000 || IsBadReadPtr(t, 2)) return;
    for (i = 0; i < 64 && t[i]; i++) if (IS_HEB(t[i])) { heb = 1; break; }
    if (!heb) return;
    k = memo_hash(t, &n) * 31u + lr + (tag[4] == 'e' ? 7u : 0u);
    if (!first_time(k ^ 0xc0ffee00u) || g_probe_logged >= 250) return;
    InterlockedIncrement(&g_probe_logged);
    u8(t, a, sizeof a);
    LG("%s lr=%08lx cch=%lu in=\"%s\"", tag, lr, cch, a);
}
static DWORD W_copy(DWORD dest, DWORD cch, DWORD src) {
    DWORD lr = (DWORD)__builtin_return_address(0);
    probe("copy ", (const wchar_t*)src, lr, cch);
    if (g_row_flip && src && !(src & 1) && src >= 0x10000 && !IsBadReadPtr((void*)src, 2)) {
        int i;
        for (i = 0; i < (int)(sizeof ROW_LR / sizeof ROW_LR[0]); i++) if (ROW_LR[i] == lr) {
            const wchar_t* t = (const wchar_t*)src;
            wchar_t* out = g_rowbuf[(InterlockedIncrement(&g_rowring) & 0x7fffffff) % RING];
            if (!memo_has(t) && rtl_flip(t, out, FLIP_MAX) && wcslen(out) < cch) {   /* callee copies at once; output never longer than input */
                InterlockedIncrement(&g_n_row); memo_add(out);
                if ((g_n_row & 63) == 1) LG("row-flip #%ld at lr=%08lx", g_n_row, lr);
                return g_copy_orig(dest, cch, (DWORD)out);
            }
            break;
        }
    }
    return g_copy_orig(dest, cch, src);
}

typedef DWORD (*CopyExFn)(DWORD, DWORD, DWORD, DWORD, DWORD, DWORD);
static CopyExFn g_copyex_orig;
static const DWORD EXROW_LR[] = { 0x000295d4u };      /* now-playing "next songs" builder: one title per call, joined with 
 by the caller */
static volatile LONG g_n_exrow;
static DWORD W_copyex(DWORD dest, DWORD cch, DWORD src, DWORD pend, DWORD prem, DWORD flags) {
    DWORD lr = (DWORD)__builtin_return_address(0);
    probe("copyex", (const wchar_t*)src, lr, cch);
    if (g_row_flip && src && !(src & 1) && src >= 0x10000 && !IsBadReadPtr((void*)src, 2)) {
        int i;
        for (i = 0; i < (int)(sizeof EXROW_LR / sizeof EXROW_LR[0]); i++) if (EXROW_LR[i] == lr) {
            const wchar_t* t = (const wchar_t*)src;
            wchar_t* out = g_rowbuf[(InterlockedIncrement(&g_rowring) & 0x7fffffff) % RING];
            if (!memo_has(t) && rtl_flip(t, out, FLIP_MAX) && wcslen(out) < cch) {   /* same length as input, copied at once */
                InterlockedIncrement(&g_n_exrow); memo_add(out);
                if ((g_n_exrow & 63) == 1) LG("exrow-flip #%ld at lr=%08lx", g_n_exrow, lr);
                return g_copyex_orig(dest, cch, (DWORD)out, pend, prem, flags);
            }
            break;
        }
    }
    return g_copyex_orig(dest, cch, src, pend, prem, flags);
}

/* ---- stage Q: log-only hook on the metadata-property getter ---- */
typedef HRESULT (*PropFn)(DWORD obj, DWORD prop, wchar_t* buf, DWORD cch, DWORD extra);
static PropFn g_prop_next;
static volatile int g_prop_flip;                     /* stage G */
static volatile LONG g_n_prop;
static wchar_t g_propbuf[FLIP_MAX];                  /* only touched on the calling thread's stack frame order; see lock below */
static CRITICAL_SECTION g_prop_cs; static volatile int g_prop_cs_ok;
static volatile LONG g_prop_logged;
static HRESULT H_prop(DWORD obj, DWORD prop, wchar_t* buf, DWORD cch, DWORD extra) {
    DWORD lr = (DWORD)__builtin_return_address(0);
    HRESULT r = g_prop_next(obj, prop, buf, cch, extra);
    if (g_prop_flip && (int)r >= 0 && buf && cch && cch < FLIP_MAX && !((DWORD)buf & 1) && prop >= 0x20001u && prop <= 0x20003u && !memo_has(buf)) {
        /* flip in place: output has the same length and fits the caller's buffer */
        if (g_prop_cs_ok) {
            EnterCriticalSection(&g_prop_cs);
            if (rtl_flip(buf, g_propbuf, FLIP_MAX)) {
                int l = 0; while (g_propbuf[l]) l++;
                if ((DWORD)l < cch) { memcpy(buf, g_propbuf, (l + 1) * sizeof(wchar_t)); memo_add(buf); InterlockedIncrement(&g_n_prop); }
            }
            LeaveCriticalSection(&g_prop_cs);
            if ((g_n_prop & 127) == 1 && g_n_prop) LG("prop-flip #%ld id=%08lx lr=%08lx", g_n_prop, prop, lr);
        }
        return r;
    }
    if ((int)r >= 0 && buf && cch && cch <= 4096 && !((DWORD)buf & 1)) {
        DWORD i; int heb = 0;
        for (i = 0; i < cch && i < 64 && buf[i]; i++) if (IS_HEB(buf[i])) { heb = 1; break; }
        if (heb) {
            int n; unsigned k = memo_hash(buf, &n) * 31u + lr + prop * 131u;
            if (first_time(k ^ 0x70726f70u) && g_prop_logged < 300) {
                char a[160]; InterlockedIncrement(&g_prop_logged); u8(buf, a, sizeof a);
                LG("prop lr=%08lx id=%08lx cch=%lu out=\"%s\"", lr, prop, cch, a);
            }
        }
    }
    return r;
}

/* Clears the armed flag after ARM_SECONDS of healthy uptime. */
static DWORD WINAPI Disarm(LPVOID p) {
    (void)p;
    Sleep(ARM_SECONDS * 1000);
    DeleteFileW(F_ARMED);
    LG("disarmed after %d s of uptime: stable", ARM_SECONDS);
    return 0;
}

__declspec(dllexport) int RtlFlipInstall(void) {
    char stage; DWORD w0, w1; HANDLE t; int prev_unstable = 0;
    LG("==== RtlFlipInstall v0.2 loaded (pid=%lu) runtime=%d", GetCurrentProcessId(), lyra_runtime_available());

    if (exists(F_TRIPPED)) { LG("INERT: rtlflip.tripped exists (delete it to re-enable). no hooks."); return 0; }
    if (exists(F_ARMED)) {                          /* previous boot did not stay up ARM_SECONDS */
        prev_unstable = read_count(F_ARMED) + 1;
        if (prev_unstable >= MAX_UNSTABLE) {
            LG("TRIPPED: %d consecutive unstable boots (each ended inside the %d s window). no hooks.", prev_unstable, ARM_SECONDS);
            MoveFileW(F_ARMED, F_TRIPPED);
            return 0;
        }
        LG("previous boot ended inside the %d s window (unstable boot %d of %d tolerated); hooking again", ARM_SECONDS, prev_unstable, MAX_UNSTABLE);
    }
    stage = read_stage();
    LG("stage=%c", stage);
    if (stage == 'A') return 0;                    /* guard-only: proves loading + file handling, patches nothing */

    w0 = *(volatile DWORD*)VA_SET_LABEL;           /* gemstone's own code, always mapped in this process */
    w1 = *(volatile DWORD*)(VA_SET_LABEL + 4);
    if (w0 != WORD0_EXPECT || w1 != WORD1_EXPECT) {
        LG("ABORT: target bytes %08lx %08lx != expected (different firmware?). no hooks.", w0, w1);
        return -1;
    }
    g_flip = (stage == 'C' || stage == 'P' || stage == 'R' || stage == 'Q' || stage == 'G');
    write_count(F_ARMED, prev_unstable);
    if (!exists(F_ARMED)) { LG("ABORT: cannot create armed flag, refusing to hook without the guard"); return -1; }
    {
        int rc = lyra_hook_install(VA_SET_LABEL, (void*)&H_label, (void**)&g_next);
        LG("hook SetLabelText rc=%d flip=%d", rc, g_flip);
        if (rc != 0 || !g_next) { DeleteFileW(F_ARMED); return -1; }
    }
    if (stage == 'P' || stage == 'R' || stage == 'Q' || stage == 'G') {
        g_row_flip = (stage == 'R' || stage == 'Q' || stage == 'G');
        DWORD cur = *(volatile DWORD*)IAT_COPY;
        if (cur != IAT_COPY_EXPECT) LG("probe skipped: IAT slot holds %08lx, expected %08lx", cur, IAT_COPY_EXPECT);
        else {
            g_copy_orig = (CopyFn)cur;                     /* set before redirecting: a call landing on the wrapper must already see it */
            *(volatile DWORD*)IAT_COPY = (DWORD)&W_copy;
            LG("probe installed on string-copy import");
        }
        cur = *(volatile DWORD*)IAT_COPYEX;
        if (cur != IAT_COPYEX_EXPECT) LG("copyex probe skipped: IAT slot holds %08lx, expected %08lx", cur, IAT_COPYEX_EXPECT);
        else {
            g_copyex_orig = (CopyExFn)cur;
            *(volatile DWORD*)IAT_COPYEX = (DWORD)&W_copyex;
            LG("probe installed on string-copy-ex import (log only)");
        }
    }
    if (stage == 'Q' || stage == 'G') {
        InitializeCriticalSection(&g_prop_cs); g_prop_cs_ok = 1; g_prop_flip = (stage == 'G');
        if (*(volatile DWORD*)VA_PROP != PROP_W0 || *(volatile DWORD*)(VA_PROP + 4) != PROP_W1) LG("prop probe skipped: target bytes differ");
        else {
            int rc = lyra_hook_install(VA_PROP, (void*)&H_prop, (void**)&g_prop_next);
            LG("prop hook rc=%d flip=%d", rc, g_prop_flip);
        }
    }
    t = CreateThread(NULL, 0, Disarm, NULL, 0, NULL);
    if (t) CloseHandle(t); else LG("WARNING: no disarm thread; armed flag stays, next boot will be inert");
    return 0;
}

BOOL WINAPI DllMain(HANDLE h, DWORD r, LPVOID l) { (void)h; (void)r; (void)l; return TRUE; }
