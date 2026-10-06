/* fontprobe: diagnostic-only Lyra mod DLL (load_module into gemstone, init FontProbeInstall).
 * Logs which fonts GDI knows, whether the ROM font files are visible, and whether
 * AddFontResourceW accepts a test font at runtime. Replaces nothing. */
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>

#define LOGPATH L"\\flash2\\automation\\fontprobe.log"
#define TESTFONT L"\\flash2\\automation\\mods\\fontprobe\\ZegoeHebTest.ttf"

static void LG(const char* fmt, ...) {
    char buf[400]; int n; DWORD w; SYSTEMTIME t; HANDLE h; va_list ap;
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

static int g_count;
static int CALLBACK EnumCb(const LOGFONTW* lf, const TEXTMETRICW* tm, DWORD type, LPARAM lp) {
    char name[80]; int i;
    (void)tm; (void)lp;
    for (i = 0; i < 79 && lf->lfFaceName[i]; i++) name[i] = (char)lf->lfFaceName[i];
    name[i] = 0;
    LG("   font[%d]: %s (type=0x%lx charset=%d)", g_count, name, type, lf->lfCharSet);
    return ++g_count < 80;
}

static void dump_fonts(const char* tag) {
    HDC dc = GetDC(NULL);
    LG("%s: dc=%p", tag, dc);
    g_count = 0;
    if (dc) { EnumFontFamiliesW(dc, NULL, (FONTENUMPROCW)EnumCb, 0); ReleaseDC(NULL, dc); }
    LG("%s: %d font(s) listed", tag, g_count);
}

static void exists(const wchar_t* p) {
    DWORD a = GetFileAttributesW(p);
    char n[120]; int i; for (i = 0; i < 119 && p[i]; i++) n[i] = (char)p[i]; n[i] = 0;
    LG("   file %s -> attr=0x%lx err=%lu", n, a, a == 0xFFFFFFFF ? GetLastError() : 0);
}

__declspec(dllexport) int FontProbeInstall(void) {
    int r;
    LG("==== FontProbeInstall: loaded (pid=%lu)", GetCurrentProcessId());
    exists(L"\\Windows\\ZegoeUI.ttf");
    exists(L"\\Windows\\ZegoeUI_B.ttf");
    exists(L"\\Windows\\arial.ttf");
    exists(L"\\Windows\\MeiryoForZune.ttf");
    exists(TESTFONT);
    dump_fonts("before");
    SetLastError(0);
    r = AddFontResourceW(TESTFONT);
    LG("AddFontResourceW(test) -> %d err=%lu", r, GetLastError());
    dump_fonts("after");
    LG("==== done");
    return 0;
}

BOOL WINAPI DllMain(HANDLE h, DWORD r, LPVOID l) { (void)h; (void)r; (void)l; return TRUE; }
