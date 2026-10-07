#include "DemoSource.h"
#include "Clock.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

constexpr double kTrackLen = 5200.0;
constexpr int kCars = 24;
constexpr int kPlayer = 4;

struct DemoCar {
  const char* driver;
  const char* cls;
  const char* vehicle;
  double lapTime;
  double start; // metres of head start
};

const DemoCar kGrid[kCars] = {
  {"A. Rossi", "Hypercar", "Ferrari 499P", 209.8, 300}, {"K. Muller", "Hypercar", "Porsche 963", 210.1, 260},
  {"J. Smith", "Hypercar", "Toyota GR010", 210.0, 230}, {"L. Dubois", "Hypercar", "Peugeot 9X8", 210.6, 190},
  {"You", "Hypercar", "Ferrari 499P", 210.3, 160},      {"M. Tanaka", "Hypercar", "Cadillac V-Series.R", 210.4, 120},
  {"P. Novak", "Hypercar", "BMW M Hybrid V8", 210.9, 90}, {"R. Silva", "Hypercar", "Alpine A424", 211.2, 40},
  {"T. Berg", "LMP2", "Oreca 07", 219.5, 20},            {"D. Costa", "LMP2", "Oreca 07", 219.8, -60},
  {"F. Weber", "LMP2", "Oreca 07", 220.1, -120},          {"G. Leone", "LMP2", "Oreca 07", 220.4, -200},
  {"H. Kim", "GT3", "Porsche 911 GT3 R", 234.0, -260},    {"I. Novak", "GT3", "BMW M4 GT3", 234.3, -330},
  {"J. Evans", "GT3", "Ferrari 296 GT3", 234.5, -380},    {"K. Ivanov", "GT3", "Aston Martin Vantage", 234.9, -440},
  {"L. Moreau", "GT3", "Lexus RC F GT3", 235.2, -520},    {"M. Russo", "GT3", "Corvette Z06 GT3.R", 235.5, -600},
  {"N. Olsen", "GT3", "Ford Mustang GT3", 235.8, -660},   {"O. Haas", "GT3", "McLaren 720S GT3", 236.1, -720},
  {"P. Lund", "GT3", "Lamborghini Huracan", 236.4, -800}, {"Q. Sato", "GT3", "Mercedes-AMG GT3", 236.7, -860},
  {"R. Vidal", "GT3", "Porsche 911 GT3 R", 237.0, -920},  {"S. Ahmed", "GT3", "BMW M4 GT3", 237.3, -990},
};

template <size_t N>
void Copy(char (&dst)[N], const char* src) { strncpy_s(dst, src, _TRUNCATE); }

// Demo track: a closed curve, plus its forward direction, in LMU's world (x right, z up seen from above).
void TrackPoint(double d, double& x, double& z, double& fx, double& fz) {
  const double u = d / kTrackLen * 6.283185307;
  x = 900.0 * std::cos(u) + 220.0 * std::cos(2.0 * u);
  z = 520.0 * std::sin(u) - 160.0 * std::sin(3.0 * u);
  // Direction of travel = derivative of the curve (clockwise seen from above, like most tracks).
  double dx = -900.0 * std::sin(u) - 440.0 * std::sin(2.0 * u), dz = 520.0 * std::cos(u) - 480.0 * std::cos(3.0 * u);
  dx = -dx; dz = -dz;
  const double len = std::sqrt(dx * dx + dz * dz);
  fx = dx / len;
  fz = dz / len;
}

// World position of a car at lap distance d, `left` metres left of the centre line.
TelemVect3 CarPos(double d, double left) {
  double x, z, fx, fz;
  TrackPoint(kTrackLen - std::fmod(d, kTrackLen), x, z, fx, fz);
  return {x - fz * left, 0.0, z + fx * left};
}

} // namespace

DemoSource::DemoSource(TripleBuffer<Snapshot>& out, int pollHz) : out_(out), period_(1.0 / std::max(pollHz, 1)) {}
DemoSource::~DemoSource() { Stop(); }

void DemoSource::Start() { thread_ = std::jthread([this](std::stop_token st) { Run(st); }); }

void DemoSource::Stop() {
  if (thread_.joinable()) { thread_.request_stop(); thread_.join(); }
}

void DemoSource::Run(std::stop_token st) {
  SetThreadDescription(GetCurrentThread(), L"Demo data source");
  PreciseTimer timer;
  const double t0 = NowSeconds();
  while (!st.stop_requested()) {
    Snapshot& s = out_.WriteBuffer();
    Fill(s, NowSeconds() - t0 + 1000.0); // start a few laps in
    s.seq = ++seq_;
    s.copyTime = NowSeconds();
    out_.Publish();
    timer.Sleep(period_);
  }
}

void DemoSource::Fill(Snapshot& s, double t) {
  s.connected = true;
  s.demo = true;
  s.gameVersion = 0;
  s.lockWaitUs = s.lockHoldUs = 0.f;
  s.app.mOptionsLocation = 3;

  ScoringInfoV01& sc = s.scoring;
  Copy(sc.mTrackName, "Demo Ring");
  sc.mSession = 10;
  sc.mCurrentET = t;
  sc.mEndET = 6.0 * 3600.0;
  sc.mMaxLaps = 2147483647;
  sc.mLapDist = kTrackLen;
  sc.mNumVehicles = kCars;
  sc.mGamePhase = 5;
  sc.mInRealtime = true;
  sc.mAmbientTemp = 24.0;
  sc.mTrackTemp = 33.0;
  sc.mSessionTimeRemaining = static_cast<float>(sc.mEndET - t);
  s.numVehicles = kCars;
  s.numModels = kCars;

  double dist[kCars];
  for (int i = 0; i < kCars; ++i) {
    const double v = kTrackLen / kGrid[i].lapTime;
    // Small periodic pace variation so gaps breathe a little.
    dist[i] = kGrid[i].start + v * t + 25.0 * std::sin(t * 0.05 + i);
  }
  // Every minute the car behind runs alongside for a few seconds (shows the radar).
  const double side = std::fmod(t, 60.0);
  const bool alongside = side < 8.0;
  if (alongside) dist[kPlayer + 1] = dist[kPlayer] - 6.0 + 9.0 * std::sin(side / 8.0 * 3.14159);
  int order[kCars];
  for (int i = 0; i < kCars; ++i) order[i] = i;
  std::sort(order, order + kCars, [&](int a, int b) { return dist[a] > dist[b]; });

  for (int p = 0; p < kCars; ++p) {
    const int i = order[p];
    VehicleScoringInfoV01& v = s.vehicles[i];
    const DemoCar& c = kGrid[i];
    const double speed = kTrackLen / c.lapTime;
    v = VehicleScoringInfoV01{};
    v.mID = i;
    Copy(v.mDriverName, c.driver);
    Copy(v.mVehicleName, c.vehicle);
    Copy(v.mVehicleClass, c.cls);
    v.mTotalLaps = static_cast<short>(std::floor(dist[i] / kTrackLen));
    v.mLapDist = std::fmod(dist[i], kTrackLen);
    v.mPlace = static_cast<unsigned char>(p + 1);
    v.mIsPlayer = i == kPlayer;
    v.mControl = v.mIsPlayer ? 0 : 1;
    v.mEstimatedLapTime = c.lapTime;
    v.mBestLapTime = c.lapTime - 0.8;
    v.mLastLapTime = c.lapTime + 0.3 * std::sin(v.mTotalLaps + i);
    v.mTimeBehindLeader = (dist[order[0]] - dist[i]) / speed;
    v.mTimeBehindNext = p > 0 ? (dist[order[p - 1]] - dist[i]) / speed : 0.0;
    v.mLapsBehindLeader = static_cast<long>(std::floor((dist[order[0]] - dist[i]) / kTrackLen));
    v.mNumPitstops = static_cast<short>(v.mTotalLaps / 14);
    v.mInPits = (i == 11) && std::fmod(t, 400.0) < 40.0;
    v.mLapStartET = t - v.mLapDist / speed;
    v.mTimeIntoLap = v.mLapDist / speed;
    const double frac = v.mLapDist / kTrackLen;
    v.mSector = static_cast<signed char>(frac < 0.33 ? 1 : frac < 0.68 ? 2 : 0);
    v.mBestSector1 = c.lapTime * 0.33 - 0.3;
    v.mBestSector2 = c.lapTime * 0.68 - 0.6;
    v.mBestLapSector1 = static_cast<float>(c.lapTime * 0.33 - 0.25);
    v.mBestLapSector2 = static_cast<float>(c.lapTime * 0.68 - 0.5);
    v.mLastSector1 = c.lapTime * 0.33 - 0.1;
    v.mLastSector2 = c.lapTime * 0.68 - 0.2;
    if (frac >= 0.33) v.mCurSector1 = c.lapTime * 0.33 - 0.35;
    if (frac >= 0.68) v.mCurSector2 = c.lapTime * 0.68 - 0.4;
    v.mLocalVel.z = -speed;
    s.models[i].id = i;
    strncpy_s(s.models[i].name, c.vehicle, _TRUNCATE);
    const double left = alongside && i == kPlayer + 1 ? 3.6 : 1.5 * std::sin(i * 1.7);
    v.mPos = s.models[i].pos = CarPos(v.mLapDist, left);
    v.mFuelFraction = static_cast<unsigned char>(255.0 * (1.0 - std::fmod(dist[i] / kTrackLen, 14.0) / 14.0));
  }

  // Player telemetry.
  const VehicleScoringInfoV01& pv = s.vehicles[kPlayer];
  TelemInfoV01& tm = s.telem;
  s.hasPlayerTelem = true;
  tm.mID = kPlayer;
  tm.mElapsedTime = t;
  tm.mLapNumber = pv.mTotalLaps + 1;
  tm.mLapStartET = pv.mLapStartET;
  {
    // Orientation rows (local x = left, y = up, z = back), see the radar widget.
    double x, z, fx, fz;
    TrackPoint(kTrackLen - pv.mLapDist, x, z, fx, fz);
    tm.mPos = pv.mPos;
    tm.mOri[0] = {-fz, 0.0, -fx};
    tm.mOri[1] = {0.0, 1.0, 0.0};
    tm.mOri[2] = {fx, 0.0, -fz};
  }
  Copy(tm.mTrackName, "Demo Ring");
  Copy(tm.mVehicleName, kGrid[kPlayer].vehicle);

  const double phase = std::fmod(t, 12.0) / 12.0; // a 12 s "corner cycle"
  const bool braking = phase > 0.75 && phase < 0.88;
  const double throttle = braking ? 0.0 : std::clamp((phase - 0.88) * 8.0 + (phase < 0.75 ? 1.0 : 0.0), 0.0, 1.0);
  tm.mUnfilteredThrottle = tm.mFilteredThrottle = throttle;
  tm.mUnfilteredBrake = tm.mFilteredBrake = braking ? 0.9 * std::sin((phase - 0.75) / 0.13 * 3.14159) : 0.0;
  tm.mUnfilteredClutch = 0.0;
  tm.mUnfilteredSteering = std::sin(t * 0.7) * 0.4;
  tm.mGear = 3 + static_cast<long>(phase * 4.0) % 5;
  tm.mMaxGears = 7;
  tm.mEngineMaxRPM = 9000.0;
  tm.mEngineRPM = 6500.0 + 2400.0 * std::fmod(phase * 4.0, 1.0);
  tm.mLocalVel.z = -(45.0 + 30.0 * std::sin(phase * 6.283));
  tm.mFuelCapacity = 90.0;
  tm.mFuel = 90.0 - std::fmod(t, 3000.0) * 0.0135;
  tm.mVirtualEnergy = static_cast<float>(1.0 - std::fmod(t, 3000.0) / 3200.0);
  tm.mBatteryChargeFraction = 0.5 + 0.35 * std::sin(t * 0.3);
  tm.mElectricBoostMotorState = throttle > 0.5 ? 2 : braking ? 3 : 1;
  tm.mDeltaBest = 0.6 * std::sin(t * 0.08);
  tm.mTC = 4; tm.mTCMax = 11; tm.mTCCut = 3; tm.mTCCutMax = 11; tm.mTCSlip = 5; tm.mTCSlipMax = 11;
  tm.mABS = 6; tm.mABSMax = 11; tm.mMotorMap = 2; tm.mMotorMapMax = 5;
  tm.mTCActive = throttle > 0.9 && phase > 0.9;
  tm.mABSActive = braking && phase < 0.8;
  tm.mRearBrakeBias = 0.455;
  tm.mDentSeverity[0] = 1; // front
  tm.mDentSeverity[7] = 2; // front-right
  tm.mLastImpactET = t - std::fmod(t, 90.0);
  tm.mEngineWaterTemp = 88.0; tm.mEngineOilTemp = 104.0;
  for (int w = 0; w < 4; ++w) {
    TelemWheelV01& wh = tm.mWheel[w];
    const double base = 355.0 + (w < 2 ? 6.0 : 2.0) + 4.0 * std::sin(t * 0.2 + w);
    wh.mTemperature[0] = base + 3.0; wh.mTemperature[1] = base; wh.mTemperature[2] = base - 2.0;
    wh.mTireCarcassTemperature = base - 4.0;
    wh.mPressure = 170.0 + w * 1.5;
    wh.mWear = 1.0 - std::fmod(t, 3000.0) / 3000.0 * (w < 2 ? 0.30 : 0.24);
    wh.mBrakeTemp = 273.15 + 450.0 + (braking ? 250.0 : 0.0) + w * 10.0; // Kelvin, like the game
  }
}
