#include "GameWatch.h"
#include "Ini.h"
#include "Log.h"
#include <string>
#include <vector>

namespace {

// Event XML -> "provider id: data | data | ..." (the <Data> values are the same in every language).
std::string Summarize(const std::wstring& xml, bool& relevant, std::string& kind) {
  auto between = [&xml](size_t from, const wchar_t* open, const wchar_t* close, size_t& end) -> std::wstring {
    const size_t a = xml.find(open, from);
    if (a == std::wstring::npos) { end = std::wstring::npos; return L""; }
    const size_t b = xml.find(L'>', a);
    const size_t c = xml.find(close, b);
    if (b == std::wstring::npos || c == std::wstring::npos) { end = std::wstring::npos; return L""; }
    end = c + wcslen(close);
    return xml.substr(b + 1, c - b - 1);
  };
  size_t e;
  std::wstring id = between(0, L"<EventID", L"</EventID>", e);
  const size_t p = xml.find(L"Provider Name='");
  std::wstring provider;
  if (p != std::wstring::npos) provider = xml.substr(p + 15, xml.find(L'\'', p + 15) - p - 15);
  std::vector<std::wstring> data;
  size_t at = 0;
  while (data.size() < 14) {
    std::wstring d = between(at, L"<Data", L"</Data>", e);
    if (e == std::wstring::npos) break;
    data.push_back(d);
    at = e;
  }
  std::wstring all;
  for (const std::wstring& d : data) {
    if (d.empty()) continue;
    if (!all.empty()) all += L" | ";
    all += d;
  }
  relevant = all.find(L"Le Mans Ultimate") != std::wstring::npos || all.find(L"LmuOverlay") != std::wstring::npos ||
             all.find(L"LiveKernelEvent") != std::wstring::npos;
  if (all.find(L"LiveKernelEvent") != std::wstring::npos) {
    // P1 of a live kernel report: 141 / 117 = GPU engine timeout / driver recovered (TDR).
    const bool tdr = all.find(L"| 141 |") != std::wstring::npos || all.find(L"| 117 |") != std::wstring::npos;
    kind = tdr ? "GPU DRIVER TIMEOUT (TDR: the graphics driver stopped responding and was reset)" : "kernel live report";
  } else if (id == L"1002") {
    kind = "APP HANG (closed by Windows after it stopped responding)";
  } else if (id == L"1000") {
    kind = "APP CRASH";
  } else {
    kind = "error report";
  }
  return Narrow(provider + L" " + id + L": " + all.substr(0, 400));
}

const char* ExitMeaning(DWORD code) {
  switch (code) {
    case 0: return "closed normally";
    case 0xC0000005: return "CRASH: access violation";
    case 0xC0000409: return "CRASH: fail-fast / stack buffer overrun";
    case 0xC00000FD: return "CRASH: stack overflow";
    case 0xC0000374: return "CRASH: heap corruption";
    case 0xCFFFFFFF: return "ENDED BY WINDOWS after it stopped responding";
    case 1: return "ended with code 1 (killed or error)";
    default: return code >= 0xC0000000 ? "CRASH (exception)" : "ended with an error code";
  }
}

} // namespace

void GameWatch::Start() {
  if (sub_) return;
  // Windows Error Reporting (1001), Application Error (1000), Application Hang (1002).
  sub_ = EvtSubscribe(nullptr, nullptr, L"Application",
                      L"*[System[(EventID=1000 or EventID=1001 or EventID=1002)]]", nullptr, this, OnEvent,
                      EvtSubscribeToFutureEvents);
  if (!sub_) Logf("game watch: event log subscription failed (%lu)", GetLastError());
}

void GameWatch::Stop() {
  if (sub_) { EvtClose(sub_); sub_ = nullptr; }
  if (proc_) { CloseHandle(proc_); proc_ = nullptr; }
}

DWORD WINAPI GameWatch::OnEvent(EVT_SUBSCRIBE_NOTIFY_ACTION action, PVOID, EVT_HANDLE ev) {
  if (action != EvtSubscribeActionDeliver) return 0;
  DWORD used = 0, props = 0;
  EvtRender(nullptr, ev, EvtRenderEventXml, 0, nullptr, &used, &props);
  if (used == 0) return 0;
  std::wstring xml(used / sizeof(wchar_t) + 1, L'\0');
  if (!EvtRender(nullptr, ev, EvtRenderEventXml, static_cast<DWORD>(xml.size() * sizeof(wchar_t)), xml.data(), &used, &props))
    return 0;
  bool relevant = false;
  std::string kind;
  const std::string line = Summarize(xml, relevant, kind);
  if (relevant) Logf("WINDOWS %s -- %s", kind.c_str(), line.c_str());
  return 0;
}

void GameWatch::Update(const Snapshot& live, bool onTrack, double now) {
  // Remember LMU's process while it's connected.
  if (live.connected && live.app.mAppWindow && live.app.mAppWindow != hwnd_) {
    DWORD pid = 0;
    GetWindowThreadProcessId(live.app.mAppWindow, &pid);
    if (pid && pid != pid_) {
      if (proc_) CloseHandle(proc_);
      proc_ = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
      pid_ = pid;
      hung_ = false;
    }
    hwnd_ = live.app.mAppWindow;
  }
  if (live.connected) {
    lastOnTrack_ = onTrack;
    lastSession_ = live.scoring.mSession;
  }
  if (now < nextCheck_ || !proc_) return;
  nextCheck_ = now + 1.0;

  if (WaitForSingleObject(proc_, 0) == WAIT_OBJECT_0) {
    DWORD code = 0;
    GetExitCodeProcess(proc_, &code);
    Logf("LMU exited: %s (code 0x%08lX)%s", ExitMeaning(code), code,
         lastOnTrack_ ? " -- you were on track" : "");
    CloseHandle(proc_);
    proc_ = nullptr;
    pid_ = 0;
    hwnd_ = nullptr;
    hung_ = false;
    return;
  }
  // Frozen: Windows' own test (the window hasn't processed messages for 5 s).
  const bool hung = hwnd_ && IsWindow(hwnd_) && IsHungAppWindow(hwnd_);
  if (hung && !hung_) {
    hungSince_ = now;
    Logf("LMU NOT RESPONDING (window frozen)%s", lastOnTrack_ ? " -- you were on track" : "");
  } else if (!hung && hung_) {
    Logf("LMU responding again after %.0f s", now - hungSince_);
  }
  hung_ = hung;
}
