// Manual test: LmuUpdaterTest.exe owner/repo [install]  -> prints what the updater finds.
// With "install" it also downloads the release over ITSELF and starts it, like the overlay does
// (run it from a copy in a scratch folder, with its version string patched lower than the release).
#include "Updater.h"
#include "Version.h"
#include <cstdio>
#include <cstring>
int main(int argc, char** argv) {
  Updater u;
  u.Check(argc > 1 ? argv[1] : "ocornut/imgui");
  for (int i = 0; i < 200 && (u.Get().phase == Updater::Phase::Checking); ++i) Sleep(100);
  Updater::State s = u.Get();
  std::printf("this %s  phase %d  latest '%s'  error '%s'\n", LMU_OVERLAY_VERSION, static_cast<int>(s.phase), s.latest.c_str(), s.error.c_str());
  std::printf("compare 1.10.2 vs 1.9.0: %d, v1.0.0 vs 1.0: %d\n", CompareVersions("1.10.2", "1.9.0"), CompareVersions("v1.0.0", "1.0"));
  if (argc > 2 && std::strcmp(argv[2], "install") == 0 && s.phase == Updater::Phase::Available) {
    u.Install();
    for (int i = 0; i < 1200 && (u.Get().phase == Updater::Phase::Downloading); ++i) Sleep(100);
    s = u.Get();
    std::printf("install phase %d  error '%s'\n", static_cast<int>(s.phase), s.error.c_str());
    if (s.phase == Updater::Phase::Ready) std::printf("launch %s\n", u.LaunchNew() ? "ok" : "FAILED");
  }
}
