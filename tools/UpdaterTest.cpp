// Manual test: LmuUpdaterTest.exe owner/repo  -> prints what the updater finds.
#include "Updater.h"
#include "Version.h"
#include <cstdio>
int main(int argc, char** argv) {
  Updater u;
  u.Check(argc > 1 ? argv[1] : "ocornut/imgui");
  for (int i = 0; i < 200 && (u.Get().phase == Updater::Phase::Checking); ++i) Sleep(100);
  const Updater::State s = u.Get();
  std::printf("this %s  phase %d  latest '%s'  error '%s'\n", LMU_OVERLAY_VERSION, static_cast<int>(s.phase), s.latest.c_str(), s.error.c_str());
  std::printf("compare 1.10.2 vs 1.9.0: %d, v1.0.0 vs 1.0: %d\n", CompareVersions("1.10.2", "1.9.0"), CompareVersions("v1.0.0", "1.0"));
}
