#include "StraightLine.h"
#include <algorithm>
#include <cmath>
#include <cstring>

void StraightLine::Reset() { *this = StraightLine{}; }

void StraightLine::Curve::Add(double kph, double accel) {
  const int b = static_cast<int>((kph - kMinKph) / kBinKph);
  if (b < 0 || b >= kBins) return;
  sum[b] += static_cast<float>(accel);
  if (++n[b] > 4000) { sum[b] *= 0.5f; n[b] /= 2; } // keep it recent (fuel load, tyres)
}

bool StraightLine::Curve::At(double kph, double& accel) const {
  // Bin centres; interpolate between the nearest measured bins on either side.
  const double x = (kph - kMinKph) / kBinKph - 0.5;
  const int i0 = static_cast<int>(std::floor(x));
  int lo = -1, hi = -1;
  for (int i = std::min(i0, kBins - 1); i >= std::max(0, i0 - 2); --i) if (n[i] >= kMinSamples) { lo = i; break; }
  for (int i = std::max(i0 + 1, 0); i <= std::min(kBins - 1, i0 + 3); ++i) if (n[i] >= kMinSamples) { hi = i; break; }
  auto mean = [this](int i) { return static_cast<double>(sum[i]) / n[i]; };
  if (lo >= 0 && hi >= 0) {
    const double f = std::clamp((x - lo) / (hi - lo), 0.0, 1.0);
    accel = mean(lo) + (mean(hi) - mean(lo)) * f;
    return true;
  }
  // Just past the last measured bin (top speed): only very close.
  const int one = lo >= 0 ? lo : hi;
  if (one < 0 || std::fabs(x - one) > 1.0) return false;
  accel = mean(one);
  return true;
}

// Drives `steps` x kStep metres flat out from v0 (m/s) with a curve's acceleration. Returns the time.
double StraightLine::Simulate(const Curve& c, double v0, int steps, double& vEnd, bool& ok) {
  double v = std::max(v0, 1.0), t = 0;
  ok = true;
  for (int i = 0; i < steps; ++i) {
    double a;
    if (!c.At(v * 3.6, a)) { ok = false; break; }
    const double vn = std::sqrt(std::max(v * v + 2.0 * a * kStep, 1.0));
    t += 2.0 * kStep / (v + vn);
    v = vn;
  }
  vEnd = v;
  return t;
}

void StraightLine::Sample(const TelemInfoV01& t, double lapDist, double trackLength, bool inPits, int damageSerial,
                          bool damaged) {
  const double et = t.mElapsedTime;
  if (et < lastET_ - 1.0) Reset();     // new session / restart
  else if (et <= lastET_) return;      // same sample
  lastET_ = et;

  // New damage: measure again from now. Repaired: nothing to compare.
  if (damageSerial != serial_) {
    if (serial_ >= 0 && damaged) since_.Clear();
    serial_ = damageSerial;
  }
  if (!damaged && wasDamaged_) since_.Clear();
  wasDamaged_ = damaged;

  // Flat out in a straight line, not just after a gear change (the shift dips the acceleration).
  if (t.mGear != gear_) { gear_ = t.mGear; shiftET_ = et; }
  const double v = std::sqrt(t.mLocalVel.x * t.mLocalVel.x + t.mLocalVel.y * t.mLocalVel.y + t.mLocalVel.z * t.mLocalVel.z);
  const bool full = t.mUnfilteredThrottle >= 0.97 && t.mUnfilteredBrake <= 0.02 && !inPits && !t.mSpeedLimiterActive;
  if (full && std::fabs(t.mUnfilteredSteering) <= 0.08 && et - shiftET_ > 0.4 && v * 3.6 >= kMinKph)
    (damaged ? since_ : before_).Add(v * 3.6, -t.mLocalAccel.z); // car frame: z points backwards

  // Lap profile.
  if (trackLength <= kStep * 10) return;
  const int steps = std::min(kMaxSteps, static_cast<int>(trackLength / kStep));
  if (t.mLapNumber != lap_) {
    if (lap_ >= 0 && lapClean_ && !damaged && lapSteps_ == steps) {
      int seen = 0;
      for (int k = 0; k < steps; ++k) seen += lapSeen_[k];
      if (seen >= steps * 0.95) {
        std::memcpy(refV_, lapV_, sizeof(float) * steps);
        std::memcpy(refFull_, lapFull_, sizeof(bool) * steps);
        refSteps_ = steps;
      }
    }
    lap_ = t.mLapNumber;
    lapClean_ = true;
    lapSteps_ = steps;
    std::memset(lapSeen_, 0, sizeof(lapSeen_));
  }
  if (inPits || damaged) lapClean_ = false;
  const int k = std::clamp(static_cast<int>(lapDist / kStep), 0, steps - 1);
  if (!lapSeen_[k]) {
    lapSeen_[k] = true;
    lapV_[k] = static_cast<float>(v);
    lapFull_[k] = full;
  } else if (!full) {
    lapFull_[k] = false;
  }
}

StraightLine::Result StraightLine::Compare() const {
  Result r;
  if (refSteps_ <= 0) return r;
  // Start from a step that isn't flat out, so the stretch across the start line stays in one piece.
  int start = -1;
  for (int k = 0; k < refSteps_; ++k) if (!refFull_[k]) { start = k; break; }
  if (start < 0) return r;
  int total = 0, covered = 0;
  double loss = 0, fastestEnd = 0;
  for (int i = 0; i < refSteps_;) {
    if (!refFull_[(start + i) % refSteps_]) { ++i; continue; }
    int len = 0;
    while (i + len < refSteps_ && refFull_[(start + i + len) % refSteps_]) ++len;
    if (len >= 5) { // 50 m or more
      total += len;
      const double v0 = refV_[(start + i) % refSteps_];
      double endBefore, endSince;
      bool ok1, ok2;
      const double t1 = Simulate(before_, v0, len, endBefore, ok1);
      const double t2 = Simulate(since_, v0, len, endSince, ok2);
      if (ok1 && ok2) {
        covered += len;
        loss += t2 - t1;
        if (endBefore > fastestEnd) {
          fastestEnd = endBefore;
          r.topSpeedLoss = (endBefore - endSince) * 3.6;
        }
      }
    }
    i += len;
  }
  if (total == 0) return r;
  r.coverage = static_cast<double>(covered) / total;
  r.valid = r.coverage >= 0.7;
  if (covered > 0) r.timeLoss = loss * total / covered; // the few uncovered stretches: same rate
  return r;
}
