#include "Model.h"
#include <algorithm>
#include <cmath>
#include <cstring>

void ConsumptionTracker::Reset() { *this = ConsumptionTracker{}; }

void ConsumptionTracker::Update(int lapNumber, double amount, bool inPits, double lapFrac) {
  if (lap_ < 0 || lapNumber < lap_) { // first sample or session restart
    lap_ = lapNumber;
    lapStart_ = last_ = amount;
    dirtyLap_ = true;
    partial_ = true;
    curTop_ = -1;
    return;
  }
  if (amount > last_ + 1e-3) dirtyLap_ = true; // refuelled / energy restored
  if (inPits) dirtyLap_ = true;

  if (lapNumber != lap_) {
    const double used = lapStart_ - amount;
    const bool clean = lapNumber == lap_ + 1 && !dirtyLap_ && used > 0.0;
    if (clean) {
      history_[next_] = used;
      next_ = (next_ + 1) % kHistory;
      if (count_ < kHistory) ++count_;
    }
    ++closed_;
    lastUsed_ = used;
    lastClean_ = clean;
    lastPartial_ = partial_ || lapNumber != lap_ + 1;
    partial_ = false;
    // A clean lap recorded (almost) all the way round becomes the same-point reference.
    if (clean && curTop_ >= kPoints - 5) {
      while (curTop_ < kPoints) cur_[++curTop_] = static_cast<float>(used);
      std::memcpy(ref_, cur_, sizeof(ref_));
      refTop_ = kPoints;
    }
    lap_ = lapNumber;
    lapStart_ = amount;
    dirtyLap_ = inPits;
    curTop_ = 0;
    cur_[0] = 0.f;
    wrapped_ = false;
  }
  last_ = amount;

  // Use so far at every 1 % of the lap. Scoring's lap distance wraps a moment after telemetry's
  // lap counter moves on: ignore the old lap's end until it does.
  if (lapFrac < 0.5) wrapped_ = true;
  if (wrapped_ && curTop_ >= 0) {
    const int k = std::clamp(static_cast<int>(lapFrac * kPoints), 0, kPoints);
    const float used = static_cast<float>(lapStart_ - amount);
    while (curTop_ < k) cur_[++curTop_] = used;
  }
}

bool ConsumptionTracker::VsLastLap(double lapFrac, double& delta) const {
  if (refTop_ < kPoints || dirtyLap_ || !wrapped_ || curTop_ < 3) return false; // too early in the lap to say
  const double x = std::clamp(lapFrac, 0.0, 1.0) * kPoints;
  const int i = std::min(static_cast<int>(x), kPoints - 1);
  delta = CurrentLapUsed() - (ref_[i] + (ref_[i + 1] - ref_[i]) * (x - i));
  return true;
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
    wear.Reset();
    lapCount = 0;
    seenClosed_ = 0;
    lapInPits_ = false;
    closedAt_ = timeAt_ = -100;
    maxTimeLeft_ = 0;
    sawPreStart_ = false;
  }
  lastSession_ = s.scoring.mSession;
  lastET_ = s.scoring.mCurrentET;

  refLapTime = player->mBestLapTime > 0 ? player->mBestLapTime
             : player->mLastLapTime > 0 ? player->mLastLapTime
                                        : player->mEstimatedLapTime;

  lapsToGo = -1.0; // set by UpdateRace()

  lapFrac = trackLength > 0 ? std::clamp(LiveLapDist(*player) / trackLength, 0.0, 1.0) : 0.0;
  if (telem) {
    const long lap = telem->mLapNumber;
    const bool pits = player->mInPits;
    fuel.Update(lap, telem->mFuel, pits, lapFrac);
    if (telem->mVirtualEnergy > 0.f) energy.Update(lap, Fraction(telem->mVirtualEnergy), pits, lapFrac);
    double tread = 0;
    for (const TelemWheelV01& w : telem->mWheel) tread += w.mWear;
    wear.Update(lap, tread / 4.0, pits, lapFrac);
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
  UpdateRaceInfo();
  UpdatePlan();
  UpdateLapHistory();
}

// Lap history: the lap's fuel / energy / tyre use is known the moment telemetry's lap counter moves
// on; its time comes with the next scoring update (or the other way round). Pair them up.
void Model::UpdateLapHistory() {
  if (!connected || !player || !timing) return;
  if (fuel.Closed() != seenClosed_) {
    seenClosed_ = fuel.Closed();
    closedAt_ = now;
    closedLap_ = LapRecord{};
    closedLap_.pit = lapInPits_;
    lapInPits_ = false;
    closedLap_.hasUse = !fuel.LastLapPartial();
    closedLap_.fuel = fuel.LastLapUsed();
    closedLap_.wear = wear.LastLapUsed();
    closedLap_.hasEnergy = race.usesEnergy && energy.Closed() > 0 && !energy.LastLapPartial();
    closedLap_.energy = energy.LastLapUsed();
  }
  if (player->mInPits) lapInPits_ = true;
  if (timing->PlayerLapSerial() != seenLapSerial_) {
    seenLapSerial_ = timing->PlayerLapSerial();
    timeAt_ = now;
    timedLap_ = LapRecord{};
    timedLap_.lap = player->mTotalLaps;
    timedLap_.time = timing->PlayerLast().lap;
    timedLap_.valid = timing->PlayerLastValid();
  }
  if (timeAt_ < 0) return;
  const bool paired = std::fabs(closedAt_ - timeAt_) < 3.0;
  if (!paired && now - timeAt_ < 3.0) return; // wait a little for the use figures
  LapRecord r = timedLap_;
  if (paired) {
    r.pit = closedLap_.pit;
    r.hasUse = closedLap_.hasUse;
    r.hasEnergy = closedLap_.hasEnergy;
    r.fuel = closedLap_.fuel;
    r.energy = closedLap_.energy;
    r.wear = closedLap_.wear;
  }
  for (int i = std::min(lapCount, kLapHistory - 1); i > 0; --i) laps[i] = laps[i - 1];
  laps[0] = r;
  lapCount = std::min(lapCount + 1, kLapHistory);
  timeAt_ = closedAt_ = -100;
}

// Practice / qualifying: the strategy for the race set in Settings, from this session's pace and use.
void Model::UpdatePlan() {
  plan = RacePlan{};
  const RaceInfo& r = race;
  if (!planCfg.enabled || !r.valid || r.race) return;
  RacePlan& p = plan;
  p.valid = true;
  p.byLaps = planCfg.byLaps;
  p.minutes = planCfg.minutes;
  p.pace = r.pace;
  p.usesEnergy = r.usesEnergy;
  p.fuelPerLap = r.fuelPerLap;
  p.energyPerLap = r.usesEnergy ? r.energyPerLap : 0.0;
  p.fuelCap = r.fuelCap;
  p.lapsOnFull = r.lapsOnFull;
  p.energyLimited = r.usesEnergy && r.energyPerLap > 0 && (r.fuelPerLap <= 0 || r.fuelCap <= 0 ||
                                                           1.0 / r.energyPerLap < r.fuelCap / r.fuelPerLap);
  // Timed: the leader finishes the lap they're on when the clock runs out; at your pace that's
  // the next whole lap.
  if (p.byLaps) p.totalLaps = planCfg.laps;
  else if (p.pace > 0) p.totalLaps = std::ceil(planCfg.minutes * 60.0 / p.pace - 1e-6);
  if (p.totalLaps <= 0 || p.lapsOnFull <= 0) return;
  p.ready = true;

  // Start full when a stop is needed; each stop then adds an equal share of the rest (+1 lap spare).
  const double need = p.totalLaps + 1.0;
  p.stops = p.totalLaps > p.lapsOnFull ? static_cast<int>(std::ceil(p.totalLaps / p.lapsOnFull - 1e-6)) - 1 : 0;
  if (p.stops == 0) {
    p.spareLaps = p.lapsOnFull - p.totalLaps;
    if (p.fuelPerLap > 0) p.fillFuel = std::fmin(p.fuelCap > 0 ? p.fuelCap : 1e9, need * p.fuelPerLap);
    if (p.energyPerLap > 0) p.fillEnergy = std::fmin(1.0, need * p.energyPerLap);
    return;
  }
  p.fillFuel = p.fuelCap;
  p.fillEnergy = p.energyPerLap > 0 ? 1.0 : 0.0;
  const double perStop = (need - p.lapsOnFull) / p.stops; // laps each stop has to add
  if (p.fuelPerLap > 0) p.addFuel = p.fuelCap > 0 ? std::fmin(p.fuelCap, perStop * p.fuelPerLap) : perStop * p.fuelPerLap;
  if (p.energyPerLap > 0) p.addEnergy = std::fmin(1.0, perStop * p.energyPerLap);
  p.stopLap = std::max(1, static_cast<int>(std::floor(p.lapsOnFull)));
}

void Model::UpdateRaceInfo() {
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
