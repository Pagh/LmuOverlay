#include "Timing.h"
#include "Ini.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

std::string FixedString(const char* s, size_t n) { return std::string(s, strnlen(s, n)); }

std::wstring SafeFileName(std::string s) {
  for (char& c : s)
    if (strchr("\\/:*?\"<>|", c) || static_cast<unsigned char>(c) < 32) c = '_';
  return Widen(s);
}

} // namespace

const wchar_t* DeltaRefLabel(DeltaRef r) {
  switch (r) {
    case DeltaRef::LmuBest: return L"PB";
    case DeltaRef::SessionBest: return L"SESSION";
    case DeltaRef::AllTime: return L"ALL-TIME";
    case DeltaRef::Lobby: return L"LOBBY";
    default: return L"";
  }
}

void SectorSet::TakeBest(const SectorSet& o) {
  for (int i = 0; i < 3; ++i)
    if (o.s[i] > 0 && (s[i] <= 0 || o.s[i] < s[i])) s[i] = o.s[i];
  if (o.lap > 0 && (lap <= 0 || o.lap < lap)) lap = o.lap;
}

bool SectorsFromCumulative(double s1, double s12, double lap, SectorSet& out) {
  out = SectorSet{};
  if (!(s1 > 0 && s12 > s1 && lap > s12)) return false;
  out.s[0] = s1;
  out.s[1] = s12 - s1;
  out.s[2] = lap - s12;
  out.lap = lap;
  return true;
}

void Timing::Reset() {
  vehicles_.clear();
  classes_.clear();
  playerLast_ = playerSession_ = SectorSet{};
  playerLastValid_ = true;
  lapNumber_ = -1;
  curLapInvalid_ = prevLapInvalid_ = false;
  lapChangeTime_ = -100;
  damaged_ = false;
  dentSum_ = 0;
  preSum_ = postSum_ = 0;
  preCount_ = postCount_ = 0;
  paceBefore_ = paceSince_ = 0;
  sessionTrace_ = lobbyTrace_ = LapTrace{};
  lobbyRef_ = SectorSet{};
  lobbyRefHolder_.clear();
  lobbyRefKey_ = -100;
  outLap_ = outBaseSet_ = false;
  curValid_ = false;
}

double Timing::TimeAlong(double a, double b) const {
  const LapTrace* tr = nullptr;
  for (const LapTrace* t : {&sessionTrace_, &allTimeTrace_, &lobbyTrace_})
    if (t->Valid() && std::fabs(t->length - curTrackLength_) <= 1.0) { tr = t; break; }
  if (!tr) return -1;
  const double ta = tr->At(a), tb = tr->At(b);
  return b >= a ? tb - ta : (tr->lap - ta) + tb;
}

double Timing::ReferenceLap(DeltaRef ref) const {
  switch (ref) {
    case DeltaRef::LmuBest: return playerBest_ > 0 ? playerBest_ : sessionTrace_.lap;
    case DeltaRef::SessionBest: return sessionTrace_.lap;
    case DeltaRef::AllTime: return allTimeTrace_.lap;
    case DeltaRef::Lobby: return lobbyRef_.lap;
    default: return 0;
  }
}

bool Timing::TraceDelta(const LapTrace& tr, double& delta) const {
  if (!tr.Valid() || std::fabs(tr.length - curTrackLength_) > 1.0) return false;
  delta = curT_ - tr.At(curD_);
  return true;
}

// The lobby's fastest lap usually wasn't recorded (driven before the overlay started, or by a car
// we only see at scoring rate), so its shape comes from the closest lap we do have: that exact lap
// if we recorded it, else your best trace. The shape is then stretched sector by sector to the
// lobby lap's official sector times.
const LapTrace* Timing::LobbyShape() const {
  auto ok = [this](const LapTrace& tr) { return tr.Valid() && std::fabs(tr.length - curTrackLength_) <= 1.0; };
  if (ok(lobbyTrace_) && std::fabs(lobbyTrace_.lap - lobbyRef_.lap) < 0.002) return &lobbyTrace_;
  if (ok(sessionTrace_)) return &sessionTrace_;
  if (ok(allTimeTrace_)) return &allTimeTrace_;
  if (ok(lobbyTrace_)) return &lobbyTrace_;
  return nullptr;
}

double Timing::WarpAt(const LapTrace& shape, const SectorSet& ref, double d) const {
  const double tS = shape.At(d);
  if (std::fabs(shape.lap - ref.lap) < 0.002) return tS;
  const double b2 = map_.SectorStart(1), b3 = map_.SectorStart(2);
  if (ref.Complete() && b2 > 0 && b3 > b2 && b3 < shape.length) {
    const double t2 = shape.At(b2), t3 = shape.At(b3);
    if (t2 > 0 && t3 > t2 && shape.lap > t3) {
      if (d < b2) return tS * ref.s[0] / t2;
      if (d < b3) return ref.s[0] + (tS - t2) * ref.s[1] / (t3 - t2);
      return ref.s[0] + ref.s[1] + (tS - t3) * ref.s[2] / (shape.lap - t3);
    }
  }
  return tS * ref.lap / shape.lap; // sector times unknown: stretch the whole lap
}

bool Timing::RawDelta(DeltaRef ref, double& delta) const {
  if (!curValid_) return false;
  switch (ref) {
    case DeltaRef::LmuBest:
      // LMU's own delta, except where it's unreliable (out laps, invalidated laps, junk values):
      // then the same comparison from your recorded best lap.
      if (playerBest_ > 0 && std::fabs(lmuDelta_) < 60.0 && !curLapInvalid_ && !outLap_) {
        delta = lmuDelta_;
        return true;
      }
      return TraceDelta(sessionTrace_, delta);
    case DeltaRef::SessionBest: return TraceDelta(sessionTrace_, delta);
    case DeltaRef::AllTime: return TraceDelta(allTimeTrace_, delta);
    case DeltaRef::Lobby: {
      if (lobbyRef_.lap <= 0) return false;
      const LapTrace* shape = LobbyShape();
      if (!shape) return false;
      delta = curT_ - WarpAt(*shape, lobbyRef_, curD_);
      return true;
    }
    default: return false;
  }
}

bool Timing::Delta(DeltaRef ref, double& delta) const {
  double raw;
  if (!RawDelta(ref, raw)) return false;
  if (outLap_) {
    const int i = static_cast<int>(ref);
    if (!outBaseSet_ || !outBaseOk_[i]) return false;
    delta = raw - outBase_[i];
    return true;
  }
  delta = raw;
  return true;
}

void Timing::PairTrace(Vehicle& vt, const VehicleScoringInfoV01& v, bool isPlayer, bool playerClass, double et) {
  // A lap's trace (from the recorder) and its official time (from scoring) arrive separately.
  if (vt.pendingLap > 0 && vt.trace.Valid() && std::fabs(vt.trace.lap - vt.pendingLap) < 1.0 &&
      std::fabs(vt.traceET - vt.pendingET) < 10.0) {
    LapTrace tr = std::move(vt.trace);
    tr.lap = vt.pendingLap;
    if (vt.pendingValid) {
      if (isPlayer && (sessionTrace_.lap <= 0 || tr.lap <= sessionTrace_.lap)) sessionTrace_ = tr;
      // All-time reference: your fastest recorded trace (records saved before traces existed
      // may hold a faster lap time; the delta widget shows the reference lap's time).
      if (isPlayer && (allTimeTrace_.lap <= 0 || tr.lap <= allTimeTrace_.lap)) {
        allTimeTrace_ = tr;
        SaveRecords();
      }
      if (playerClass && (lobbyTrace_.lap <= 0 || tr.lap < lobbyTrace_.lap)) lobbyTrace_ = tr;
    }
    vt.trace = LapTrace{};
    vt.pendingLap = 0;
  }
  // Drop halves that never found their partner.
  if (vt.pendingLap > 0 && et - vt.pendingET > 10.0) vt.pendingLap = 0;
  if (vt.trace.Valid() && et - vt.traceET > 10.0) vt.trace = LapTrace{};
}

const Timing::Vehicle* Timing::Find(long id) const {
  const auto it = vehicles_.find(id);
  return it == vehicles_.end() ? nullptr : &it->second;
}

const Timing::ClassBest* Timing::Class(const char* cls) const {
  const auto it = classes_.find(FixedString(cls, 32));
  return it == classes_.end() ? nullptr : &it->second;
}

void Timing::Update(const Snapshot& s, int playerIdx) {
  if (!s.connected) return;
  const TelemInfoV01* t = s.hasPlayerTelem ? &s.telem : nullptr;

  // Player lap validity: LMU flags the lap while it's being driven; remember it per lap.
  if (t) {
    if (t->mLapNumber != lapNumber_) {
      prevLapInvalid_ = curLapInvalid_;
      curLapInvalid_ = false;
      lapNumber_ = t->mLapNumber;
      lapChangeTime_ = t->mElapsedTime;
      outLap_ = playerIdx >= 0 && s.vehicles[playerIdx].mInPits;
      outBaseSet_ = false;
    }
    curLapInvalid_ |= t->mLapInvalidated;

    int dents = t->mDetached ? 1 : 0;
    for (unsigned char d : t->mDentSeverity) dents += d;
    if (dents > dentSum_) { damaged_ = true; lapHadDamageChange_ = true; }
    if (dents == 0 && damaged_) { damaged_ = false; postSum_ = 0; postCount_ = 0; paceSince_ = 0; } // repaired
    dentSum_ = dents;
  }

  if (playerIdx >= 0) {
    const VehicleScoringInfoV01& me = s.vehicles[playerIdx];
    if (me.mInPits) { outLap_ = true; outBaseSet_ = false; } // leaving the pits: this is an out lap
    playerBest_ = me.mBestLapTime;
  }
  lmuDelta_ = t ? t->mDeltaBest : 0.0;

  // Player trace at telemetry rate: scoring lap distance extrapolated with the car's speed.
  const double L = s.scoring.mLapDist;
  curValid_ = false;
  curTrackLength_ = L;
  const auto playerIt = playerIdx >= 0 ? vehicles_.find(s.vehicles[playerIdx].mID) : vehicles_.end();
  if (t && playerIt != vehicles_.end() && L > 100) {
    const VehicleScoringInfoV01& me = s.vehicles[playerIdx];
    Vehicle& vt = playerIt->second;
    const double speed = std::sqrt(t->mLocalVel.x * t->mLocalVel.x + t->mLocalVel.y * t->mLocalVel.y +
                                   t->mLocalVel.z * t->mLocalVel.z);
    double d = me.mLapDist + speed * std::clamp(t->mElapsedTime - s.scoring.mCurrentET, 0.0, 0.5);
    if (d >= L) d -= L;
    const double lapT = t->mElapsedTime - t->mLapStartET;
    if (t->mLapStartET > 0 && vt.recorder.Sample(d, lapT, t->mLapStartET, L)) {
      vt.trace = vt.recorder.Finished();
      vt.traceET = t->mElapsedTime;
    }
    // Out laps often start mid-lap (garage), so the recorder may not be capturing: use the raw distance.
    curD_ = vt.recorder.Recording() ? vt.recorder.Distance() : (d > L * 0.5 && lapT < 20.0 ? 0.0 : d);
    curT_ = lapT;
    curValid_ = (vt.recorder.Recording() || outLap_) && lapT > 0 && !me.mInPits;
    // Out lap: the delta starts from zero where sector 2 starts.
    if (outLap_ && !outBaseSet_ && curValid_ && curD_ < L * 0.98) {
      const double s2 = map_.SectorStart(1);
      const bool inS2 = s2 > 0 ? curD_ >= s2 : (me.mSector == 2 || me.mSector == 0);
      if (inS2) {
        outBaseSet_ = true; // RawDelta ignores outLap_ except for LMU's own delta
        for (int r = 0; r < static_cast<int>(DeltaRef::Count); ++r)
          outBaseOk_[r] = RawDelta(static_cast<DeltaRef>(r), outBase_[r]);
      }
    }
  }

  // The rest only changes with scoring updates.
  const double et = s.scoring.mCurrentET;
  if (et == lastET_) return;
  if (s.scoring.mSession != session_ || et < lastET_ - 1.0) Reset();
  session_ = s.scoring.mSession;
  lastET_ = et;

  const std::string track = FixedString(s.scoring.mTrackName, sizeof(s.scoring.mTrackName));
  std::string car = t ? FixedString(t->mVehicleModel, sizeof(t->mVehicleModel)) : std::string();
  if (car.empty() && playerIdx >= 0) car = FixedString(s.vehicles[playerIdx].mVehicleClass, 32);
  if (!track.empty() && !car.empty() && (track != track_ || car != car_)) LoadRecords(track, car);

  // Track outline and sector boundaries from every car on track.
  if (!track.empty() && L > 100 && (track != mapTrack_ || std::fabs(map_.Length() - L) > 1.0)) LoadMap(track, L);
  for (int i = 0; i < s.numVehicles; ++i) {
    const VehicleScoringInfoV01& v = s.vehicles[i];
    if (v.mInPits || v.mInGarageStall) continue;
    const double speed2 = v.mLocalVel.x * v.mLocalVel.x + v.mLocalVel.z * v.mLocalVel.z;
    if (speed2 < 15.0 * 15.0) continue; // stopped / spinning cars are off the line
    if (!map_.Complete()) map_.Add(v.mLapDist, v.mPos.x, v.mPos.z);
    map_.NoteSector(v.mSector, v.mLapDist);
  }
  if (map_.Complete() && map_.Version() != mapSavedVersion_ && et - mapSaveET_ > 30.0) SaveMap();
  if (et < mapSaveET_) mapSaveET_ = -1e9;

  const char* myClass = playerIdx >= 0 ? s.vehicles[playerIdx].mVehicleClass : nullptr;
  bool anyLap = false;
  for (int i = 0; i < s.numVehicles; ++i) {
    const VehicleScoringInfoV01& v = s.vehicles[i];
    auto [it, isNew] = vehicles_.try_emplace(v.mID);
    Vehicle& vt = it->second;
    const bool isPlayer = i == playerIdx;
    const bool playerClass = myClass && strncmp(v.mVehicleClass, myClass, sizeof(v.mVehicleClass)) == 0;
    // Other cars' traces at scoring rate (the player's is recorded above from telemetry).
    if (!isPlayer && v.mLapStartET > 0 && vt.recorder.Sample(v.mLapDist, et - v.mLapStartET, v.mLapStartET, L)) {
      vt.trace = vt.recorder.Finished();
      vt.traceET = et;
    }
    if (isNew) {
      // Joined mid-session: start from what LMU already knows about this car.
      SectorSet bestLap;
      if (SectorsFromCumulative(v.mBestLapSector1, v.mBestLapSector2, v.mBestLapTime, bestLap)) vt.best = bestLap;
      if (v.mBestSector1 > 0 && (vt.best.s[0] <= 0 || v.mBestSector1 < vt.best.s[0])) vt.best.s[0] = v.mBestSector1;
      SectorsFromCumulative(v.mLastSector1, v.mLastSector2, v.mLastLapTime, vt.last);
      vt.lastLapSeen = v.mLastLapTime;
      if (i == playerIdx) { playerSession_ = vt.best; playerLast_ = vt.last; }
      anyLap = true;
      continue;
    }
    if (v.mLastLapTime == vt.lastLapSeen || v.mLastLapTime <= 0) {
      vt.lastLapSeen = v.mLastLapTime;
      PairTrace(vt, v, isPlayer, playerClass, et);
      continue;
    }
    vt.lastLapSeen = v.mLastLapTime;

    SectorSet lap;
    const bool complete = SectorsFromCumulative(v.mLastSector1, v.mLastSector2, v.mLastLapTime, lap);
    if (!complete) lap.lap = v.mLastLapTime;
    vt.last = lap;
    // Scoring and telemetry don't update together: if the telemetry lap counter just moved on,
    // the finished lap's flag is in prevLapInvalid_, otherwise it's still the current one.
    bool valid = true;
    if (i == playerIdx && t) valid = !(t->mElapsedTime - lapChangeTime_ < 3.0 ? prevLapInvalid_ : curLapInvalid_);
    if (valid) vt.best.TakeBest(lap);
    if (i == playerIdx) OnPlayerLap(lap, valid, damaged_);
    vt.pendingLap = v.mLastLapTime;
    vt.pendingValid = valid;
    vt.pendingET = et;
    PairTrace(vt, v, isPlayer, playerClass, et);
    anyLap = true;
  }

  if (anyLap) {
    // Rebuild class bests (a few dozen cars, only when a lap completed).
    classes_.clear();
    for (int i = 0; i < s.numVehicles; ++i) {
      const VehicleScoringInfoV01& v = s.vehicles[i];
      const Vehicle& vt = vehicles_[v.mID];
      ClassBest& cb = classes_[FixedString(v.mVehicleClass, sizeof(v.mVehicleClass))];
      const std::string name = FixedString(v.mDriverName, sizeof(v.mDriverName));
      for (int k = 0; k < 3; ++k)
        if (vt.best.s[k] > 0 && (cb.best.s[k] <= 0 || vt.best.s[k] < cb.best.s[k])) { cb.best.s[k] = vt.best.s[k]; cb.holder[k] = name; }
      if (vt.best.lap > 0 && (cb.best.lap <= 0 || vt.best.lap < cb.best.lap)) { cb.best.lap = vt.best.lap; cb.holder[3] = name; }
    }
  }

  // Lobby reference: the fastest lap in your class as LMU reports it (includes laps driven before
  // the overlay started). Taken at the start of each of your laps so it can't change under you.
  if (myClass) {
    SectorSet best;
    std::string holder;
    for (int i = 0; i < s.numVehicles; ++i) {
      const VehicleScoringInfoV01& v = s.vehicles[i];
      if (v.mBestLapTime <= 0 || strncmp(v.mVehicleClass, myClass, sizeof(v.mVehicleClass)) != 0) continue;
      if (best.lap > 0 && v.mBestLapTime >= best.lap) continue;
      if (!SectorsFromCumulative(v.mBestLapSector1, v.mBestLapSector2, v.mBestLapTime, best)) {
        best = SectorSet{};
        best.lap = v.mBestLapTime;
      }
      holder = FixedString(v.mDriverName, sizeof(v.mDriverName));
    }
    const VehicleScoringInfoV01& me = s.vehicles[playerIdx];
    if (me.mTotalLaps != lobbyRefKey_ || lobbyRef_.lap <= 0 || me.mInPits) {
      lobbyRef_ = best;
      lobbyRefHolder_ = holder;
      lobbyRefKey_ = me.mTotalLaps;
    }
  }
}

void Timing::OnPlayerLap(const SectorSet& lap, bool valid, bool damaged) {
  playerLast_ = lap;
  playerLastValid_ = valid;
  if (!valid) { lapHadDamageChange_ = false; return; }
  playerSession_.TakeBest(lap);

  SectorSet merged = allTime_;
  merged.TakeBest(lap);
  if (memcmp(&merged, &allTime_, sizeof(merged)) != 0) {
    allTime_ = merged;
    SaveRecords();
  }

  // Pace before / since damage (the lap on which the damage happened counts for neither).
  if (lap.lap > 0) {
    if (!damaged) {
      // Rolling average of recent clean laps (weights older laps less).
      preSum_ = preCount_ < 3 ? preSum_ + lap.lap : preSum_ * 2.0 / 3.0 + lap.lap;
      preCount_ = std::min(preCount_ + 1, 3);
      paceBefore_ = preSum_ / preCount_;
    } else if (!lapHadDamageChange_) {
      postSum_ += lap.lap;
      ++postCount_;
      paceSince_ = postSum_ / postCount_;
    }
  }
  lapHadDamageChange_ = false;
}

void Timing::LoadRecords(const std::string& track, const std::string& car) {
  track_ = track;
  car_ = car;
  allTime_ = SectorSet{};
  allTimeTrace_ = LapTrace{};
  fuelPerLap_ = energyPerLap_ = 0;
  if (recordsDir_.empty()) return;
  IniDoc doc;
  if (!doc.Load(recordsDir_ + L"\\" + SafeFileName(track + " - " + car) + L".ini")) return;
  allTime_.s[0] = doc.GetFloat("best", "s1", 0);
  allTime_.s[1] = doc.GetFloat("best", "s2", 0);
  allTime_.s[2] = doc.GetFloat("best", "s3", 0);
  allTime_.lap = doc.GetFloat("best", "lap", 0);
  fuelPerLap_ = doc.GetFloat("usage", "fuel_per_lap", 0);
  energyPerLap_ = doc.GetFloat("usage", "energy_per_lap", 0);

  // Trace of the all-time best lap, for the live delta (elapsed ms every 5 m).
  allTimeTrace_ = LapTrace{};
  const std::string data = doc.Get("trace", "ms", "");
  const double length = doc.GetFloat("trace", "length", 0);
  if (!data.empty() && length > 0 && doc.GetFloat("trace", "bin", 0) == kTraceBin) {
    LapTrace tr;
    const char* p = data.c_str();
    while (*p) {
      char* end = nullptr;
      const long v = std::strtol(p, &end, 10);
      if (end == p) break;
      tr.t.push_back(static_cast<float>(v / 1000.0));
      p = *end == ',' ? end + 1 : end;
    }
    tr.length = length;
    tr.lap = doc.GetFloat("trace", "lap", 0);
    if (tr.t.size() == static_cast<size_t>(length / kTraceBin) + 1) allTimeTrace_ = std::move(tr);
  }
}

void Timing::SaveRecords() const {
  if (recordsDir_.empty() || track_.empty() || car_.empty()) return;
  CreateDirectoryW(recordsDir_.c_str(), nullptr);
  IniDoc doc;
  doc.header = "All-time personal bests (valid laps only). Delete this file to reset.";
  doc.Set("info", "track", track_);
  doc.Set("info", "car", car_);
  char buf[32];
  auto put = [&](const char* key, double v) {
    snprintf(buf, sizeof(buf), "%.3f", v);
    doc.Set("best", key, buf);
  };
  put("s1", allTime_.s[0]);
  put("s2", allTime_.s[1]);
  put("s3", allTime_.s[2]);
  put("lap", allTime_.lap);
  if (fuelPerLap_ > 0 || energyPerLap_ > 0) {
    snprintf(buf, sizeof(buf), "%.4f", fuelPerLap_);
    doc.Set("usage", "fuel_per_lap", buf);
    snprintf(buf, sizeof(buf), "%.5f", energyPerLap_);
    doc.Set("usage", "energy_per_lap", buf);
  }
  if (allTimeTrace_.Valid()) {
    std::string data;
    data.reserve(allTimeTrace_.t.size() * 7);
    for (float v : allTimeTrace_.t) {
      data += std::to_string(static_cast<long>(std::lround(v * 1000.0)));
      data += ',';
    }
    data.pop_back();
    snprintf(buf, sizeof(buf), "%.1f", kTraceBin);
    doc.Set("trace", "bin", buf);
    snprintf(buf, sizeof(buf), "%.3f", allTimeTrace_.length);
    doc.Set("trace", "length", buf);
    snprintf(buf, sizeof(buf), "%.3f", allTimeTrace_.lap);
    doc.Set("trace", "lap", buf);
    doc.Set("trace", "ms", std::move(data));
  }
  doc.Save(recordsDir_ + L"\\" + SafeFileName(track_ + " - " + car_) + L".ini");
}

void Timing::RecordConsumption(double fuelPerLap, double energyPerLap) {
  auto changed = [](double stored, double v) { return v > 0 && std::fabs(v - stored) > stored * 0.01; };
  if (!changed(fuelPerLap_, fuelPerLap) && !changed(energyPerLap_, energyPerLap)) return;
  if (fuelPerLap > 0) fuelPerLap_ = fuelPerLap;
  if (energyPerLap > 0) energyPerLap_ = energyPerLap;
  SaveRecords();
}

void Timing::LoadMap(const std::string& track, double length) {
  mapTrack_ = track;
  map_.Reset(length);
  mapSavedVersion_ = map_.Version();
  if (recordsDir_.empty()) return;
  IniDoc doc;
  if (doc.Load(recordsDir_ + L"\\" + SafeFileName(track) + L".map.ini") && map_.Load(doc) &&
      std::fabs(map_.Length() - length) <= 1.0) {
    // Maps from older versions get smoothed on load: save them once in the new form.
    mapSavedVersion_ = doc.GetInt("map", "smoothed", 0) ? map_.Version() : 0;
    return;
  }
  map_.Reset(length); // missing, or the layout changed
  mapSavedVersion_ = map_.Version();
}

void Timing::SaveMap() {
  mapSavedVersion_ = map_.Version();
  mapSaveET_ = lastET_;
  if (recordsDir_.empty() || mapTrack_.empty()) return;
  CreateDirectoryW(recordsDir_.c_str(), nullptr);
  IniDoc doc;
  doc.header = "Track outline learned from car positions (10 m steps). Delete to re-learn.";
  doc.Set("info", "track", mapTrack_);
  map_.Save(doc);
  doc.Save(recordsDir_ + L"\\" + SafeFileName(mapTrack_) + L".map.ini");
}
