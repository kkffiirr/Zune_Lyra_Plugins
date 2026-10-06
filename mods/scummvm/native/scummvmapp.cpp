// scummvmapp.exe - started by the "ScummVM" XNA app tile (see ../app). Runs only when the user opens the app.
// Starts scummvmrun.exe (which hosts ScummVM), waits for it to end for ANY reason, then thaws the Zune UI
// (gemstone) so a crash can never leave the device frozen. Nothing here runs at boot.
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
extern "C" {
#include "lyra.h"
}

#define NK_PROC_LIST 0x80bee010u
static const WCHAR *RUNNER = L"\\flash2\\scummvm\\scummvmrun.exe";

static void logf(const char *fmt, ...) {
    char b[160]; va_list ap; va_start(ap, fmt); int n = _vsnprintf(b, sizeof b - 1, fmt, ap); va_end(ap);
    if (n < 0) n = sizeof b - 1; b[n] = 0;
    HANDLE h = CreateFileW(L"\\flash2\\scummvm\\app.log", GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    if (GetFileSize(h, NULL) > 20000) SetEndOfFile(h);
    SetFilePointer(h, 0, NULL, FILE_END); DWORD w; WriteFile(h, b, n, &w, NULL); CloseHandle(h);
}

static void kname(DWORD w, char *out, int cap) {
    int i = 0;
    for (; i < cap - 1 && w; i++) {
        unsigned short ch = (unsigned short)(lyra_kreadu32(w + i * 2) & 0xffff);
        if (!ch) break;
        out[i] = (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : (char)ch;
    }
    out[i] = 0;
}
static DWORD findPid(const char *want) {
    DWORD head = lyra_kreadu32(NK_PROC_LIST), cur = head;
    for (int i = 0; i < 64 && cur; i++) {
        char nm[24]; kname(lyra_kreadu32(cur + 0x20), nm, sizeof nm);
        if (strstr(nm, want)) return lyra_kreadu32(cur + 0x0c);
        DWORD nxt = lyra_kreadu32(cur); if (nxt == head) break; cur = nxt;
    }
    return 0;
}

// Resume every thread of gemstone (the Zune UI host). Harmless if nothing is suspended.
static int thawUi() {
    DWORD pid = findPid("gemstone"), pid2 = findPid("xnalauncher"), pid3 = findPid("compositor");   // UI shell, XNA loader, compositor
    HMODULE th = LoadLibraryW(L"toolhelp.dll");
    if ((!pid && !pid2 && !pid3) || !th) return -1;
    typedef HANDLE (WINAPI *PSnap)(DWORD, DWORD);
    typedef BOOL (WINAPI *PThread)(HANDLE, LPTHREADENTRY32);
    typedef BOOL (WINAPI *PClose)(HANDLE);
    PSnap snap = (PSnap)GetProcAddress(th, L"CreateToolhelp32Snapshot");
    PThread first = (PThread)GetProcAddress(th, L"Thread32First");
    PThread next = (PThread)GetProcAddress(th, L"Thread32Next");
    PClose cls = (PClose)GetProcAddress(th, L"CloseToolhelp32Snapshot");
    if (!snap || !first || !next) return -2;
    int n = 0;
    HANDLE s = snap(TH32CS_SNAPTHREAD, 0);
    if (s == INVALID_HANDLE_VALUE) return -3;
    THREADENTRY32 te; te.dwSize = sizeof te;
    if (first(s, &te)) {
        do {
            if (!((pid && te.th32OwnerProcessID == pid) || (pid2 && te.th32OwnerProcessID == pid2) || (pid3 && te.th32OwnerProcessID == pid3))) continue;
            DWORD prev;
            while ((prev = ResumeThread((HANDLE)te.th32ThreadID)) != (DWORD)-1 && prev > 1) n++;
            n++;
        } while (next(s, &te));
    }
    if (cls) cls(s);
    return n;
}

// Log every process (pid + name) so we can tell what is drawing on the screen while ScummVM runs.
static void logProcs(const char *tag) {
    DWORD head = lyra_kreadu32(NK_PROC_LIST), cur = head;
    logf("-- processes (%s)\r\n", tag);
    for (int i = 0; i < 64 && cur; i++) {
        char nm[32]; kname(lyra_kreadu32(cur + 0x20), nm, sizeof nm);
        logf("   pid=%lu %s\r\n", lyra_kreadu32(cur + 0x0c), nm);
        DWORD nxt = lyra_kreadu32(cur); if (nxt == head) break; cur = nxt;
    }
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    CreateDirectoryW(L"\\flash2\\scummvm", NULL);
    HANDLE once = CreateMutexW(NULL, FALSE, L"scummvm-app-single-instance");
    if (once && GetLastError() == ERROR_ALREADY_EXISTS) { logf("already running, exiting\r\n"); return 0; }
    logf("app started\r\n");
    Sleep(1500);                                 // let the XNA tile app finish exiting and the UI settle
    for (int i = 0; i < 40; i++) {               // Lyra's kernel access must be up (it normally is)
        if (lyra_runtime_available()) { lyra_kernel_ensure_helpers(); if (lyra_kernel_ready()) break; }
        Sleep(500);
    }
    if (!lyra_kernel_ready()) { logf("Lyra kernel access not available - is Lyra installed?\r\n"); return 1; }
    logProcs("before launch");
    // Leave XNA-app mode: ending the XNA launcher makes the system bring the Zune shell (gemstone) back. ScummVM then
    // runs in normal mode (shell frozen, hardware buttons work, no restart on quit). If the shell does not return
    // within 40s we carry on in XNA-app mode (compositor/launcher frozen), the old behaviour.
    DWORD xp = findPid("xnalauncher");
    // DISABLED: ending xnalauncher makes the Zune restart at once (tested 2026-10-06), so we stay in XNA-app mode.
    if (0 && xp && !findPid("gemstone")) {
        HANDLE hp = OpenProcess(PROCESS_TERMINATE, FALSE, xp);
        if (hp) { BOOL ok = TerminateProcess(hp, 0); logf("terminate xnalauncher ok=%d err=%lu\r\n", (int)ok, GetLastError()); CloseHandle(hp); }
        else logf("OpenProcess(xnalauncher) failed err=%lu\r\n", GetLastError());
        for (int i = 0; i < 80 && !findPid("gemstone"); i++) Sleep(500);
        if (findPid("gemstone")) { logf("shell is back\r\n"); Sleep(4000); }   // let it finish starting
        else logf("shell did not return in 40s - continuing in app mode\r\n");
        logProcs("after leaving XNA mode");
    }
    PROCESS_INFORMATION pi; memset(&pi, 0, sizeof pi);
    if (!CreateProcessW(RUNNER, NULL, NULL, NULL, FALSE, 0, NULL, NULL, NULL, &pi)) {
        logf("CreateProcess(runner) failed err=%lu\r\n", GetLastError());
        return 2;
    }
    if (WaitForSingleObject(pi.hProcess, 6000) == WAIT_TIMEOUT) {   // ScummVM is up: record who else is running
        logProcs("ScummVM running");
        WaitForSingleObject(pi.hProcess, INFINITE);
    }
    DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    int t = thawUi();                            // always give the Zune UI back, even after a crash
    logf("ScummVM ended code=%lu thaw=%d\r\n", code, t);
    Sleep(3000);
    logProcs("3s after exit");                   // does the UI shell (gemstone) come back by itself?
    return 0;
}
