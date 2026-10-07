#include "LapTrace.h"
#include <algorithm>
#include <cmath>

double LapTrace::At(double d) const {
  if (!Valid()) return 0;
  d = std::clamp(d, 0.0, length);
  const double f = d / kTraceBin;
  const size_t i = static_cast<size_t>(f);
  if (i + 1 < t.size()) return t[i] + (t[i + 1] - t[i]) * (f - static_cast<double>(i));
  // Between the last bin and the finish line.
  const double lastD = static_cast<double>(t.size() - 1) * kTraceBin;
  const double span = length - lastD;
  return span > 0 ? t.back() + (lap - t.back()) * ((d - lastD) / span) : lap;
}

void LapRecorder::FillTo(double d, double t) {
  if (d <= lastD_) return;
  const int first = static_cast<int>(lastD_ / kTraceBin) + 1;
  const int last = std::min(static_cast<int>(d / kTraceBin), static_cast<int>(cur_.size()) - 1);
  for (int b = first; b <= last; ++b) {
    const double bd = b * kTraceBin;
    cur_[b] = static_cast<float>(lastT_ + (t - lastT_) * (bd - lastD_) / (d - lastD_));
  }
  lastD_ = d;
  lastT_ = t;
}

bool LapRecorder::Sample(double d, double t, double lapStartET, double L) {
  if (L <= 100 || lapStartET < 0) return false;

  // Lap distance and the lap start don't always flip on the same update: just after the
  // line the distance may still read "end of the previous lap".
  if (d > L * 0.5 && t < 15.0 && (lapStartET != startET_ ? true : lastD_ < L * 0.25)) d -= L;

  if (lapStartET != startET_) {
    bool done = false;
    if (startET_ >= 0 && !partial_ && lastD_ > L * 0.9 && lapStartET > startET_) {
      const double lapTime = lapStartET - startET_;
      FillTo(L - 0.01, lapTime - 1e-3);
      finished_.t = cur_;
      finished_.lap = lapTime;
      finished_.length = L;
      done = std::none_of(cur_.begin(), cur_.end(), [](float v) { return v < 0; });
      if (!done) finished_ = LapTrace{};
    }
    startET_ = lapStartET;
    length_ = L;
    cur_.assign(static_cast<size_t>(L / kTraceBin) + 1, -1.f);
    cur_[0] = 0.f;
    lastD_ = 0;
    lastT_ = 0;
    partial_ = d > 150.0; // first sample far from the line: we missed the start of this lap
    if (!partial_ && d > 0) FillTo(d, t);
    return done;
  }

  if (partial_ || d < 0) return false;
  if (d < lastD_) {
    if (lastD_ - d > 200.0) partial_ = true; // reset to pits / teleported
    return false;                            // small backwards jitter
  }
  FillTo(d, t);
  return false;
}
