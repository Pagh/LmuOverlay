#pragma once
#include <windows.h>
#include <mutex>
#include <string>
#include <thread>

// Updates from GitHub releases: asks api.github.com for the latest release of a repository,
// and if it's newer than this build, downloads its LmuOverlay.exe asset next to the running
// exe and swaps them (Windows lets a running exe be renamed). Settings, profiles and records
// live in separate files and are kept. Everything runs on a background thread, only when
// asked (at startup if enabled, or from the settings window), never while you drive.
class Updater {
public:
  enum class Phase { Idle, Checking, UpToDate, Available, Downloading, Ready, Failed };
  struct State {
    Phase phase = Phase::Idle;
    std::string latest;     // "1.2.0"
    std::string notes;      // release notes (first lines)
    std::string error;
  };

  ~Updater();
  void Check(const std::string& repo);   // async
  void Install();                        // async, after Check found an update
  State Get() const;
  // After Install succeeded: start the new exe (it waits for this process to exit) and return true.
  bool LaunchNew() const;
  // At startup: remove the previous exe left by an update.
  static void CleanUp();

private:
  void Run(bool install);
  void Set(Phase p, std::string error = {});

  mutable std::mutex mutex_;
  State state_;
  std::string repo_, assetUrl_;
  std::jthread thread_;
};

// "1.10.2" > "1.9.0": numeric, part by part. Leading 'v' ignored.
int CompareVersions(const std::string& a, const std::string& b);
