#pragma once
#include "Snapshot.h"
#include <windows.h>
#include <winevt.h>

// Records what happens to LMU and the PC in logs\overlay.log, for crash / freeze hunting:
//  - LMU exiting (normally, crashed, or closed by Windows after a hang) with its exit code;
//  - LMU's window not responding (frozen) and when it recovers;
//  - Windows reports as they happen: application crashes / hangs of LMU and GPU driver
//    timeouts (TDR: the graphics driver stopped responding and was reset), via an event-log
//    subscription (Windows calls us; nothing is polled).
class GameWatch {
public:
  ~GameWatch() { Stop(); }
  void Start();
  void Stop();
  // Main thread, every tick (does real work about once a second).
  void Update(const Snapshot& live, bool onTrack, double now);

private:
  static DWORD WINAPI OnEvent(EVT_SUBSCRIBE_NOTIFY_ACTION action, PVOID ctx, EVT_HANDLE ev);

  EVT_HANDLE sub_ = nullptr;
  HANDLE proc_ = nullptr;
  DWORD pid_ = 0;
  HWND hwnd_ = nullptr;
  bool hung_ = false;
  double hungSince_ = 0, nextCheck_ = 0;
  bool lastOnTrack_ = false;
  long lastSession_ = -1;
};
