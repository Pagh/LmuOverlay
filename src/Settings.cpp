#include "Settings.h"
#include "widgets/Widget.h"
#include <algorithm>
#include <cstdlib>

namespace {
constexpr const char* kSessionKeys[] = {"practice", "qualifying", "race"};
}

const char* SessionKindName(SessionKind k) {
  switch (k) {
    case SessionKind::Practice: return "Practice";
    case SessionKind::Qualifying: return "Qualifying";
    case SessionKind::Race: return "Race";
    default: return "?";
  }
}

SessionKind SessionKindFromLmu(long s) {
  if (s >= 5 && s <= 8) return SessionKind::Qualifying;
  if (s >= 10 && s <= 13) return SessionKind::Race;
  return SessionKind::Practice; // test day, practice, warmup
}

void GeneralSettings::Load(const IniDoc& d) {
  const GeneralSettings def;
  pollHz = std::clamp(d.GetInt("general", "poll_hz", def.pollHz), 5, 240);
  maxFps = std::clamp(d.GetInt("general", "max_fps", def.maxFps), 5, 240);
  scale = static_cast<float>(std::clamp(d.GetFloat("general", "scale", def.scale), 0.5, 3.0));
  lowerPriority = d.GetBool("general", "lower_priority", def.lowerPriority);
  onlyOnTrack = d.GetBool("general", "only_on_track", def.onlyOnTrack);
  affinityMask = std::strtoull(d.Get("general", "affinity_mask", "0").c_str(), nullptr, 0);
  startupTip = d.GetBool("general", "startup_tip", def.startupTip);
  openSettingsOnStart = d.GetBool("general", "open_settings_on_start", def.openSettingsOnStart);
  restApi = d.GetBool("general", "rest_api", def.restApi);
  restIntervalS = std::clamp(d.GetInt("general", "rest_interval_s", def.restIntervalS), 2, 60);
  monitor = d.Get("general", "monitor", def.monitor);
  lastGameMonitor = d.Get("general", "last_game_monitor", "");

  autoSwitch = d.GetBool("profiles", "auto_switch", def.autoSwitch);
  for (int i = 0; i < static_cast<int>(SessionKind::Count); ++i)
    profileFor[i] = d.Get("profiles", kSessionKeys[i], def.profileFor[i]);
  manualProfile = d.Get("profiles", "manual", def.manualProfile);
  static const char* const kWheelKeys[kWheelActions] = {"next_delta_reference", "prev_delta_reference", "next_profile",
                                                        "toggle_overlay"};
  for (int i = 0; i < kWheelActions; ++i) wheelButtons[i] = d.Get("wheel_buttons", kWheelKeys[i], "");
  updateRepo = d.Get("update", "repo", "");
  updateCheckAtStart = d.GetBool("update", "check_at_start", def.updateCheckAtStart);
}

void GeneralSettings::Save(IniDoc& d) const {
  d.header = "LMU Overlay settings. Widget settings live in the profiles\\ folder.\n"
             "Edit through the Settings window (tray icon), or by hand while the overlay is closed.";
  d.SetInt("general", "poll_hz", pollHz);
  d.SetInt("general", "max_fps", maxFps);
  d.SetFloat("general", "scale", scale);
  d.SetBool("general", "lower_priority", lowerPriority);
  d.SetBool("general", "only_on_track", onlyOnTrack);
  d.Set("general", "affinity_mask", std::to_string(affinityMask));
  d.SetBool("general", "startup_tip", startupTip);
  d.SetBool("general", "open_settings_on_start", openSettingsOnStart);
  d.SetBool("general", "rest_api", restApi);
  d.SetInt("general", "rest_interval_s", restIntervalS);
  d.Set("general", "monitor", monitor);
  d.Set("general", "last_game_monitor", lastGameMonitor);
  d.SetBool("profiles", "auto_switch", autoSwitch);
  for (int i = 0; i < static_cast<int>(SessionKind::Count); ++i) d.Set("profiles", kSessionKeys[i], profileFor[i]);
  d.Set("profiles", "manual", manualProfile);
  static const char* const kWheelKeys[kWheelActions] = {"next_delta_reference", "prev_delta_reference", "next_profile",
                                                        "toggle_overlay"};
  for (int i = 0; i < kWheelActions; ++i) d.Set("wheel_buttons", kWheelKeys[i], wheelButtons[i]);
  d.Set("update", "repo", updateRepo);
  d.SetBool("update", "check_at_start", updateCheckAtStart);
}

std::vector<MonitorInfo> EnumMonitors() {
  std::vector<MonitorInfo> out;
  EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR mon, HDC, LPRECT, LPARAM lp) -> BOOL {
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi))
      reinterpret_cast<std::vector<MonitorInfo>*>(lp)->push_back({Narrow(mi.szDevice), mi.rcMonitor, (mi.dwFlags & MONITORINFOF_PRIMARY) != 0});
    return TRUE;
  }, reinterpret_cast<LPARAM>(&out));
  std::sort(out.begin(), out.end(), [](const MonitorInfo& a, const MonitorInfo& b) { return a.device < b.device; });
  return out;
}

void ProfileManager::Init(const std::wstring& dir) {
  dir_ = dir;
  CreateDirectoryW(dir_.c_str(), nullptr);
}

std::wstring ProfileManager::PathFor(const std::string& name) const { return dir_ + L"\\" + Widen(name) + L".ini"; }

std::vector<std::string> ProfileManager::List() const {
  std::vector<std::string> names;
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW((dir_ + L"\\*.ini").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return names;
  do {
    std::wstring n = fd.cFileName;
    names.push_back(Narrow(n.substr(0, n.size() - 4)));
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  std::sort(names.begin(), names.end());
  return names;
}

bool ProfileManager::Exists(const std::string& name) const {
  return ValidName(name) && GetFileAttributesW(PathFor(name).c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool ProfileManager::Load(const std::string& name, IniDoc& out) const {
  if (!ValidName(name) || !out.Load(PathFor(name))) return false;
  Normalize(out);
  return true;
}

bool ProfileManager::Save(const std::string& name, const IniDoc& doc) const {
  return ValidName(name) && doc.Save(PathFor(name));
}

bool ProfileManager::Create(const std::string& name, const IniDoc& from) const {
  if (!ValidName(name) || Exists(name)) return false;
  IniDoc copy = from;
  Normalize(copy);
  return Save(name, copy);
}

bool ProfileManager::Rename(const std::string& from, const std::string& to) const {
  if (!ValidName(to) || Exists(to)) return false;
  return MoveFileW(PathFor(from).c_str(), PathFor(to).c_str()) != 0;
}

bool ProfileManager::Delete(const std::string& name) const {
  return ValidName(name) && DeleteFileW(PathFor(name).c_str()) != 0;
}

bool ProfileManager::ValidName(const std::string& name) {
  if (name.empty() || name.size() > 40) return false;
  for (char c : name)
    if (!(isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_')) return false;
  return name.front() != ' ' && name.back() != ' ';
}

void ProfileManager::Normalize(IniDoc& doc) {
  doc.header = "LMU Overlay profile: one section per widget. Positions are relative to the overlay's monitor.";
  for (const WidgetType& t : WidgetTypes())
    for (const OptionDef& d : FullSchema(t))
      if (!doc.Find(t.id, d.key)) SetOptionValue(doc, t.id, d, d.def);
}
