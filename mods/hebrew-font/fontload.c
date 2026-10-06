/* fontload v5: Lyra mod DLL (load_module into gemstone, init FontLoadInstall).
 *  1. checks which Zegoe weights draw real Hebrew (pixel comparison against the missing-glyph box)
 *  2. adds the Hebrew fonts itself if the boot-time helper (fontd.exe) did not
 *  3. redirects the UI's font creation for the two weights that cannot be overridden by name
 *     ("Zegoe UI" and "Zegoe UI SemiLight") to uniquely named Hebrew copies, by patching the
 *     import-table slots that point at coredll's CreateFontIndirectW / CreateFontW.
 *     Every distinct font request is logged so we learn what the UI asks for. */
#include <windows.h>
#include <tlhelp32.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define DIR     L"\\flash2\\automation\\mods\\hebrew-font\\"
#define LOGPATH L"\\flash2\\automation\\fontload.log"

static void LG(const char* fmt, ...) {
    char buf[300]; int n; DWORD w; SYSTEMTIME t; HANDLE h; va_list ap;
    GetLocalTime(&t);
    n = sprintf(buf, "[%02d:%02d:%02d] ", t.wHour, t.wMinute, t.wSecond);
    va_start(ap, fmt); n += vsnprintf(buf + n, sizeof(buf) - 4 - n, fmt, ap); va_end(ap);
    buf[n++] = '\r'; buf[n++] = '\n';
    h = CreateFileW(LOGPATH, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, NULL, FILE_END);
    WriteFile(h, buf, n, &w, NULL);
    CloseHandle(h);
}

/* ---- pixel check ------------------------------------------------------------------------ */
static int g_h = -48, g_w = FW_NORMAL, g_cs = DEFAULT_CHARSET;
static unsigned render_hash(const wchar_t* face, UINT cp, int* lit) {
    HDC sdc = GetDC(NULL), dc; HBITMAP bm, ob; HFONT f, of; LOGFONTW lf; wchar_t s[2];
    unsigned h = 2166136261u; int x, y; *lit = -1;
    if (!sdc) return 0;
    dc = CreateCompatibleDC(sdc); bm = CreateCompatibleBitmap(sdc, 64, 64);
    if (!dc || !bm) { ReleaseDC(NULL, sdc); return 0; }
    ob = (HBITMAP)SelectObject(dc, bm);
    PatBlt(dc, 0, 0, 64, 64, BLACKNESS);
    memset(&lf, 0, sizeof lf);
    lf.lfHeight = g_h; lf.lfWeight = g_w; lf.lfCharSet = (BYTE)g_cs;
    wcsncpy(lf.lfFaceName, face, 31);
    f = CreateFontIndirectW(&lf); of = (HFONT)SelectObject(dc, f);
    SetTextColor(dc, RGB(255, 255, 255)); SetBkMode(dc, TRANSPARENT);
    s[0] = (wchar_t)cp; s[1] = 0;
    ExtTextOutW(dc, 6, 2, 0, NULL, s, 1, NULL);
    *lit = 0;
    for (y = 0; y < 64; y++) for (x = 0; x < 64; x++) {
        COLORREF c = GetPixel(dc, x, y);
        if (c & 0xFF) (*lit)++;
        h = (h ^ (c & 0xFF)) * 16777619u;
    }
    SelectObject(dc, of); DeleteObject(f); SelectObject(dc, ob); DeleteObject(bm);
    DeleteDC(dc); ReleaseDC(NULL, sdc);
    return h;
}

static void fname(const wchar_t* f, char* o, int cap) { int k; for (k = 0; k < cap - 1 && f[k]; k++) o[k] = (char)f[k]; o[k] = 0; }

static int has_alef(const char* tag, const wchar_t* face, unsigned boxhash) {
    int lit; unsigned h = render_hash(face, 0x5D0, &lit); char fn[40];
    fname(face, fn, sizeof fn);
    LG("%s: %-20s alef lit=%d -> %s", tag, fn, lit, h == boxhash ? "BOX" : "HEBREW");
    return h != boxhash;
}

static const wchar_t* const FACES[] = {
    L"Zegoe UI", L"Zegoe UI Light", L"Zegoe UI SemiLight", L"Zegoe UI Semibold", L"Zegoe UI Bold", L"Zegoe UI Black",
    L"Zegoe UH", L"Zegoe UH SemiLight",
    L"Arial", L"Arial Black", L"Courier New", L"Times New Roman", L"Verdana", L"Georgia", L"Impact", L"Trebuchet MS",
    L"Malgun for Zune", L"Meiryo for Zune"
};
#define NFACE ((int)(sizeof FACES / sizeof FACES[0]))

static const wchar_t* const FILES[] = {
    L"ZegoeUI.ttf", L"ZegoeUI_L.ttf", L"ZegoeUI_SL.ttf", L"ZegoeUI_SB.ttf", L"ZegoeUI_B.ttf", L"ZegoeUI_Blk.ttf",
    L"ZegoeHeb_R.ttf", L"ZegoeHeb_SL.ttf"
};
#define NFILE ((int)(sizeof FILES / sizeof FILES[0]))

/* ---- font-creation redirect ------------------------------------------------------------- */
typedef HFONT (WINAPI *CfiFn)(const LOGFONTW*);
typedef HFONT (WINAPI *CfwFn)(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, LPCWSTR);
static CfiFn g_cfi;
static CfwFn g_cfw;

static volatile int g_ok_r, g_ok_sl;   /* set only after the renamed copy was verified to draw Hebrew */

static const wchar_t* redirect_face(const wchar_t* in) {
    if (!in) return in;
    if (g_ok_r  && !wcsicmp(in, L"Zegoe UI"))           return L"ZegoeHeb UI";
    if (g_ok_sl && !wcsicmp(in, L"Zegoe UI SemiLight")) return L"ZegoeHeb SemiLight";
    return in;
}

#define SEEN_MAX 64
static unsigned g_seen[SEEN_MAX]; static volatile LONG g_nseen;
static void note_request(const char* api, const wchar_t* face, int weight, int height) {
    unsigned k = 5381; const wchar_t* p = face; int i; char fn[40];
    if (!face) return;
    while (*p) k = k * 33 + *p++;
    k = k * 33 + (unsigned)weight;
    for (i = 0; i < g_nseen && i < SEEN_MAX; i++) if (g_seen[i] == k) return;
    if (g_nseen >= SEEN_MAX) return;
    g_seen[g_nseen++] = k;
    fname(face, fn, sizeof fn);
    LG("request %s: face=\"%s\" weight=%d height=%d%s", api, fn, weight, height, redirect_face(face) != face ? "  -> REDIRECT" : "");
}

static HFONT WINAPI H_cfi(const LOGFONTW* lf) {
    LOGFONTW m;
    if (lf) {
        const wchar_t* r;
        note_request("CreateFontIndirectW", lf->lfFaceName, lf->lfWeight, lf->lfHeight);
        r = redirect_face(lf->lfFaceName);
        if (r != lf->lfFaceName) { m = *lf; wcsncpy(m.lfFaceName, r, 31); m.lfFaceName[31] = 0; return g_cfi(&m); }
    }
    return g_cfi(lf);
}

static HFONT WINAPI H_cfw(int h, int w, int esc, int ori, int wt, DWORD it, DWORD ul, DWORD so, DWORD cs,
                          DWORD op, DWORD cp, DWORD q, DWORD pf, LPCWSTR face) {
    note_request("CreateFontW", face, wt, h);
    return g_cfw(h, w, esc, ori, wt, it, ul, so, cs, op, cp, q, pf, redirect_face(face));
}

/* patch every import-table slot in this process that holds `real` with `repl`; returns slots patched */
static int patch_iat(void* real, void* repl, const char* what) {
    HANDLE snap; MODULEENTRY32 me; int total = 0;
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) { LG("patch %s: snapshot failed err=%lu", what, GetLastError()); return 0; }
    me.dwSize = sizeof me;
    if (Module32First(snap, &me)) do {
        BYTE* base = me.modBaseAddr; IMAGE_DOS_HEADER* dos; IMAGE_NT_HEADERS* nt; IMAGE_IMPORT_DESCRIPTOR* imp;
        DWORD rva, hits = 0; char mn[40];
        fname(me.szModule, mn, sizeof mn);
        if (IsBadReadPtr(base, sizeof *dos)) continue;
        dos = (IMAGE_DOS_HEADER*)base;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || IsBadReadPtr(base + dos->e_lfanew, sizeof *nt)) continue;
        nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) continue;
        rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
        if (!rva || IsBadReadPtr(base + rva, sizeof *imp)) continue;
        for (imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + rva); imp->Name && !IsBadReadPtr(imp, sizeof *imp); imp++) {
            DWORD* slot = (DWORD*)(base + imp->FirstThunk);
            if (!imp->FirstThunk) continue;
            for (; !IsBadReadPtr(slot, 4) && *slot; slot++) {
                if (*slot == (DWORD)real) {
                    DWORD old;
                    if (VirtualProtect(slot, 4, PAGE_READWRITE, &old)) { *slot = (DWORD)repl; VirtualProtect(slot, 4, old, &old); hits++; }
                    else LG("patch %s: VirtualProtect failed in %s err=%lu", what, mn, GetLastError());
                }
            }
        }
        if (hits) { LG("patch %s: %lu slot(s) in %s", what, hits, mn); total += (int)hits; }
    } while (Module32Next(snap, &me));
    CloseToolhelp32Snapshot(snap);
    return total;
}

static void install_redirects(void) {
    HMODULE core = GetModuleHandleW(L"coredll.dll");
    int n1 = 0, n2 = 0;
    if (!core) { LG("redirect: no coredll handle"); return; }
    g_cfi = (CfiFn)GetProcAddressW(core, L"CreateFontIndirectW");
    g_cfw = (CfwFn)GetProcAddressW(core, L"CreateFontW");
    LG("redirect: CreateFontIndirectW=%p CreateFontW=%p", (void*)g_cfi, (void*)g_cfw);
    if (g_cfi) n1 = patch_iat((void*)g_cfi, (void*)&H_cfi, "CreateFontIndirectW");
    if (g_cfw) n2 = patch_iat((void*)g_cfw, (void*)&H_cfw, "CreateFontW");
    LG("redirect: patched %d + %d slot(s)", n1, n2);
}

static void matrix_test(const wchar_t* face, unsigned boxhash) {
    static const int HS[] = { -48, -29, -17, 30 };
    static const int WS[] = { 0, 300, 400, 500, 700 };
    static const int CS[] = { 0, 1, 177 };      /* ANSI, DEFAULT, HEBREW */
    int hi, wi, ci, lit; char fn[40], row[200];
    fname(face, fn, sizeof fn);
    for (ci = 0; ci < 3; ci++) for (hi = 0; hi < 4; hi++) {
        int n = 0;
        for (wi = 0; wi < 5; wi++) {
            unsigned h;
            g_h = HS[hi]; g_w = WS[wi]; g_cs = CS[ci];
            h = render_hash(face, 0x5D0, &lit);
            n += sprintf(row + n, " w%d=%c", WS[wi], h == boxhash ? 'B' : 'H');
        }
        LG("matrix %s charset=%d height=%d:%s", fn, CS[ci], HS[hi], row);
    }
    g_h = -48; g_w = FW_NORMAL; g_cs = DEFAULT_CHARSET;
}

static void log_stock(unsigned boxhash) {
    static const struct { int id; const char* nm; } ST[] = { {SYSTEM_FONT, "SYSTEM_FONT"}, {DEFAULT_GUI_FONT, "DEFAULT_GUI_FONT"} };
    int i;
    for (i = 0; i < 2; i++) {
        LOGFONTW lf; HGDIOBJ o = GetStockObject(ST[i].id); char fn[40]; int lit; unsigned h;
        if (!o || !GetObjectW(o, sizeof lf, &lf)) { LG("stock %s: unavailable", ST[i].nm); continue; }
        fname(lf.lfFaceName, fn, sizeof fn);
        h = render_hash(lf.lfFaceName, 0x5D0, &lit);
        LG("stock %s: face=\"%s\" weight=%ld height=%ld -> alef %s", ST[i].nm, fn, (long)lf.lfWeight, (long)lf.lfHeight, h == boxhash ? "BOX" : "HEBREW");
    }
}

__declspec(dllexport) int FontLoadInstall(void) {
    int i, ok = 0, lit, missing = 0; wchar_t path[MAX_PATH]; unsigned box;
    LG("==== FontLoadInstall v5: loaded (pid=%lu)", GetCurrentProcessId());
    box = render_hash(L"Zegoe UI", 0xE000, &lit);
    for (i = 0; i < 8; i++) if (!has_alef("state", FACES[i], box)) missing++;
    if (missing) {
        LG("%d face(s) show the box: adding fonts from here as a fallback", missing);
        for (i = 0; i < NFILE; i++) {
            int r;
            wcscpy(path, DIR); wcscat(path, FILES[i]);
            r = AddFontResourceW(path);
            LG("AddFontResourceW(%d) -> %d err=%lu", i, r, GetLastError());
            if (r > 0) ok++;
        }
        for (i = 0; i < NFACE; i++) has_alef("after", FACES[i], box);
        if (ok) PostMessageW(HWND_BROADCAST, WM_FONTCHANGE, 0, 0);
    }
#ifdef ENABLE_REDIRECT
    g_ok_r  = has_alef("verify", L"ZegoeHeb UI", box);
    g_ok_sl = has_alef("verify", L"ZegoeHeb SemiLight", box);
    if (g_ok_r || g_ok_sl) install_redirects();
    else LG("redirect: renamed fonts do not draw Hebrew, redirect NOT installed");
#else
    LG("redirect: disabled in this build (untested; enable with -DENABLE_REDIRECT)");
#endif
    LG("==== done");
    return 0;
}

BOOL WINAPI DllMain(HANDLE h, DWORD r, LPVOID l) { (void)h; (void)r; (void)l; return TRUE; }
