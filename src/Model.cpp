#include "Model.h"
#include <algorithm>
#include <cmath>
#include <cstring>

void ConsumptionTracker::Reset() { *this = ConsumptionTracker{}; }

void ConsumptionTracker::Update(int lapNumber, double amount, bool inPits) {
  if (lap_ < 0 || lapNumber < lap_) { // first sample or session restart
    lap_ = lapNumber;
    lapStart_ = last_ = amount;
    dirtyLap_ = true;
    return;
  }
  if (amount > last_ + 1e-3) dirtyLap_ = true; // refuelled / energy restored
  if (inPits) dirtyLap_ = true;

  if (lapNumber != lap_) {
    const double used = lapStart_ - amount;
    if (lapNumber == lap_ + 1 && !dirtyLap_ && used > 0.0) {
      history_[next_] = used;
      next_ = (next_ + 1) % kHistory;
      if (count_ < kHistory) ++count_;
    }
    lap_ = lapNumber;
    lapStart_ = amount;
    dirtyLap_ = inPits;
  }
  last_ = amount;
}

double ConsumptionTracker::PerLap() const {
  if (count_ == 0) return 0.0;
  double sum = 0.0;
  for (int i = 0; i < count_; ++i) sum += history_[i];
  return sum / count_;
}

void Model::Update(const Snapshot& s, double nowSeconds) {
  snap = &s;
  now = nowSeconds;
  connected = s.connected;
  player = nullptr;
  playerIdx = -1;
  telem = s.hasPlayerTelem ? &s.telem : nullptr;

  for (int i = 0; i < s.numVehicles; ++i) {
    if (s.vehicles[i].mIsPlayer) { playerIdx = i; break; }
  }
  if (playerIdx < 0 && telem) {
    for (int i = 0; i < s.numVehicles; ++i)
      if (s.vehicles[i].mID == telem->mID) { playerIdx = i; break; }
  }
  player = playerIdx >= 0 ? &s.vehicles[playerIdx] : nullptr;
  onTrack = connected && s.scoring.mInRealtime && player != nullptr;
  trackLength = s.scoring.mLapDist;

  if (!connected || !player) return;

  // New session, track or car (or time went backwards after a restart): forget usage history.
  // Two practice sessions in a row have the same session number, so the track / car matter too.
  const char* car = telem ? telem->mVehicleModel : "";
  const bool newPlace = strncmp(s.scoring.mTrackName, lastTrack_, sizeof(lastTrack_)) != 0 ||
                        strncmp(car, lastCar_, sizeof(lastCar_)) != 0;
  if (newPlace) {
    strncpy_s(lastTrack_, s.scoring.mTrackName, _TRUNCATE);
    strncpy_s(lastCar_, car, _TRUNCATE);
  }
  if (newPlace || s.scoring.mSession != lastSession_ || s.scoring.mCurrentET + 1.0 < lastET_) {
    fuel.Reset();
    energy.Reset();
    maxTimeLeft_ = 0;
    sawPreStart_ = false;
  }
  lastSession_ = s.scoring.mSession;
  lastET_ = s.scoring.mCurrentET;

  refLapTime = player->mBestLapTime > 0 ? player->mBestLapTime
             : player->mLastLapTime > 0 ? player->mLastLapTime
                                        : player->mEstimatedLapTime;

  lapsToGo = -1.0; // set by UpdateRace()

  if (telem) {
    fuel.Update(telem->mLapNumber, telem->mFuel, player->mInPits);
    if (telem->mVirtualEnergy > 0.f) energy.Update(telem->mLapNumber, Fraction(telem->mVirtualEnergy), player->mInPits);
  }
}

namespace {
// Lap time to expect from a car: last lap, else best, else LMU's estimate.
double PaceOf(const VehicleScoringInfoV01& v) {
  if (v.mLastLapTime > 0) return v.mLastLapTime;
  if (v.mBestLapTime > 0) return v.mBestLapTime;
  return v.mEstimatedLapTime > 0 ? v.mEstimatedLapTime : 0.0;
}
} // namespace

double Model::LiveLapDist(const VehicleScoringInfoV01& v) const {
  if (!snap || trackLength <= 0) return v.mLapDist;
  const double dt = telem ? std::clamp(telem->mElapsedTime - snap->scoring.mCurrentET, 0.0, 0.5) : 0.0;
  const double speed = std::sqrt(v.mLocalVel.x * v.mLocalVel.x + v.mLocalVel.z * v.mLocalVel.z);
  double d = v.mLapDist + (v.mInPits ? 0.0 : speed * dt);
  if (d >= trackLength) d -= trackLength;
  return d;
}

void Model::UpdateRace() {
  race = RaceInfo{};
  if (!connected || !player || trackLength <= 0) return;
  const Snapshot& s = *snap;
  const ScoringInfoV01& sc = s.scoring;
  RaceInfo& r = race;
  r.valid = true;
  r.race = sc.mSession >= 10 && sc.mSession <= 13;
  r.started = !r.race || sc.mGamePhase >= 5;
  r.lap = player->mTotalLaps + 1;
  r.timed = !(sc.mMaxLaps > 0 && sc.mMaxLaps < 100000);
  // LMU's own countdown (mEndET is invalid before a race starts and may include the formation lap).
  if (sc.mSessionTimeRemaining >= 0 && sc.mSessionTimeRemaining < 1e6) r.timeLeft = sc.mSessionTimeRemaining;
  else if (sc.mEndET > 0) r.timeLeft = std::fmax(0.0, sc.mEndET - sc.mCurrentET);
  if (r.timeLeft > maxTimeLeft_) maxTimeLeft_ = r.timeLeft;
  if (r.race && !r.started) sawPreStart_ = true;
  // The countdown before the start is the race length (mEndET also counts the formation lap).
  r.sessionLength = sawPreStart_ ? maxTimeLeft_ : 0.0;

  // Your pace: this race's laps, else your best this session, your all-time best here, LMU's estimate.
  using P = RaceInfo::Pace;
  if (r.race && player->mLastLapTime > 0) { r.pace = player->mLastLapTime; r.paceSource = P::Race; }
  else if (player->mBestLapTime > 0) { r.pace = player->mBestLapTime; r.paceSource = P::SessionBest; }
  else if (timing && timing->PlayerAllTime().lap > 0) { r.pace = timing->PlayerAllTime().lap; r.paceSource = P::AllTime; }
  else if (player->mEstimatedLapTime > 0) { r.pace = player->mEstimatedLapTime; r.paceSource = P::Estimate; }
  // A clean last lap that's much slower than your best (traffic, pit, yellow) isn't representative.
  if (r.paceSource == P::Race && player->mBestLapTime > 0 && r.pace > player->mBestLapTime * 1.07)
    r.pace = player->mBestLapTime * 1.02;

  const double myFrac = r.started ? player->mLapDist / trackLength : 0.0;
  if (!r.timed) {
    r.totalLaps = sc.mMaxLaps;
    r.lapsToGo = std::fmax(0.0, sc.mMaxLaps - player->mTotalLaps - myFrac);
  } else if (r.pace > 0 && r.timeLeft >= 0) {
    // Timed race: when the clock hits zero the overall leader finishes the lap they're on;
    // everyone else finishes the lap they're on when the leader crosses the line.
    double finishIn = r.timeLeft;
    if (r.race) {
      const VehicleScoringInfoV01* leader = player;
      for (int i = 0; i < s.numVehicles; ++i)
        if (s.vehicles[i].mPlace == 1) leader = &s.vehicles[i];
      double lp = leader == player ? r.pace : PaceOf(*leader);
      if (lp <= 0 || !r.started) lp = r.pace; // before the start there are no race laps yet
      const double lf = r.started ? leader->mLapDist / trackLength : 0.0;
      const double leaderToGo = std::ceil(r.timeLeft / lp + lf - 1e-6) - lf;
      finishIn = leaderToGo * lp;
    }
    r.lapsToGo = std::fmax(0.0, std::ceil(finishIn / r.pace + myFrac - 1e-6) - myFrac);
  }
  if (r.lapsToGo >= 0 && r.timed) r.totalLaps = (r.started ? player->mTotalLaps + myFrac : 0.0) + r.lapsToGo;
  lapsToGo = r.lapsToGo;

  if (!telem) return;
  r.fuel = telem->mFuel;
  r.fuelCap = telem->mFuelCapacity;
  r.usesEnergy = telem->mVirtualEnergy > 0.f;
  r.energy = r.usesEnergy ? Fraction(telem->mVirtualEnergy) : 0.0;
  r.fuelPerLap = fuel.PerLap();
  r.energyPerLap = energy.PerLap();
  r.perLapMeasured = r.fuelPerLap > 0 || r.energyPerLap > 0;
  if (timing) {
    if (r.fuelPerLap <= 0) r.fuelPerLap = timing->SavedFuelPerLap();
    if (r.energyPerLap <= 0) r.energyPerLap = timing->SavedEnergyPerLap();
  }
  if (rest && rest->valid) { // LMU's own estimate from the garage screen
    if (r.fuelPerLap <= 0) r.fuelPerLap = rest->garageFuelPerLap;
    if (r.energyPerLap <= 0) r.energyPerLap = rest->garageEnergyPerLap;
  }

  // Laps the tank lasts, now and when full; the limiting quantity wins.
  double inTank = -1, onFull = -1;
  if (r.fuelPerLap > 0) {
    inTank = r.fuel / r.fuelPerLap;
    if (r.fuelCap > 0) onFull = r.fuelCap / r.fuelPerLap;
  }
  if (r.usesEnergy && r.energyPerLap > 0) {
    const double e = r.energy / r.energyPerLap, ef = 1.0 / r.energyPerLap;
    if (inTank < 0 || e < inTank) { inTank = e; r.energyLimited = true; }
    if (onFull < 0 || ef < onFull) onFull = ef;
  }
  r.lapsInTank = inTank;
  r.lapsOnFull = onFull;
  if (onFull <= 0 || r.lapsToGo < 0) return;

  r.stopKnown = true;
  if (!r.started) {
    // Before the start: the whole race against a full tank; start with race laps + 1 spare.
    r.stopRequired = r.totalLaps > onFull;
    r.stopsLeft = r.stopRequired ? static_cast<int>(std::ceil(r.totalLaps / onFull - 1e-6)) - 1 : 0;
    if (r.fuelPerLap > 0) r.fillFuel = std::fmin(r.fuelCap > 0 ? r.fuelCap : 1e9, (r.totalLaps + 1) * r.fuelPerLap);
    if (r.usesEnergy && r.energyPerLap > 0) r.fillEnergy = std::fmin(1.0, (r.totalLaps + 1) * r.energyPerLap);
    r.spareLaps = r.stopRequired ? 0.0 : onFull - r.totalLaps;
    return;
  }
  const double shortBy = r.lapsToGo - inTank;
  r.stopsLeft = shortBy > 0 ? static_cast<int>(std::ceil(shortBy / onFull - 1e-6)) : 0;
  r.stopRequired = r.stopsLeft > 0;
  r.spareLaps = -shortBy;
  if (!r.stopRequired) return;
  // Each stop adds an equal share of what's missing (+1 lap spare), never more than fits.
  const double perStop = (shortBy + 1.0) / r.stopsLeft;
  if (r.fuelPerLap > 0) r.addFuel = std::fmin(perStop * r.fuelPerLap, std::fmax(0.0, r.fuelCap - r.fuel));
  if (r.usesEnergy && r.energyPerLap > 0) r.addEnergy = std::fmin(perStop * r.energyPerLap, std::fmax(0.0, 1.0 - r.energy));
  // Latest: the lap the tank runs dry on. Earliest: once the remaining stops can cover the rest.
  r.windowClose = r.lap + static_cast<int>(std::floor(inTank + myFrac)) - 1;
  const double coverable = r.stopsLeft * onFull;
  r.windowOpen = std::max(r.lap, static_cast<int>(std::ceil(r.lap - 1 + myFrac + r.lapsToGo - coverable - 1e-6)));
  if (r.windowClose < r.lap) r.windowClose = r.lap;
  if (r.windowOpen > r.windowClose) r.windowOpen = r.windowClose;
}
