#pragma once
#include <windows.h>

// QueryPerformanceCounter-based monotonic clock, in seconds.
inline double NowSeconds() {
  static const double inv = [] {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return 1.0 / static_cast<double>(f.QuadPart);
  }();
  LARGE_INTEGER c;
  QueryPerformanceCounter(&c);
  return static_cast<double>(c.QuadPart) * inv;
}

// High-resolution waitable timer: sub-millisecond sleeps without raising the
// global timer resolution (timeBeginPeriod), which costs the whole system.
class PreciseTimer {
public:
  PreciseTimer() {
    h_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!h_) h_ = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS); // pre-1803 fallback
  }
  ~PreciseTimer() { if (h_) CloseHandle(h_); }
  PreciseTimer(const PreciseTimer&) = delete;
  PreciseTimer& operator=(const PreciseTimer&) = delete;

  // Arms the timer to fire after `seconds`; wait on Handle() (alone or with other handles).
  void Arm(double seconds) {
    LARGE_INTEGER due;
    due.QuadPart = -static_cast<LONGLONG>(seconds > 0 ? seconds * 1e7 : 0); // relative, 100ns units
    SetWaitableTimer(h_, &due, 0, nullptr, nullptr, FALSE);
  }
  void Sleep(double seconds) {
    Arm(seconds);
    WaitForSingleObject(h_, INFINITE);
  }
  HANDLE Handle() const { return h_; }

private:
  HANDLE h_ = nullptr;
};
