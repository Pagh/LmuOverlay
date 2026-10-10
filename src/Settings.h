#pragma once
#include "Ini.h"
#include <windows.h>
#include <string>
#include <vector>

enum class SessionKind { Practice, Qualifying, Race, Count };
const char* SessionKindName(SessionKind k);
SessionKind SessionKindFromLmu(long session); // LMU mSession: 0 test, 1-4 practice, 5-8 qualifying, 9 warmup, 10-13 race

// Application-wide settings (settings.ini). Profiles hold everything about widgets.
struct GeneralSettings {
  int pollHz = 60;            // how often we copy from LMU's shared memory
  int maxFps = 60;            // cap for overlay redraws (widgets only redraw when their content changes)
  float scale = 1.0f;         // global UI scale, multiplied by each widget's own scale
  bool lowerPriority = true;  // run below normal priority so the overlay never competes with LMU
  bool onlyOnTrack = true;    // hide while in menus / garage monitor
  unsigned long long affinityMask = 0;
  bool startupTip = true;
  bool openSettingsOnStart = true;
  bool restApi = true;            // read damage details from LMU's local REST API
  int restIntervalS = 5;          // while driving; plus right after every impact

  std::string monitor = "auto";   // "auto" (follow LMU's window) or a device name like \\.\DISPLAY1
  std::string lastGameMonitor;    // remembered for when LMU isn't running

  bool autoSwitch = true;         // pick the profile from the session type
  std::string profileFor[static_cast<int>(SessionKind::Count)] = {"Practice", "Qualifying", "Race"};
  std::string manualProfile = "Race";

  // Race to plan for in practice / qualifying (Strategy widget).
  bool planEnabled = true;
  bool planByLaps = false;          // else timed
  int planMinutes = 60;
  int planLaps = 25;

  std::string updateRepo;           // GitHub "owner/name" with the releases ("" = Version.h default)
  bool updateCheckAtStart = true;

  static constexpr int kWheelActions = 4;  // see WheelAction
  std::string wheelButtons[kWheelActions]; // serialized ButtonBinding per action ("" = none)

  void Load(const IniDoc& doc);
  void Save(IniDoc& doc) const;
};

// "Start with Windows": a Run entry for the current user that starts the overlay quietly in the
// tray (--autostart); it then waits for LMU and shows itself when you drive.
bool StartWithWindows();          // the entry exists and points at this exe
bool SetStartWithWindows(bool on);

struct MonitorInfo {
  std::string device;  // \\.\DISPLAYn
  RECT rect;
  bool primary;
};
std::vector<MonitorInfo> EnumMonitors();

// Profiles live in <exe>\profiles\<name>.ini, one INI section per widget.
class ProfileManager {
public:
  void Init(const std::wstring& dir);
  std::vector<std::string> List() const;
  bool Exists(const std::string& name) const;
  bool Load(const std::string& name, IniDoc& out) const;
  bool Save(const std::string& name, const IniDoc& doc) const;
  bool Create(const std::string& name, const IniDoc& from) const;
  bool Rename(const std::string& from, const std::string& to) const;
  bool Delete(const std::string& name) const;
  static bool ValidName(const std::string& name);

  // Writes every option (with defaults where missing) so profile files are self-documenting.
  static void Normalize(IniDoc& doc);

private:
  std::wstring PathFor(const std::string& name) const;
  std::wstring dir_;
};
