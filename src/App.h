#pragma once
#include "DemoSource.h"
#include "LmuRest.h"
#include "Timing.h"
#include "Ini.h"
#include "Model.h"
#include "Overlay.h"
#include "Settings.h"
#include "SettingsWindow.h"
#include "SharedMemoryReader.h"
#include "WheelButtons.h"
#include "GameWatch.h"
#include "Updater.h"
#include <shellapi.h>
#include <memory>
#include <string>

// Owns everything: data sources, profiles, overlay, tray icon and settings window.
// All of it runs on the main thread except the data source threads.
class App {
public:
  int Run(HINSTANCE inst, bool demoOnly);

  // ---- used by the settings window ----
  GeneralSettings& Settings() { return gs_; }
  void SettingsChanged();                         // save settings.ini and apply

  ProfileManager& Profiles() { return profiles_; }
  const std::string& ProfileName() const { return profileName_; }
  IniDoc& ProfileDoc() { return profileDoc_; }    // the profile on screen (= the one being edited while settings are open)
  void SelectProfile(const std::string& name);    // switch the edited/shown profile
  void ProfileEdited();                           // ProfileDoc() changed: rebuild widgets + save (debounced)
  void ProfileRenamed(const std::string& from, const std::string& to);

  Overlay& GetOverlay() { return overlay_; }
  bool Preview() const { return preview_; }
  void SetPreview(bool on) { preview_ = on; }

  bool LiveConnected() const { return liveModel_.connected; }
  bool LiveOnTrack() const { return liveModel_.onTrack; }
  SessionKind LiveSession() const { return liveSession_; }
  std::string OverlayMonitor() const { return monitorDevice_; }

  const std::wstring& Dir() const { return exeDir_; }
  WheelButtons& Wheel() { return wheel_; }
  Updater& Updates() { return updater_; }
  std::string UpdateRepo() const;                 // settings, else the built-in default
  void OpenSettings();
  void Quit() { running_ = false; }

private:
  void LoadOrMigrate();
  void ApplyProcessSettings();
  void StartReader();
  void UpdateDemoSource(bool needed);
  void UpdateMonitor(double now);
  std::string WantedProfile() const;
  void ActivateProfile(const std::string& name);
  void SaveProfileNow();
  void SaveSettingsNow();
  void ReloadFromDisk();
  void OnWidgetMoved(const char* section, int x, int y);
  void CreateTray();
  void ShowTrayMenu(HWND hwnd);
  bool HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT& result);
  void Tick(double now);
  void UpdateRest(double now);
  void ReadDeltaRef();          // from the profile's [delta] reference option
  void CycleDeltaRef(int step = 1);
  void CycleProfile();
  void Notify(const wchar_t* fmt, const wchar_t* arg);
  void ApplyWheelBindings();
  void OnWheelAction(WheelAction a);
  void LogSession(double now);    // session / lap / perf lines in logs\overlay.log

  HINSTANCE inst_ = nullptr;
  bool demoOnly_ = false;
  bool running_ = true;

  std::wstring exeDir_, settingsPath_;
  IniDoc settingsDoc_;
  GeneralSettings gs_;
  ProfileManager profiles_;
  std::string profileName_;        // profile on screen
  std::string selectedProfile_;    // profile chosen in the settings window (shown while it's open)
  bool profilePinned_ = false;     // the user picked selectedProfile_ in the window
  IniDoc profileDoc_;
  bool rebuildPending_ = false, savePending_ = false;
  double lastEdit_ = 0, lastRebuild_ = 0;
  bool settingsDirty_ = false;
  double lastSettingsEdit_ = 0;

  std::unique_ptr<TripleBuffer<Snapshot>> liveBuf_, demoBuf_;
  std::unique_ptr<SharedMemoryReader> reader_;
  std::unique_ptr<DemoSource> demo_;
  int readerPollHz_ = 0;
  Model liveModel_, demoModel_;
  Timing liveTiming_, demoTiming_;
  LmuRest rest_;
  RestData restData_, demoRest_;
  double nextRestPoll_ = 0, lastImpactET_ = -1, lastGarageRead_ = -100;
  DeltaRef deltaRef_ = DeltaRef::LmuBest;
  double deltaRefChangedAt_ = -100;
  SessionKind liveSession_ = SessionKind::Practice;
  std::string overrideProfile_;    // picked with the profile button / hotkey, until the session type changes
  SessionKind overrideSession_ = SessionKind::Practice;
  WheelButtons wheel_;
  GameWatch gameWatch_;
  Updater updater_;
  bool updateNotified_ = false;
  double nextWheelPoll_ = 0;
  std::vector<WheelAction> wheelFired_;
  wchar_t notice_[48] = L"";
  // Log state
  bool loggedConnected_ = false;
  long loggedSession_ = -1;
  std::string loggedTrack_;
  int loggedLaps_ = -1;
  double perfLogAt_ = 0, perfRenderSum_ = 0, perfRenderMax_ = 0, perfRedraws_ = 0, perfSnaps_ = 0;
  float perfHoldMax_ = 0;
  int perfSeconds_ = 0;
  unsigned long long cpuLast_ = 0;
  double cpuLastAt_ = 0;
  double noticeAt_ = -100;

  Overlay overlay_;
  SettingsWindow settingsWindow_;
  bool preview_ = true;
  std::string monitorDevice_;
  double nextMonitorCheck_ = 0;
  NOTIFYICONDATAW tray_{sizeof(NOTIFYICONDATAW)};
};
