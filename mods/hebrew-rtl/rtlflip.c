/* rtlflip: Lyra mod DLL (load_module into gemstone, init RtlFlipInstall), firmware 4.5.
 * Hooks the UI's label-text setter (0x38434) and list-row text setter (0x83914); when the text
 * contains Hebrew, passes the visually reordered text instead. Latin-only text is passed untouched. */
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include "lyra.h"
#include "flip_core.h"

#define VA_SET_LABEL 0x00038434u   /* HRESULT SetLabelText(void* elem, const wchar_t* text) */
#define VA_ROW_LABEL 0x00083914u   /* HRESULT RawRowLabel(DWORD out8, DWORD outc, const wchar_t* text) */
#define LOGPATH L"\\flash2\\automation\\rtlflip.log"
#define LOG_LIMIT 120

typedef HRESULT (*LabelFn)(void* elem, const wchar_t* text);
typedef HRESULT (*RowFn)(DWORD a, DWORD b, const wchar_t* text);
static LabelFn g_next_label;
static RowFn   g_next_row;
static volatile LONG g_logged;

static void LG(const char* fmt, ...) {
    char buf[400]; int n; DWORD w; HANDLE h; va_list ap;
    n = 0;
    va_start(ap, fmt); n += vsnprintf(buf + n, sizeof(buf) - 4, fmt, ap); va_end(ap);
    buf[n++] = '\r'; buf[n++] = '\n';
    h = CreateFileW(LOGPATH, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, NULL, FILE_END);
    WriteFile(h, buf, n, &w, NULL);
    CloseHandle(h);
}

static void log_flip(const char* tag, const wchar_t* in) {
    char u8[200]; int n;
    if (InterlockedIncrement(&g_logged) > LOG_LIMIT) return;
    n = WideCharToMultiByte(CP_UTF8, 0, in, -1, u8, sizeof(u8) - 1, NULL, NULL);
    if (n <= 0) n = 1;
    u8[n < (int)sizeof(u8) ? n : (int)sizeof(u8) - 1] = 0;
    LG("%s: flipped \"%s\"", tag, u8);
}

static HRESULT H_label(void* elem, const wchar_t* text) {
    wchar_t buf[FLIP_MAX];
    if (text && rtl_flip(text, buf, FLIP_MAX)) { log_flip("label", text); return g_next_label(elem, buf); }
    return g_next_label(elem, text);
}

static HRESULT H_row(DWORD a, DWORD b, const wchar_t* text) {
    wchar_t buf[FLIP_MAX];
    if (text && rtl_flip(text, buf, FLIP_MAX)) { log_flip("row", text); return g_next_row(a, b, buf); }
    return g_next_row(a, b, text);
}

__declspec(dllexport) int RtlFlipInstall(void) {
    int rc1, rc2;
    LG("==== RtlFlipInstall: loaded (pid=%lu) runtime=%d", GetCurrentProcessId(), lyra_runtime_available());
    rc1 = lyra_hook_install(VA_SET_LABEL, (void*)&H_label, (void**)&g_next_label);
    rc2 = lyra_hook_install(VA_ROW_LABEL, (void*)&H_row,   (void**)&g_next_row);
    LG("hook SetLabelText rc=%d, RawRowLabel rc=%d", rc1, rc2);
    return (rc1 == 0 || rc2 == 0) ? 0 : -1;
}

BOOL WINAPI DllMain(HANDLE h, DWORD r, LPVOID l) { (void)h; (void)r; (void)l; return TRUE; }
