// scummvmrun.exe - hosts ScummVM in its own process (started by scummvmd.exe).
// Loads scummvm-zune.dll and runs its RunDaemon entry on the main thread (which switches to a big private
// stack itself). A crash here kills only this process; scummvmd.exe then thaws the Zune UI.
// Optional \flash2\automation\scummvm\launch.txt holds "gameid|\path" (default: tentacle).
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef int (*RunDaemonFn)(const void *arg, int arg_len, HANDLE stop_event);

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    char arg[600] = "launcher";    // ScummVM's game list; launch.txt can override with "gameid|path" to autostart
    HANDLE f = CreateFileW(L"\\flash2\\scummvm\\launch.txt", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        char b[580]; DWORD n = 0;
        if (ReadFile(f, b, sizeof b - 1, &n, NULL) && n > 0) {
            b[n] = 0;
            while (n && (b[n - 1] == '\r' || b[n - 1] == '\n' || b[n - 1] == ' ')) b[--n] = 0;
            if (n) strcpy(arg, b);
        }
        CloseHandle(f);
    }
    HMODULE dll = LoadLibraryW(L"\\flash2\\scummvm\\scummvm-zune.dll");
    if (!dll) return 2;
    RunDaemonFn run = (RunDaemonFn)GetProcAddress(dll, L"RunDaemon");
    if (!run) return 3;
    HANDLE never = CreateEventW(NULL, TRUE, FALSE, NULL);   // never signalled: ScummVM ends via its own Quit
    return run(arg, (int)strlen(arg), never);
}
