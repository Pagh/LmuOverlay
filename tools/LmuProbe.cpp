// LmuProbe: console tool that connects to LMU_Data the same way the overlay does
// and prints the raw values the overlay relies on. Useful to verify units in-game.
#include "LmuSdk.h"
#include "Clock.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

// "watch" mode: samples for a few seconds to check coordinate conventions while driving.
static int Watch(const SharedMemoryLayout* view, SharedMemoryLock* lock, SharedMemoryUpdateWaiter* waiter) {
  auto copy = std::make_unique<SharedMemoryObjectOut>();
  double sxa = 0, sxx = 0; // correlation of steering with lateral acceleration
  int n = 0;
  double minD[3] = {1e9, 1e9, 1e9}, maxD[3] = {-1, -1, -1};
  const double t0 = NowSeconds();
  while (NowSeconds() - t0 < 8.0) {
    if (!waiter->WaitForNextFrame(200)) continue;
    if (!lock->Lock(50)) continue;
    std::memcpy(&copy->scoring.scoringInfo, &view->data.scoring.scoringInfo, sizeof(ScoringInfoV01));
    std::memcpy(copy->scoring.vehScoringInfo, view->data.scoring.vehScoringInfo, sizeof(copy->scoring.vehScoringInfo));
    std::memcpy(&copy->telemetry, &view->data.telemetry, sizeof(copy->telemetry));
    lock->Unlock();
    const auto& tel = copy->telemetry;
    if (tel.playerHasVehicle && tel.playerVehicleIdx < tel.activeVehicles) {
      const TelemInfoV01& t = tel.telemInfo[tel.playerVehicleIdx];
      if (std::fabs(t.mUnfilteredSteering) > 0.05 && std::fabs(t.mLocalAccel.x) > 2) {
        sxa += t.mUnfilteredSteering * t.mLocalAccel.x;
        sxx += 1;
      }
      ++n;
    }
    const auto& sc = copy->scoring.scoringInfo;
    for (int i = 0; i < sc.mNumVehicles && i < kMaxVehicles; ++i) {
      const auto& v = copy->scoring.vehScoringInfo[i];
      const int s = v.mSector; // 0 = sector 3, 1 = sector 1, 2 = sector 2
      if (s < 0 || s > 2 || v.mInPits) continue;
      if (v.mLapDist < minD[s]) minD[s] = v.mLapDist;
      if (v.mLapDist > maxD[s]) maxD[s] = v.mLapDist;
    }
  }
  const auto& sc = copy->scoring.scoringInfo;
  std::printf("samples %d  steering*accelX: sum %.1f over %.0f turning samples -> +x is %s\n", n, sxa, sxx,
              sxx < 10 ? "unknown (drive through corners)" : sxa > 0 ? "RIGHT" : "LEFT");
  std::printf("mSector lapDist ranges (track %.0f m): sector1(mSector=1) %.0f..%.0f  sector2(=2) %.0f..%.0f  sector3(=0) %.0f..%.0f\n",
              sc.mLapDist, minD[1], maxD[1], minD[2], maxD[2], minD[0], maxD[0]);
  std::printf("sectorFlag %d %d %d  yellowState %d\n", sc.mSectorFlag[0], sc.mSectorFlag[1], sc.mSectorFlag[2], sc.mYellowFlagState);
  for (int i = 0; i < sc.mNumVehicles && i < kMaxVehicles; ++i) {
    const auto& v = copy->scoring.vehScoringInfo[i];
    const double sp = std::sqrt(v.mLocalVel.x * v.mLocalVel.x + v.mLocalVel.y * v.mLocalVel.y + v.mLocalVel.z * v.mLocalVel.z);
    if (sp < 15 && !v.mInPits)
      std::printf("  slow veh %ld lapDist %.0f mSector %d speed %.1f garage %d lateral %.1f edge %.1f\n", v.mID, v.mLapDist,
                  v.mSector, sp, v.mInGarageStall, v.mPathLateral, v.mTrackEdge);
  }
  return 0;
}

// "map" mode: builds the track outline from every car's position and reports its winding,
// which tells the world's handedness (Spa, Le Mans, Monza... run clockwise seen from above).
static int MapMode(const SharedMemoryLayout* view, SharedMemoryLock* lock, SharedMemoryUpdateWaiter* waiter) {
  auto copy = std::make_unique<SharedMemoryObjectOut>();
  constexpr int kBins = 400;
  double sx[kBins]{}, sz[kBins]{};
  int n[kBins]{};
  double L = 0;
  const double t0 = NowSeconds();
  while (NowSeconds() - t0 < 15.0) {
    if (!waiter->WaitForNextFrame(200)) continue;
    if (!lock->Lock(50)) continue;
    std::memcpy(&copy->scoring.scoringInfo, &view->data.scoring.scoringInfo, sizeof(ScoringInfoV01));
    std::memcpy(copy->scoring.vehScoringInfo, view->data.scoring.vehScoringInfo, sizeof(copy->scoring.vehScoringInfo));
    lock->Unlock();
    const auto& sc = copy->scoring.scoringInfo;
    L = sc.mLapDist;
    for (int i = 0; i < sc.mNumVehicles && i < kMaxVehicles; ++i) {
      const auto& v = copy->scoring.vehScoringInfo[i];
      if (v.mInPits || L <= 0) continue;
      const int b = std::clamp(static_cast<int>(v.mLapDist / L * kBins), 0, kBins - 1);
      sx[b] += v.mPos.x; sz[b] += v.mPos.z; ++n[b];
    }
  }
  int filled = 0;
  double area = 0, px = 0, pz = 0, fx = 0, fz = 0;
  bool first = true;
  for (int b = 0; b < kBins; ++b) {
    if (!n[b]) continue;
    const double x = sx[b] / n[b], z = sz[b] / n[b];
    if (first) { fx = x; fz = z; first = false; }
    else area += px * z - x * pz;
    px = x; pz = z; ++filled;
  }
  area += px * fz - fx * pz;
  std::printf("track %.0f m  bins filled %d/%d  shoelace(x,z) %.0f -> %s in (x,z)\n", L, filled, kBins, area / 2,
              area > 0 ? "x->z rotation positive" : "x->z rotation negative");
  for (int b = 0; b < kBins; b += 40)
    if (n[b]) std::printf("  d=%5.0f  x=%8.1f z=%8.1f\n", b * L / kBins, sx[b] / n[b], sz[b] / n[b]);
  return 0;
}

int main(int argc, char** argv) {
  auto waiter = SharedMemoryUpdateWaiter::MakeSharedMemoryUpdateWaiter();
  if (!waiter) { std::printf("LMU events not found: is LMU running (and recent enough)?\n"); return 1; }
  HANDLE map = OpenFileMappingA(FILE_MAP_READ, FALSE, LMU_SHARED_MEMORY_FILE);
  if (!map) { std::printf("LMU_Data mapping not found (error %lu)\n", GetLastError()); return 1; }
  auto* view = static_cast<const SharedMemoryLayout*>(MapViewOfFile(map, FILE_MAP_READ, 0, 0, sizeof(SharedMemoryLayout)));
  auto lock = SharedMemoryLock::MakeSharedMemoryLock();
  if (!view || !lock) { std::printf("map/lock failed (error %lu)\n", GetLastError()); return 1; }

  if (argc > 1 && std::strcmp(argv[1], "watch") == 0) return Watch(view, &*lock, &*waiter);
  if (argc > 1 && std::strcmp(argv[1], "map") == 0) return MapMode(view, &*lock, &*waiter);

  auto copy = std::make_unique<SharedMemoryObjectOut>();
  int frames = 0, scoringUpdates = 0;
  double lastScoringET = -1;
  const double t0 = NowSeconds();
  double holdMax = 0;
  while (NowSeconds() - t0 < 1.0) {
    if (!waiter->WaitForNextFrame(200)) continue;
    const double a = NowSeconds();
    if (!lock->Lock(50)) continue;
    std::memcpy(&copy->generic, &view->data.generic, sizeof(copy->generic));
    std::memcpy(&copy->scoring.scoringInfo, &view->data.scoring.scoringInfo, sizeof(ScoringInfoV01));
    std::memcpy(copy->scoring.vehScoringInfo, view->data.scoring.vehScoringInfo, sizeof(copy->scoring.vehScoringInfo));
    std::memcpy(&copy->telemetry, &view->data.telemetry, sizeof(copy->telemetry));
    lock->Unlock();
    const double b = NowSeconds();
    if (b - a > holdMax) holdMax = b - a;
    ++frames;
    if (copy->scoring.scoringInfo.mCurrentET != lastScoringET) { ++scoringUpdates; lastScoringET = copy->scoring.scoringInfo.mCurrentET; }
  }

  const auto& g = copy->generic;
  const auto& sc = copy->scoring.scoringInfo;
  const auto& tel = copy->telemetry;
  std::printf("frames signalled in 1s: %d   scoring updates: %d   max copy time (full struct): %.1f us\n", frames,
              scoringUpdates, holdMax * 1e6);
  std::printf("gameVersion %ld  appWindow %p  optionsLocation %u  size %ux%u windowed %lu\n", g.gameVersion,
              (void*)g.appInfo.mAppWindow, g.appInfo.mOptionsLocation, (unsigned)g.appInfo.mWidth,
              (unsigned)g.appInfo.mHeight, g.appInfo.mWindowed);
  std::printf("track '%.64s'  session %ld  phase %u  inRealtime %d  vehicles %ld  lapDist %.1f  ET %.1f/%.1f  maxLaps %ld\n",
              sc.mTrackName, sc.mSession, sc.mGamePhase, sc.mInRealtime, sc.mNumVehicles, sc.mLapDist, sc.mCurrentET,
              sc.mEndET, sc.mMaxLaps);
  std::printf("telemetry: active %u  playerIdx %u  hasVehicle %d\n", tel.activeVehicles, tel.playerVehicleIdx, tel.playerHasVehicle);
  std::printf("flags: yellowState %d  sectorFlag %d %d %d  startLight %u/%u  TL steps/point %u steps/penalty %u\n",
              sc.mYellowFlagState, sc.mSectorFlag[0], sc.mSectorFlag[1], sc.mSectorFlag[2], sc.mStartLight,
              sc.mNumRedLights, sc.mTrackLimitsStepsPerPoint, sc.mTrackLimitsStepsPerPenalty);
  std::printf("weather: ambient %.1f track %.1f raining %.2f wet %.2f/%.2f/%.2f  remaining %.1f  gameMode %u  grip %u\n",
              sc.mAmbientTemp, sc.mTrackTemp, sc.mRaining, sc.mMinPathWetness, sc.mAvgPathWetness, sc.mMaxPathWetness,
              sc.mSessionTimeRemaining, sc.mGameMode, sc.mTrackGripLevel);
  {
    int slow = 0;
    for (int i = 0; i < sc.mNumVehicles && i < kMaxVehicles; ++i) {
      const auto& v = copy->scoring.vehScoringInfo[i];
      const double sp = std::sqrt(v.mLocalVel.x * v.mLocalVel.x + v.mLocalVel.y * v.mLocalVel.y + v.mLocalVel.z * v.mLocalVel.z);
      if (i < 6 || (sp < 15 && !v.mInPits))
        std::printf("  veh %2ld lapDist %7.1f speed %5.1f m/s inPits %d pitState %u flag %u indPhase %u countLap %u "
                    "underYellow %d penalties %d pos %.0f,%.0f,%.0f fuel%% %u\n",
                    v.mID, v.mLapDist, sp, v.mInPits, v.mPitState, v.mFlag, v.mIndividualPhase, v.mCountLapFlag,
                    v.mUnderYellow, v.mNumPenalties, v.mPos.x, v.mPos.y, v.mPos.z, v.mFuelFraction);
      if (sp < 15 && !v.mInPits) ++slow;
    }
    std::printf("  slow cars on track: %d\n", slow);
  }
  std::printf("vehicles (name | veh file | class | telemetry model):\n");
  for (int i = 0; i < sc.mNumVehicles && i < kMaxVehicles; ++i) {
    const auto& v = copy->scoring.vehScoringInfo[i];
    const char* model = "";
    for (int k = 0; k < tel.activeVehicles && k < kMaxVehicles; ++k)
      if (tel.telemInfo[k].mID == v.mID) model = tel.telemInfo[k].mVehicleModel;
    std::printf("  %2d '%.64s' | '%.32s' | '%.32s' | '%.30s'\n", v.mID, v.mVehicleName, v.mVehFilename, v.mVehicleClass, model);
  }
  for (int i = 0; i < sc.mNumVehicles && i < kMaxVehicles; ++i) {
    const auto& v = copy->scoring.vehScoringInfo[i];
    if (!v.mIsPlayer) continue;
    std::printf("player scoring: '%.32s' class '%.32s' place %u laps %d lapDist %.1f best %.3f last %.3f inPits %d sector %d\n",
                v.mDriverName, v.mVehicleClass, v.mPlace, v.mTotalLaps, v.mLapDist, v.mBestLapTime, v.mLastLapTime,
                v.mInPits, v.mSector);
  }
  if (tel.playerHasVehicle && tel.playerVehicleIdx < tel.activeVehicles) {
    const TelemInfoV01& t = tel.telemInfo[tel.playerVehicleIdx];
    std::printf("player telem: '%.64s' gear %ld rpm %.0f/%.0f fuel %.2f/%.2f\n", t.mVehicleName, t.mGear, t.mEngineRPM,
                t.mEngineMaxRPM, t.mFuel, t.mFuelCapacity);
    std::printf("  virtualEnergy %.4f  SoC %.4f  battery %.4f  motorState %u  regen %.1f  deltaBest %.3f\n",
                t.mVirtualEnergy, t.mSoC, t.mBatteryChargeFraction, t.mElectricBoostMotorState, t.mRegen, t.mDeltaBest);
    std::printf("  TL steps %u  water %.1f oil %.1f  gapAhead %.3f gapBehind %.3f  pos %.1f,%.1f,%.1f  ori0 %.2f,%.2f,%.2f\n",
                t.mTrackLimitsSteps, t.mEngineWaterTemp, t.mEngineOilTemp, t.mTimeGapCarAhead, t.mTimeGapCarBehind,
                t.mPos.x, t.mPos.y, t.mPos.z, t.mOri[0].x, t.mOri[0].y, t.mOri[0].z);
    std::printf("  TC %u/%u cut %u/%u slip %u/%u ABS %u/%u map %u/%u rearBias %.3f\n", t.mTC, t.mTCMax, t.mTCCut,
                t.mTCCutMax, t.mTCSlip, t.mTCSlipMax, t.mABS, t.mABSMax, t.mMotorMap, t.mMotorMapMax, t.mRearBrakeBias);
    for (int w = 0; w < 4; ++w) {
      const auto& wh = t.mWheel[w];
      std::printf("  wheel %d: surface %.1f/%.1f/%.1f K  inner %.1f/%.1f/%.1f K  carcass %.1f K  optimal %.1f  "
                  "compound %u/%u  pressure %.1f  wear %.3f  brakeTemp %.1f\n", w,
                  wh.mTemperature[0], wh.mTemperature[1], wh.mTemperature[2], wh.mTireInnerLayerTemperature[0],
                  wh.mTireInnerLayerTemperature[1], wh.mTireInnerLayerTemperature[2], wh.mTireCarcassTemperature,
                  wh.mOptimalTemp, wh.mCompoundIndex, wh.mCompoundType, wh.mPressure, wh.mWear, wh.mBrakeTemp);
    }
  }
  return 0;
}
