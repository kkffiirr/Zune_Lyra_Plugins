/* fontd: Lyra mod daemon (manifest "daemons"), spawned in boot phase 1, before the UI loads its fonts.
 * Registers the Hebrew-extended Zegoe UI fonts via AddFontResourceW, retrying until GDI accepts them,
 * then stays resident (fonts added by a process may be dropped when it exits). */
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>

#define DIR     L"\\flash2\\automation\\mods\\hebrew-font\\"
#define LOGPATH L"\\flash2\\automation\\fontd.log"

static void LG(const char* fmt, ...) {
    char buf[300]; int n; DWORD w; SYSTEMTIME t; HANDLE h; va_list ap;
    GetLocalTime(&t);
    n = sprintf(buf, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_start(ap, fmt); n += vsnprintf(buf + n, sizeof(buf) - 4 - n, fmt, ap); va_end(ap);
    buf[n++] = '\r'; buf[n++] = '\n';
    h = CreateFileW(LOGPATH, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, NULL, FILE_END);
    WriteFile(h, buf, n, &w, NULL);
    CloseHandle(h);
}

static const wchar_t* const FILES[] = {
    L"ZegoeUI.ttf", L"ZegoeUI_L.ttf", L"ZegoeUI_SL.ttf",
    L"ZegoeUI_SB.ttf", L"ZegoeUI_B.ttf", L"ZegoeUI_Blk.ttf",
    L"ZegoeHeb_R.ttf", L"ZegoeHeb_SL.ttf"
};
#define NF ((int)(sizeof FILES / sizeof FILES[0]))

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPWSTR cmd, int show) {
    int added[NF] = {0}, done = 0, tries, i; wchar_t path[MAX_PATH];
    (void)hi; (void)hp; (void)cmd; (void)show;
    LG("==== fontd started (pid=%lu)", GetCurrentProcessId());
    for (tries = 0; tries < 120 && done < NF; tries++) {
        for (i = 0; i < NF; i++) {
            int r;
            if (added[i]) continue;
            wcscpy(path, DIR); wcscat(path, FILES[i]);
            SetLastError(0);
            r = AddFontResourceW(path);
            if (r > 0) { added[i] = 1; done++; LG("added font %d on try %d (r=%d)", i, tries, r); }
        }
        if (done < NF) Sleep(250);
    }
    LG("==== %d/%d fonts added after %d tries", done, NF, tries);
    if (done) PostMessageW(HWND_BROADCAST, WM_FONTCHANGE, 0, 0);
    for (;;) Sleep(60000);
    return 0;
}

/* console-subsystem builds start at main(); both entry points run the same code */
int main(void) { return WinMain(NULL, NULL, NULL, 0); }
