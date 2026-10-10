// scummvmzdk.exe - started directly by the "ScummVM ZDK" XNA app tile (no supervisor, no UI freezing).
// Loads scummvm-zdk.dll (ScummVM with the OpenZDK display/input backend) and runs its RunDaemon entry on the
// main thread (which switches to a big private stack itself). When ScummVM quits it returns normally, so the
// XNA launcher and the system compositor/input service are left in a consistent state (no reboot).
// Optional \flash2\scummvm\launch-zdk.txt holds "gameid|\path" (default: the game list).
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>

typedef int (*RunDaemonFn)(const void *arg, int arg_len, HANDLE stop_event);

static void note(const char *m) {
    HANDLE h = CreateFileW(L"\\flash2\\scummvm\\scummvm.log", GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, NULL, FILE_END); DWORD w; WriteFile(h, m, (DWORD)strlen(m), &w, NULL); WriteFile(h, "\n", 1, &w, NULL); CloseHandle(h);
}

#include <tlhelp32.h>
// Is a process with this image name running? (toolhelp.dll, as in the freeze code of the backend)
static bool procRunning(const WCHAR *want) {
    HMODULE th = LoadLibraryW(L"toolhelp.dll");
    if (!th) return false;
    typedef HANDLE (WINAPI *PSnap)(DWORD, DWORD);
    typedef BOOL (WINAPI *PProc)(HANDLE, LPPROCESSENTRY32);
    typedef BOOL (WINAPI *PClose)(HANDLE);
    PSnap snap = (PSnap)GetProcAddress(th, L"CreateToolhelp32Snapshot");
    PProc first = (PProc)GetProcAddress(th, L"Process32First"), next = (PProc)GetProcAddress(th, L"Process32Next");
    PClose cls = (PClose)GetProcAddress(th, L"CloseToolhelp32Snapshot");
    if (!snap || !first || !next) return false;
    HANDLE h = snap(TH32CS_SNAPPROCESS, 0);
    if (h == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32 pe; pe.dwSize = sizeof pe;
    bool found = false;
    if (first(h, &pe)) do { if (!_wcsicmp(pe.szExeFile, want)) found = true; } while (!found && next(h, &pe));
    if (cls) cls(h);
    return found;
}

// An XNA-launched app runs without the Zune shell; if it is gone when we exit, start it again so the system has
// something to return to (otherwise the XNA launcher's exit restarts the whole device).
static void restoreShell() {
    bool g = procRunning(L"gemstone.exe");
    char m[120]; sprintf(m, "zdk host: gemstone running=%d compositor=%d xnalauncher=%d", (int)g, (int)procRunning(L"compositor.exe"), (int)procRunning(L"xnalauncher.exe"));
    note(m);
    if (g) return;
    PROCESS_INFORMATION pi; memset(&pi, 0, sizeof pi);
    BOOL ok = CreateProcessW(L"\\Windows\\gemstone.exe", NULL, NULL, NULL, FALSE, 0, NULL, NULL, NULL, &pi);
    sprintf(m, "zdk host: started gemstone ok=%d err=%lu", (int)ok, ok ? 0UL : GetLastError());
    note(m);
    if (ok) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
    Sleep(4000);
    note("zdk host: shell restore done");
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    char arg[600] = "launcher";
    HANDLE f = CreateFileW(L"\\flash2\\scummvm\\launch-zdk.txt", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        char b[580]; DWORD n = 0;
        if (ReadFile(f, b, sizeof b - 1, &n, NULL) && n > 0) {
            b[n] = 0;
            while (n && (b[n - 1] == '\r' || b[n - 1] == '\n' || b[n - 1] == ' ')) b[--n] = 0;
            if (n) strcpy(arg, b);
        }
        CloseHandle(f);
    }
    note("zdk host: start\n");
    {   // Diagnostic: lash2\scummvm\zdk-test.txt = 1 (exit at once) or 2 (init ZDKGL, draw one frame, clean up, exit).
        int mode = 0;
        HANDLE t = CreateFileW(L"\\flash2\\scummvm\\zdk-test.txt", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (t != INVALID_HANDLE_VALUE) { char c = 0; DWORD n = 0; if (ReadFile(t, &c, 1, &n, NULL) && n) mode = c - '0'; CloseHandle(t); }
        if (mode == 6 || mode == 7) {   // GL only, different shutdown orders
            HMODULE z = LoadLibraryW(L"zdksystem.dll");
            typedef LONG (WINAPI *F0)();  typedef LONG (WINAPI *Fb)(BOOL);  typedef void (WINAPI *Fc)(float, float, float, float);  typedef void (WINAPI *Fk)(unsigned);
            Fb splash = (Fb)GetProcAddress(z, L"ZDKSystem_ShowSplashScreen");
            F0 gi = (F0)GetProcAddress(z, L"ZDKGL_Initialize"), gc = (F0)GetProcAddress(z, L"ZDKGL_Cleanup");
            F0 bd = (F0)GetProcAddress(z, L"ZDKGL_BeginDraw"), ed = (F0)GetProcAddress(z, L"ZDKGL_EndDraw");
            Fc cc = (Fc)GetProcAddress(z, L"ZDKGL_glClearColor"); Fk cl = (Fk)GetProcAddress(z, L"ZDKGL_glClear");
            if (z && splash && gi && gc && bd && ed && cc && cl) {
                splash(FALSE); gi();
                for (int i = 0; i < 90; i++) { bd(); cc(0.0f, 0.0f, 0.5f, 1.0f); cl(0x4000); ed(); }
                if (mode == 6) { splash(TRUE); note("zdk host: mode 6: splash on, cleaning up"); gc(); restoreShell(); }
                else { note("zdk host: mode 7: restoring shell first"); restoreShell(); note("zdk host: mode 7: cleaning up"); gc(); }
            } else note("zdk host: mode 6/7 missing exports");
            note("zdk host: mode 6/7 returning");
            return 0;
        }
        if (mode == 3) { restoreShell(); return 0; }
        if (mode == 4) {   // ZDKInput only
            HMODULE z = LoadLibraryW(L"zdksystem.dll");
            typedef LONG (WINAPI *F0)(); typedef LONG (WINAPI *Fs)(void *);
            F0 ii = z ? (F0)GetProcAddress(z, L"ZDKInput_Initialize") : 0, is = z ? (F0)GetProcAddress(z, L"ZDKInput_Shutdown") : 0;
            Fs ig = z ? (Fs)GetProcAddress(z, L"ZDKInput_GetState") : 0;
            if (ii && is && ig) {
                ii(); static char buf[256];
                for (int i = 0; i < 120; i++) { ig(buf); Sleep(16); }
                is();
                note("zdk host: test mode 4 (input) done");
            } else note("zdk host: test mode 4, missing exports");
            restoreShell();
            return 0;
        }
        if (mode == 5) {   // WaveOut audio only
            WAVEFORMATEX wf; memset(&wf, 0, sizeof wf);
            wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 2; wf.nSamplesPerSec = 22050; wf.wBitsPerSample = 16; wf.nBlockAlign = 4; wf.nAvgBytesPerSec = 22050 * 4;
            HWAVEOUT h = 0;
            if (waveOutOpen(&h, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR) {
                static short silence[4096]; WAVEHDR hdr; memset(&hdr, 0, sizeof hdr);
                hdr.lpData = (LPSTR)silence; hdr.dwBufferLength = sizeof silence;
                waveOutPrepareHeader(h, &hdr, sizeof hdr); waveOutWrite(h, &hdr, sizeof hdr);
                Sleep(500);
                waveOutReset(h); waveOutUnprepareHeader(h, &hdr, sizeof hdr); waveOutClose(h);
                note("zdk host: test mode 5 (audio) done");
            } else note("zdk host: test mode 5, waveOutOpen failed");
            restoreShell();
            return 0;
        }
        if (mode == 1) { note("zdk host: test mode 1, exiting now\n"); return 0; }
        if (mode == 2) {
            HMODULE z = LoadLibraryW(L"zdksystem.dll");
            typedef LONG (WINAPI *F0)();  typedef LONG (WINAPI *Fb)(BOOL);  typedef void (WINAPI *Fc)(float, float, float, float);  typedef void (WINAPI *Fk)(unsigned);
            Fb splash = (Fb)GetProcAddress(z, L"ZDKSystem_ShowSplashScreen");
            F0 gi = (F0)GetProcAddress(z, L"ZDKGL_Initialize"), gc = (F0)GetProcAddress(z, L"ZDKGL_Cleanup");
            F0 bd = (F0)GetProcAddress(z, L"ZDKGL_BeginDraw"), ed = (F0)GetProcAddress(z, L"ZDKGL_EndDraw");
            Fc cc = (Fc)GetProcAddress(z, L"ZDKGL_glClearColor"); Fk cl = (Fk)GetProcAddress(z, L"ZDKGL_glClear");
            if (z && splash && gi && gc && bd && ed && cc && cl) {
                splash(FALSE); gi();
                for (int i = 0; i < 90; i++) { bd(); cc(0.0f, 0.4f, 0.0f, 1.0f); cl(0x4000); ed(); }
                note("zdk host: test mode 2, cleaning up\n");
                gc();
            } else note("zdk host: test mode 2, missing exports\n");
            note("zdk host: test mode 2, returning\n");
            restoreShell();
            return 0;
        }
    }

    HMODULE dll = LoadLibraryW(L"\\flash2\\scummvm\\scummvm-zdk.dll");
    if (!dll) { note("zdk host: cannot load scummvm-zdk.dll\n"); return 2; }
    RunDaemonFn run = (RunDaemonFn)GetProcAddress(dll, L"RunDaemon");
    if (!run) { note("zdk host: no RunDaemon\n"); return 3; }
    HANDLE never = CreateEventW(NULL, TRUE, FALSE, NULL);
    int rc = run(arg, (int)strlen(arg), never);
    note("zdk host: ScummVM returned, exiting\n");   // no restoreShell(): an XNA tile that used the GPU restarts the device on exit regardless
    return rc;
}
