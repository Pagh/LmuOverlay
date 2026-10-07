#include "App.h"
#include "Clock.h"
#include "Log.h"
#include "Version.h"
#include <cstdio>
#include <algorithm>

namespace {

constexpr UINT kTrayMsg = WM_APP + 1;
constexpr UINT kOpenSettingsMsg = WM_APP + 2; // sent by a second instance
enum HotkeyId { kHotkeyEdit = 1, kHotkeyToggle, kHotkeyQuit, kHotkeyRef, kHotkeyProfile };
static_assert(GeneralSettings::kWheelActions == static_cast<int>(WheelAction::Count));
enum MenuId { kMenuSettings = 100, kMenuEdit, kMenuToggle, kMenuReload, kMenuQuit };

std::wstring ExeDir() {
  wchar_t path[MAX_PATH];
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring s(path);
  return s.substr(0, s.find_last_of(L"\\/") + 1);
}

// Overlay self-measurement, shown by the perf widget.
struct PerfAccum {
  double start = 0, renderSum = 0, renderMax = 0;
  int renderCount = 0, redraws = 0;
  uint64_t seq = 0;
  float holdMax = 0, waitMax = 0;
} g_acc;

const OptionDef& DeltaRefOption() {
  static const OptionDef def = [] {
    for (const OptionDef& d : FullSchema(kDeltaWidget))
      if (strcmp(d.key, "reference") == 0) return d;
    return OptionDef{"reference", "", OptType::Choice};
  }();
  return def;
}

} // namespace

// ----------------------------------------------------------------------------
// Startup / shutdown

int App::Run(HINSTANCE inst, bool demoOnly) {
  inst_ = inst;
  demoOnly_ = demoOnly;
  exeDir_ = ExeDir();
  settingsPath_ = exeDir_ + L"settings.ini";
  profiles_.Init(exeDir_ + L"profiles");
  LogOpen(exeDir_ + L"logs");
  Logf("---- overlay started v%s (build %s %s)%s", LMU_OVERLAY_VERSION, __DATE__, __TIME__, demoOnly ? " demo" : "");
  Updater::CleanUp();
  LoadOrMigrate();
  ApplyProcessSettings();

  liveTiming_.SetRecordsDir(exeDir_ + L"records");
  demoRest_.valid = true; // fake damage details for the demo / preview
  demoRest_.aero = 0.06f;
  demoRest_.suspension[1] = 0.18f;
  demoRest_.repairSeconds = 32.f;
  demoRest_.fuelFillRate = 2.857f;
  demoRest_.energyFillRate = 0.025f;
  demoRest_.fuelInsert = 1.5f;
  demoRest_.tyreChange = 12.f;

  liveBuf_ = std::make_unique<TripleBuffer<Snapshot>>(); // ~200 KB each: keep off the stack
  demoBuf_ = std::make_unique<TripleBuffer<Snapshot>>();
  if (!demoOnly_) { StartReader(); gameWatch_.Start(); }

  auto hook = [this](HWND h, UINT m, WPARAM w, LPARAM l, LRESULT& r) { return HandleMessage(h, m, w, l, r); };
  auto moved = [this](const char* section, int x, int y) { OnWidgetMoved(section, x, y); };
  if (!overlay_.Create(inst_, gs_, hook, moved)) {
    MessageBoxW(nullptr, L"Could not initialise Direct3D / DirectComposition.", L"LMU Overlay", MB_ICONERROR);
    return 1;
  }
  ActivateProfile(WantedProfile());

  const HWND hwnd = overlay_.Hwnd();
  RegisterHotKey(hwnd, kHotkeyEdit, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'E');
  RegisterHotKey(hwnd, kHotkeyToggle, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'O');
  RegisterHotKey(hwnd, kHotkeyQuit, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'Q');
  RegisterHotKey(hwnd, kHotkeyRef, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'D');
  RegisterHotKey(hwnd, kHotkeyProfile, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'P');
  wheel_.Init(hwnd);
  ApplyWheelBindings();
  CreateTray();
  if (gs_.openSettingsOnStart) OpenSettings();
  if (gs_.updateCheckAtStart && !UpdateRepo().empty()) updater_.Check(UpdateRepo());

  PreciseTimer tick;
  const HANDLE tickHandle = tick.Handle();
  tick.Arm(0);
  g_acc.start = NowSeconds();

  while (running_) {
    const DWORD r = MsgWaitForMultipleObjectsEx(1, &tickHandle, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) running_ = false;
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (r != WAIT_OBJECT_0 || !running_) continue;

    Tick(NowSeconds());
    // When hidden (menus, game closed) and no settings window, wake only a few times per second.
    const bool active = overlay_.Visible() || settingsWindow_.IsOpen();
    tick.Arm(active ? 1.0 / gs_.maxFps : 0.25);
  }

  if (savePending_) SaveProfileNow();
  if (settingsDirty_) SaveSettingsNow();
  Logf("overlay stopped");
  gameWatch_.Stop();
  settingsWindow_.Close();
  Shell_NotifyIconW(NIM_DELETE, &tray_);
  for (int id : {kHotkeyEdit, kHotkeyToggle, kHotkeyQuit, kHotkeyRef, kHotkeyProfile}) UnregisterHotKey(hwnd, id);
  wheel_.Shutdown();
  overlay_.SetEditMode(false);
  overlay_.Destroy();
  if (reader_) reader_->Stop();
  if (demo_) demo_->Stop();
  rest_.Stop();
  return 0;
}

void App::LoadOrMigrate() {
  // v1 kept everything in LmuOverlay.ini: [general] + one section per widget, same key names.
  const std::wstring legacyPath = exeDir_ + L"LmuOverlay.ini";
  const bool haveSettings = settingsDoc_.Load(settingsPath_);
  IniDoc legacy;
  const bool haveLegacy = !haveSettings && legacy.Load(legacyPath);
  gs_.Load(haveSettings ? settingsDoc_ : legacy);
  if (!haveSettings) SaveSettingsNow();

  if (profiles_.List().empty()) {
    IniDoc seed;
    if (haveLegacy)
      for (const IniDoc::Section& sec : legacy.Sections())
        if (FindWidgetType(sec.name))
          for (const auto& [k, v] : sec.values) seed.Set(sec.name, k, v);
    for (const char* name : {"Practice", "Qualifying", "Race"}) profiles_.Create(name, seed);
  }
  if (haveLegacy) MoveFileExW(legacyPath.c_str(), (legacyPath + L".old").c_str(), MOVEFILE_REPLACE_EXISTING);
}

void App::ApplyProcessSettings() {
  // Below-normal priority: whenever LMU wants a core, it gets it before us.
  // (The reader thread raises itself while it holds the game's lock; see SharedMemoryReader.)
  SetPriorityClass(GetCurrentProcess(), gs_.lowerPriority ? BELOW_NORMAL_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS);
  DWORD_PTR processMask = 0, systemMask = 0;
  GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask);
  const DWORD_PTR wanted = gs_.affinityMask ? static_cast<DWORD_PTR>(gs_.affinityMask) & systemMask : systemMask;
  if (wanted && wanted != processMask) SetProcessAffinityMask(GetCurrentProcess(), wanted);
}

void App::StartReader() {
  reader_.reset(); // joins the old thread
  reader_ = std::make_unique<SharedMemoryReader>(*liveBuf_, gs_.pollHz);
  reader_->Start();
  readerPollHz_ = gs_.pollHz;
}

void App::UpdateDemoSource(bool needed) {
  if (needed && !demo_) {
    demo_ = std::make_unique<DemoSource>(*demoBuf_, gs_.pollHz);
    demo_->Start();
  } else if (!needed && demo_) {
    demo_.reset();
  }
}

// ----------------------------------------------------------------------------
// Per-tick work

void App::Tick(double now) {
  if (liveBuf_->Acquire()) {
    g_acc.holdMax = std::max(g_acc.holdMax, liveBuf_->Read().lockHoldUs);
    g_acc.waitMax = std::max(g_acc.waitMax, liveBuf_->Read().lockWaitUs);
  }
  const Snapshot& live = liveBuf_->Read();
  liveModel_.Update(live, now);
  liveTiming_.Update(live, liveModel_.playerIdx);
  if (liveModel_.connected) liveSession_ = SessionKindFromLmu(live.scoring.mSession);
  else if (demoOnly_) liveSession_ = SessionKind::Race; // the demo is a race
  UpdateRest(now);
  liveModel_.timing = &liveTiming_;
  liveModel_.rest = &restData_;
  // Wheel buttons (buffered by DirectInput, so polling ~30 times a second loses nothing).
  if (now >= nextWheelPoll_) {
    nextWheelPoll_ = now + 1.0 / 30.0;
    wheelFired_.clear();
    wheel_.Poll(wheelFired_);
    for (WheelAction a : wheelFired_) OnWheelAction(a);
  }
  wcscpy_s(liveModel_.notice, notice_);
  wcscpy_s(demoModel_.notice, notice_);
  liveModel_.noticeAt = demoModel_.noticeAt = noticeAt_;
  liveModel_.deltaRef = demoModel_.deltaRef = deltaRef_;
  liveModel_.deltaRefChangedAt = demoModel_.deltaRefChangedAt = deltaRefChangedAt_;
  // Remember fuel / energy use per lap for this track and car (pre-race fuel planning).
  if (liveModel_.onTrack) liveTiming_.RecordConsumption(liveModel_.fuel.PerLap(), liveModel_.energy.PerLap());
  liveModel_.UpdateRace();

  // Preview (settings open) and layout editing show demo data when you're not on track,
  // so every widget is visible with realistic content.
  const bool previewing = settingsWindow_.InUse() && preview_;
  const bool useDemo = demoOnly_ || ((previewing || overlay_.EditMode()) && !liveModel_.onTrack);
  UpdateDemoSource(useDemo);
  const Model* model = &liveModel_;
  if (useDemo && demo_) {
    demoBuf_->Acquire();
    demoModel_.Update(demoBuf_->Read(), now);
    demoTiming_.Update(demoBuf_->Read(), demoModel_.playerIdx);
    demoModel_.timing = &demoTiming_;
    demoModel_.rest = &demoRest_;
    demoTiming_.RecordConsumption(2.83, 0.0656); // what the demo car uses per lap
    demoModel_.UpdateRace();
    model = &demoModel_;
  }

  const std::string want = WantedProfile();
  if (want != profileName_) ActivateProfile(want);
  if (rebuildPending_ && now - lastRebuild_ > 0.05) {
    overlay_.SetWidgets(CreateWidgets(profileDoc_));
    rebuildPending_ = false;
    lastRebuild_ = now;
  }
  if (savePending_ && now - lastEdit_ > 0.5) SaveProfileNow();
  if (settingsDirty_ && now - lastSettingsEdit_ > 0.5) SaveSettingsNow();
  UpdateMonitor(now);

  const double t0 = NowSeconds();
  const int drawn = overlay_.Tick(*model, now, previewing);
  const double t1 = NowSeconds();
  if (drawn) {
    g_acc.renderSum += t1 - t0;
    g_acc.renderMax = std::max(g_acc.renderMax, t1 - t0);
    ++g_acc.renderCount;
    g_acc.redraws += drawn;
  }
  if (t1 - g_acc.start >= 1.0) {
    const double span = t1 - g_acc.start;
    g_perf.renderMsAvg = g_acc.renderCount ? static_cast<float>(g_acc.renderSum / g_acc.renderCount * 1000.0) : 0.f;
    g_perf.renderMsMax = static_cast<float>(g_acc.renderMax * 1000.0);
    g_perf.lockHoldUsMax = g_acc.holdMax;
    g_perf.lockWaitUsMax = g_acc.waitMax;
    g_perf.snapshotsPerSec = static_cast<float>((live.seq - g_acc.seq) / span);
    g_perf.redrawsPerSec = static_cast<float>(g_acc.redraws / span);
    if (overlay_.Visible()) {
      perfRenderSum_ += g_perf.renderMsAvg;
      perfRenderMax_ = std::max<double>(perfRenderMax_, g_perf.renderMsMax);
      perfHoldMax_ = std::max(perfHoldMax_, g_perf.lockHoldUsMax);
      perfRedraws_ += g_perf.redrawsPerSec;
      perfSnaps_ += g_perf.snapshotsPerSec;
      ++perfSeconds_;
    }
    g_acc = PerfAccum{t1};
    g_acc.seq = live.seq;
  }

  // Updates: tell once when one is available; restart into the new exe once installed.
  const Updater::State up = updater_.Get();
  if (up.phase == Updater::Phase::Available && !updateNotified_) {
    updateNotified_ = true;
    Logf("update available: v%s", up.latest.c_str());
    NOTIFYICONDATAW tip = tray_;
    tip.uFlags = NIF_INFO;
    tip.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
    wcscpy_s(tip.szInfoTitle, L"LMU Overlay: update available");
    swprintf_s(tip.szInfo, L"Version %hs is out. Settings > General > Updates to install it.", up.latest.c_str());
    Shell_NotifyIconW(NIM_MODIFY, &tip);
  }
  if (up.phase == Updater::Phase::Ready && running_) {
    if (savePending_) SaveProfileNow();
    if (settingsDirty_) SaveSettingsNow();
    if (updater_.LaunchNew()) running_ = false;
  }
  if (!demoOnly_) {
    LogSession(now);
    gameWatch_.Update(live, liveModel_.onTrack, now);
  }
  if (settingsWindow_.IsOpen()) settingsWindow_.Frame();
}

void App::LogSession(double now) {
  const Model& m = liveModel_;
  if (m.connected != loggedConnected_) {
    Logf(m.connected ? "LMU connected" : "LMU disconnected");
    loggedConnected_ = m.connected;
  }
  if (m.connected && m.player) {
    const Snapshot& s = *m.snap;
    const std::string track(s.scoring.mTrackName, strnlen(s.scoring.mTrackName, sizeof(s.scoring.mTrackName)));
    if (s.scoring.mSession != loggedSession_ || track != loggedTrack_) {
      const char* model = s.hasPlayerTelem ? s.telem.mVehicleModel : "";
      Logf("session %ld (%s) at %s, %.30s, class %.32s, %d cars", s.scoring.mSession, SessionKindName(liveSession_),
           track.c_str(), model, m.player->mVehicleClass, s.numVehicles);
      loggedSession_ = s.scoring.mSession;
      loggedTrack_ = track;
      loggedLaps_ = m.player->mTotalLaps;
    }
    // One line per lap: what the strategy logic thinks, to check it against what really happened.
    if (m.player->mTotalLaps != loggedLaps_) {
      loggedLaps_ = m.player->mTotalLaps;
      const RaceInfo& r = m.race;
      Logf("lap %d done: last %.3f best %.3f%s | fuel %.2f L (%.3f L/lap) energy %.1f%% (%.2f%%/lap) | "
           "laps est %.1f, to go %.1f, in tank %.1f, full %.1f | stop %s, stops left %d, window %d-%d | pace %.3f",
           m.player->mTotalLaps, m.player->mLastLapTime, m.player->mBestLapTime,
           liveTiming_.PlayerLastValid() ? "" : " (invalid)", r.fuel, r.fuelPerLap, r.energy * 100.0,
           r.energyPerLap * 100.0, r.totalLaps, r.lapsToGo, r.lapsInTank, r.lapsOnFull,
           !r.stopKnown ? "unknown" : r.stopRequired ? "REQUIRED" : "not needed", r.stopsLeft, r.windowOpen,
           r.windowClose, r.pace);
    }
  }
  // Once a minute: the overlay's own cost.
  if (now - perfLogAt_ >= 60.0) {
    FILETIME c, e, k, u;
    GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
    const unsigned long long cpu = (static_cast<unsigned long long>(k.dwHighDateTime) << 32 | k.dwLowDateTime) +
                                   (static_cast<unsigned long long>(u.dwHighDateTime) << 32 | u.dwLowDateTime);
    if (perfSeconds_ > 0 && cpuLastAt_ > 0) {
      const double cpuPct = (cpu - cpuLast_) / 1e7 / (now - cpuLastAt_) * 100.0;
      Logf("perf (overlay visible %ds): draw avg %.2f ms max %.2f ms | lock held max %.1f us | redraws %.0f/s | "
           "snapshots %.0f/s | cpu %.1f%% of one core", perfSeconds_, perfRenderSum_ / perfSeconds_, perfRenderMax_,
           perfHoldMax_, perfRedraws_ / perfSeconds_, perfSnaps_ / perfSeconds_, cpuPct);
    }
    cpuLast_ = cpu;
    cpuLastAt_ = now;
    perfLogAt_ = now;
    perfRenderSum_ = perfRenderMax_ = perfRedraws_ = perfSnaps_ = 0;
    perfHoldMax_ = 0;
    perfSeconds_ = 0;
  }
}

void App::UpdateRest(double now) {
  // Damage details come from LMU's REST API. Ask rarely: every few seconds while driving,
  // and shortly after each impact. Requests run on their own thread.
  const bool wanted = gs_.restApi && !demoOnly_ && liveModel_.connected && liveModel_.onTrack;
  if (!wanted) {
    if (rest_.Running() && !gs_.restApi) { rest_.Stop(); restData_ = RestData{}; }
    return;
  }
  if (!rest_.Running()) rest_.Start();
  if (liveModel_.telem && liveModel_.telem->mLastImpactET != lastImpactET_) {
    if (lastImpactET_ >= 0) nextRestPoll_ = std::min(nextRestPoll_, now + 1.0); // let the damage settle
    lastImpactET_ = liveModel_.telem->mLastImpactET;
  }
  if (now >= nextRestPoll_) {
    const bool needGarage = !liveModel_.race.perLapMeasured && now - lastGarageRead_ > 30.0;
    if (needGarage) lastGarageRead_ = now;
    rest_.Refresh(needGarage);
    nextRestPoll_ = now + gs_.restIntervalS;
  }
  restData_ = rest_.Latest();
}

void App::UpdateMonitor(double now) {
  if (now < nextMonitorCheck_) return;
  nextMonitorCheck_ = now + 1.0;

  const std::vector<MonitorInfo> mons = EnumMonitors();
  auto find = [&mons](const std::string& dev) -> const MonitorInfo* {
    for (const MonitorInfo& m : mons) if (m.device == dev) return &m;
    return nullptr;
  };
  const MonitorInfo* target = nullptr;
  if (gs_.monitor != "auto") {
    target = find(gs_.monitor);
  } else {
    // Follow the game's window; remember its monitor for when LMU isn't running.
    const HWND game = liveBuf_->Read().app.mAppWindow;
    if (liveModel_.connected && game && IsWindow(game)) {
      MONITORINFOEXW mi{};
      mi.cbSize = sizeof(mi);
      if (GetMonitorInfoW(MonitorFromWindow(game, MONITOR_DEFAULTTONEAREST), &mi)) target = find(Narrow(mi.szDevice));
      if (target && gs_.lastGameMonitor != target->device) {
        gs_.lastGameMonitor = target->device;
        SaveSettingsNow();
      }
    }
    if (!target && !gs_.lastGameMonitor.empty()) target = find(gs_.lastGameMonitor);
  }
  if (!target)
    for (const MonitorInfo& m : mons) if (m.primary) target = &m;
  if (!target) return;
  monitorDevice_ = target->device;
  overlay_.SetMonitorRect(target->rect);
}

// ----------------------------------------------------------------------------
// Profiles and settings

std::string App::WantedProfile() const {
  std::string name;
  // While the settings window is open, the profile you picked there stays on screen; until you
  // pick one it keeps following the session (the window may open before LMU is detected).
  if (settingsWindow_.IsOpen() && profilePinned_) name = selectedProfile_;
  else if (!overrideProfile_.empty() && overrideSession_ == liveSession_) name = overrideProfile_;
  else if (gs_.autoSwitch) name = liveModel_.connected || demoOnly_ ? gs_.profileFor[static_cast<int>(liveSession_)] : profileName_;
  else name = gs_.manualProfile;

  if (profiles_.Exists(name)) return name;
  if (profiles_.Exists(profileName_)) return profileName_;
  const auto all = profiles_.List();
  return all.empty() ? std::string("Default") : all.front();
}

void App::ActivateProfile(const std::string& name) {
  if (savePending_) SaveProfileNow();
  IniDoc doc;
  if (!profiles_.Load(name, doc)) {
    ProfileManager::Normalize(doc);
    profiles_.Save(name, doc); // e.g. "Default" when every profile was deleted by hand
  }
  profileDoc_ = std::move(doc);
  if (name != profileName_) Logf("profile: %s", name.c_str());
  profileName_ = name;
  overlay_.SetWidgets(CreateWidgets(profileDoc_));
  rebuildPending_ = false;
  ReadDeltaRef();
}

void App::ReadDeltaRef() {
  const int v = static_cast<int>(GetOptionValue(profileDoc_, "delta", DeltaRefOption()));
  deltaRef_ = static_cast<DeltaRef>(std::clamp(v, 0, static_cast<int>(DeltaRef::Count) - 1));
}

void App::CycleDeltaRef(int step) {
  const int n = static_cast<int>(DeltaRef::Count);
  deltaRef_ = static_cast<DeltaRef>(((static_cast<int>(deltaRef_) + step) % n + n) % n);
  Notify(L"DELTA  vs %ls", DeltaRefLabel(deltaRef_));
  deltaRefChangedAt_ = NowSeconds();
  SetOptionValue(profileDoc_, "delta", DeltaRefOption(), static_cast<double>(deltaRef_));
  savePending_ = true; // remembered in the profile; no rebuild needed, widgets read it from the model
  lastEdit_ = NowSeconds();
}

void App::CycleProfile() {
  const auto all = profiles_.List();
  if (all.empty()) return;
  size_t i = 0;
  while (i < all.size() && all[i] != profileName_) ++i;
  const std::string next = all[i < all.size() ? (i + 1) % all.size() : 0];
  overrideProfile_ = next;
  overrideSession_ = liveSession_;
  if (settingsWindow_.IsOpen()) { selectedProfile_ = next; profilePinned_ = true; }
  Notify(L"PROFILE  %ls", Widen(next).c_str());
}

std::string App::UpdateRepo() const { return gs_.updateRepo.empty() ? std::string(LMU_OVERLAY_REPO) : gs_.updateRepo; }

void App::Notify(const wchar_t* fmt, const wchar_t* arg) {
  swprintf_s(notice_, fmt, arg);
  Logf("%ls", notice_);
  noticeAt_ = NowSeconds();
}

void App::ApplyWheelBindings() {
  std::vector<ButtonBinding> b;
  for (const std::string& s : gs_.wheelButtons) b.push_back(ButtonBinding::Parse(s));
  wheel_.SetBindings(b);
}

void App::OnWheelAction(WheelAction a) {
  switch (a) {
    case WheelAction::NextDeltaRef: CycleDeltaRef(1); break;
    case WheelAction::PrevDeltaRef: CycleDeltaRef(-1); break;
    case WheelAction::NextProfile: CycleProfile(); break;
    case WheelAction::ToggleOverlay: overlay_.SetUserHidden(!overlay_.UserHidden()); break;
    default: break;
  }
}

void App::SelectProfile(const std::string& name) {
  selectedProfile_ = name;
  profilePinned_ = true;
  if (name != profileName_) ActivateProfile(name);
}

void App::ProfileEdited() {
  rebuildPending_ = savePending_ = true;
  lastEdit_ = NowSeconds();
  ReadDeltaRef();
}

void App::SaveProfileNow() {
  profiles_.Save(profileName_, profileDoc_);
  savePending_ = false;
}

void App::ProfileRenamed(const std::string& from, const std::string& to) {
  if (profileName_ == from) profileName_ = to;
  if (selectedProfile_ == from) selectedProfile_ = to;
  for (std::string& p : gs_.profileFor) if (p == from) p = to;
  if (gs_.manualProfile == from) gs_.manualProfile = to;
  SettingsChanged();
}

void App::OnWidgetMoved(const char* section, int x, int y) {
  profileDoc_.SetInt(section, "x", x);
  profileDoc_.SetInt(section, "y", y);
  savePending_ = true; // no rebuild needed: the visual already moved
  lastEdit_ = NowSeconds();
}

void App::SettingsChanged() {
  // Apply right away (cheap); write the file and restart the reader once edits settle.
  ApplyProcessSettings();
  ApplyWheelBindings();
  overlay_.SetSettings(gs_);
  nextMonitorCheck_ = 0;
  settingsDirty_ = true;
  lastSettingsEdit_ = NowSeconds();
}

void App::SaveSettingsNow() {
  gs_.Save(settingsDoc_);
  settingsDoc_.Save(settingsPath_);
  settingsDirty_ = false;
  if (reader_ && readerPollHz_ != gs_.pollHz) StartReader();
}

void App::ReloadFromDisk() {
  if (settingsDoc_.Load(settingsPath_)) gs_.Load(settingsDoc_);
  ApplyProcessSettings();
  ApplyWheelBindings();
  overlay_.SetSettings(gs_);
  if (reader_ && readerPollHz_ != gs_.pollHz) StartReader();
  savePending_ = false; // discard unsaved edits: the files win
  ActivateProfile(profileName_);
  nextMonitorCheck_ = 0;
}

// ----------------------------------------------------------------------------
// Tray, hotkeys, settings window

void App::OpenSettings() {
  if (settingsWindow_.IsOpen()) { settingsWindow_.Open(*this, inst_); return; } // brings it to front
  selectedProfile_ = profileName_;
  profilePinned_ = false;
  settingsWindow_.Open(*this, inst_);
}

void App::CreateTray() {
  tray_ = NOTIFYICONDATAW{sizeof(NOTIFYICONDATAW)};
  tray_.hWnd = overlay_.Hwnd();
  tray_.uID = 1;
  tray_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  tray_.uCallbackMessage = kTrayMsg;
  tray_.hIcon = static_cast<HICON>(LoadImageW(inst_, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                             GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
  if (!tray_.hIcon) tray_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wcscpy_s(tray_.szTip, demoOnly_ ? L"LMU Overlay (demo)" : L"LMU Overlay");
  Shell_NotifyIconW(NIM_ADD, &tray_);

  if (gs_.startupTip && !gs_.openSettingsOnStart) {
    NOTIFYICONDATAW tip = tray_;
    tip.uFlags = NIF_INFO;
    tip.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
    wcscpy_s(tip.szInfoTitle, L"LMU Overlay is running");
    wcscpy_s(tip.szInfo, L"Double-click the tray icon for settings.\nCtrl+Alt+E: move widgets  ·  Ctrl+Alt+D: delta reference");
    Shell_NotifyIconW(NIM_MODIFY, &tip);
  }
}

void App::ShowTrayMenu(HWND hwnd) {
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING | MF_DEFAULT, kMenuSettings, L"Settings...");
  AppendMenuW(menu, MF_STRING | (overlay_.EditMode() ? MF_CHECKED : 0), kMenuEdit, L"Move widgets\tCtrl+Alt+E");
  AppendMenuW(menu, MF_STRING | (overlay_.UserHidden() ? 0 : MF_CHECKED), kMenuToggle, L"Show overlay\tCtrl+Alt+O");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kMenuReload, L"Reload settings from disk");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kMenuQuit, L"Quit\tCtrl+Alt+Q");
  POINT pt;
  GetCursorPos(&pt);
  SetForegroundWindow(hwnd); // required so the menu closes when clicking elsewhere
  const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
  DestroyMenu(menu);
  switch (cmd) {
    case kMenuSettings: OpenSettings(); break;
    case kMenuEdit: overlay_.SetEditMode(!overlay_.EditMode()); break;
    case kMenuToggle: overlay_.SetUserHidden(!overlay_.UserHidden()); break;
    case kMenuReload: ReloadFromDisk(); break;
    case kMenuQuit: running_ = false; break;
  }
}

bool App::HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) {
  static const UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
  if (msg == taskbarCreated && taskbarCreated) { // Explorer restarted: re-add the tray icon
    Shell_NotifyIconW(NIM_ADD, &tray_);
    return false;
  }
  switch (msg) {
    case WM_HOTKEY:
      if (wp == kHotkeyEdit) overlay_.SetEditMode(!overlay_.EditMode());
      else if (wp == kHotkeyToggle) overlay_.SetUserHidden(!overlay_.UserHidden());
      else if (wp == kHotkeyQuit) running_ = false;
      else if (wp == kHotkeyRef) CycleDeltaRef();
      else if (wp == kHotkeyProfile) CycleProfile();
      result = 0;
      return true;
    case kTrayMsg:
      if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) ShowTrayMenu(hwnd);
      else if (LOWORD(lp) == WM_LBUTTONDBLCLK) OpenSettings();
      result = 0;
      return true;
    case WM_DEVICECHANGE:
      wheel_.DevicesChanged();
      return false;
    case kOpenSettingsMsg:
      OpenSettings();
      result = 0;
      return true;
  }
  return false;
}
