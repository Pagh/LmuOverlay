// LMU Overlay - low-overhead overlay for Le Mans Ultimate.
//
// Threads:
//   reader (SharedMemoryReader / DemoSource): copies game data at poll_hz into a triple buffer.
//   main: window messages, rendering and the settings window, woken by a high-resolution timer.
#include "App.h"
#include <shellapi.h>

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2); // positions are physical pixels

  // After an update the new exe is started by the old one: wait for it to close first.
  {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i + 1 < argc; ++i)
      if (wcscmp(argv[i], L"--wait-pid") == 0)
        if (HANDLE old = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(_wtoi(argv[i + 1])))) {
          WaitForSingleObject(old, 15000);
          CloseHandle(old);
        }
    LocalFree(argv);
  }

  HANDLE mutex = CreateMutexW(nullptr, TRUE, L"LmuOverlay_SingleInstance");
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    // Already running: ask that instance to show its settings window instead.
    if (HWND other = FindWindowW(L"LmuOverlayWindow", nullptr)) PostMessageW(other, WM_APP + 2, 0, 0);
    return 0;
  }

  bool demo = false, autostart = false;
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 1; i < argc; ++i) {
    if (wcscmp(argv[i], L"--demo") == 0) demo = true;
    if (wcscmp(argv[i], L"--autostart") == 0) autostart = true; // started with Windows: stay in the tray
  }
  LocalFree(argv);

  int rc = 0;
  {
    auto app = std::make_unique<App>(); // large object: keep it off the stack
    rc = app->Run(inst, demo, autostart);
  }
  if (mutex) { ReleaseMutex(mutex); CloseHandle(mutex); }
  return rc;
}
