/* regprobe: read-only Lyra mod DLL (load_module into gemstone, init RegProbeInstall).
 * Dumps the font-related registry keys (GDI font linking, FontPath) to \flash2\automation\regprobe.log. Changes nothing. */
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define LOGPATH L"\\flash2\\automation\\regprobe.log"

static void LG(const char* fmt, ...) {
    char buf[700]; int n; DWORD w; HANDLE h; va_list ap;
    va_start(ap, fmt); n = vsnprintf(buf, sizeof(buf) - 4, fmt, ap); va_end(ap);
    if (n < 0 || n > (int)sizeof(buf) - 4) n = sizeof(buf) - 4;
    buf[n++] = '\r'; buf[n++] = '\n';
    h = CreateFileW(LOGPATH, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, NULL, FILE_END);
    WriteFile(h, buf, n, &w, NULL);
    CloseHandle(h);
}

static void w2a(const wchar_t* in, char* out, int cap) { int i = 0; while (in[i] && i < cap - 1) { out[i] = (in[i] < 128) ? (char)in[i] : '?'; i++; } out[i] = 0; }

static void dump_values(HKEY key, const char* label) {
    DWORD i;
    for (i = 0; i < 200; i++) {
        wchar_t name[128]; DWORD nlen = 128, type = 0, dlen = 0; BYTE data[600]; LONG r; char an[128];
        dlen = sizeof(data);
        r = RegEnumValueW(key, i, name, &nlen, NULL, &type, data, &dlen);
        if (r != ERROR_SUCCESS) break;
        w2a(name, an, sizeof an);
        if (type == REG_SZ || type == REG_EXPAND_SZ) {
            char v[400]; w2a((wchar_t*)data, v, sizeof v); LG("  %s [%s] SZ = \"%s\"", label, an, v);
        } else if (type == REG_DWORD && dlen >= 4) {
            LG("  %s [%s] DWORD = 0x%08lx", label, an, *(DWORD*)data);
        } else if (type == REG_MULTI_SZ) {
            wchar_t* p = (wchar_t*)data; char v[400]; int first = 1;
            while (*p && (BYTE*)p < data + dlen) { w2a(p, v, sizeof v); LG("  %s [%s] MULTI_SZ %s \"%s\"", label, an, first ? "=" : "+", v); first = 0; p += wcslen(p) + 1; }
            if (first) LG("  %s [%s] MULTI_SZ (empty)", label, an);
        } else {
            LG("  %s [%s] type=%lu len=%lu", label, an, type, dlen);
        }
    }
}

static void dump_key(const wchar_t* path, int depth) {
    HKEY k; LONG r; char ap[200]; DWORD i;
    w2a(path, ap, sizeof ap);
    r = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &k);
    if (r != ERROR_SUCCESS) { LG("HKLM\\%s : not present (err=%ld)", ap, r); return; }
    LG("HKLM\\%s :", ap);
    dump_values(k, "");
    if (depth > 0) {
        for (i = 0; i < 40; i++) {
            wchar_t sub[128], full[300]; DWORD sl = 128; FILETIME ft;
            if (RegEnumKeyExW(k, i, sub, &sl, NULL, NULL, NULL, &ft) != ERROR_SUCCESS) break;
            wcscpy(full, path); wcscat(full, L"\\"); wcscat(full, sub);
            dump_key(full, depth - 1);
        }
    }
    RegCloseKey(k);
}

__declspec(dllexport) int RegProbeInstall(void) {
    LG("==== RegProbeInstall (pid=%lu)", GetCurrentProcessId());
    dump_key(L"SYSTEM\\GDI", 2);
    dump_key(L"SOFTWARE\\Microsoft\\FontLink", 3);
    dump_key(L"SOFTWARE\\Microsoft\\FontPath", 1);
    dump_key(L"SOFTWARE\\Microsoft\\FontLinkProbe", 0);
    LG("==== done");
    return 0;
}

BOOL WINAPI DllMain(HANDLE h, DWORD r, LPVOID l) { (void)h; (void)r; (void)l; return TRUE; }
