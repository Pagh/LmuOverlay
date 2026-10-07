#include "TrackMap.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

void TrackMap::Reset(double length) {
  *this = TrackMap{};
  length_ = length;
  const size_t bins = length > 0 ? static_cast<size_t>(std::ceil(length / kBin)) : 0;
  x_.assign(bins, 0.f);
  z_.assign(bins, 0.f);
  n_.assign(bins, 0);
  ++version_;
}

bool TrackMap::Add(double d, double x, double z) {
  if (complete_ || n_.empty() || d < 0 || d >= length_) return false;
  const size_t b = std::min(n_.size() - 1, static_cast<size_t>(d / kBin));
  // Running mean of the cars that passed here: settles on the racing line, ignores one-off moments.
  if (n_[b] < 30) {
    ++n_[b];
    x_[b] += static_cast<float>((x - x_[b]) / n_[b]);
    z_[b] += static_cast<float>((z - z_[b]) / n_[b]);
    if (n_[b] == 1) ++filled_;
  }
  if (filled_ < static_cast<int>(n_.size() * 0.97)) return false;
  // Done once every bin has a few samples (or the stragglers are isolated gaps).
  int solid = 0;
  for (uint8_t c : n_) solid += c >= 3;
  if (solid < static_cast<int>(n_.size() * 0.95)) return false;
  FillGaps();
  complete_ = true;
  ++version_;
  return true;
}

void TrackMap::FillGaps() {
  const int n = static_cast<int>(n_.size());
  for (int b = 0; b < n; ++b) {
    if (n_[b]) continue;
    int prev = b, next = b;
    while (!n_[(prev + n) % n] && prev > b - n) --prev;
    while (!n_[next % n] && next < b + n) ++next;
    const int pi = (prev + n) % n, ni = next % n;
    const float t = static_cast<float>(b - prev) / static_cast<float>(next - prev);
    x_[b] = x_[pi] + (x_[ni] - x_[pi]) * t;
    z_[b] = z_[pi] + (z_[ni] - z_[pi]) * t;
  }
  std::fill(n_.begin(), n_.end(), uint8_t{30});
  filled_ = n;
  Smooth();
}

void TrackMap::Smooth() {
  // Two passes of a [1 2 1] filter around the loop: removes the jitter of quiet sessions
  // (few cars per bin) without rounding off corners at 10 m resolution.
  const size_t n = x_.size();
  if (n < 3) return;
  std::vector<float> tx(n), tz(n);
  for (int pass = 0; pass < 2; ++pass) {
    for (size_t i = 0; i < n; ++i) {
      const size_t a = (i + n - 1) % n, b = (i + 1) % n;
      tx[i] = (x_[a] + 2 * x_[i] + x_[b]) / 4.f;
      tz[i] = (z_[a] + 2 * z_[i] + z_[b]) / 4.f;
    }
    x_.swap(tx);
    z_.swap(tz);
  }
}

bool TrackMap::NoteSector(int lmuSector, double d) {
  // The smallest lap distance seen in a sector converges on where it starts.
  double* s = lmuSector == 2 ? &s2_ : lmuSector == 0 ? &s3_ : nullptr;
  if (!s || d <= 0 || d >= length_) return false;
  if (*s > 0 && d >= *s - 0.5) return false;
  *s = d;
  ++version_;
  return true;
}

int TrackMap::SectorOf(double d) const {
  if (s2_ <= 0 || s3_ <= 0) return -1;
  return d < s2_ ? 0 : d < s3_ ? 1 : 2;
}

bool TrackMap::Position(double d, float& x, float& z) const {
  if (!complete_ || n_.empty()) return false;
  const int n = static_cast<int>(n_.size());
  d = std::fmod(d, length_);
  if (d < 0) d += length_;
  const double f = d / kBin;
  const int a = std::min(n - 1, static_cast<int>(f)), b = (a + 1) % n;
  const float t = static_cast<float>(f - a);
  x = x_[a] + (x_[b] - x_[a]) * t;
  z = z_[a] + (z_[b] - z_[a]) * t;
  return true;
}

bool TrackMap::Load(const IniDoc& doc) {
  const double length = doc.GetFloat("map", "length", 0);
  const std::string xs = doc.Get("map", "x", ""), zs = doc.Get("map", "z", "");
  if (length <= 0 || xs.empty() || doc.GetFloat("map", "bin", 0) != kBin) return false;
  Reset(length);
  auto parse = [](const std::string& s, std::vector<float>& out) {
    size_t i = 0;
    for (const char* p = s.c_str(); *p && i < out.size();) {
      char* end = nullptr;
      out[i++] = std::strtof(p, &end);
      if (end == p) return false;
      p = *end == ',' ? end + 1 : end;
    }
    return i == out.size();
  };
  if (!parse(xs, x_) || !parse(zs, z_)) { Reset(length); return false; }
  std::fill(n_.begin(), n_.end(), uint8_t{30});
  filled_ = static_cast<int>(n_.size());
  complete_ = true;
  if (doc.GetInt("map", "smoothed", 0) == 0) Smooth(); // maps saved by older versions
  s2_ = doc.GetFloat("sectors", "s2", -1);
  s3_ = doc.GetFloat("sectors", "s3", -1);
  ++version_;
  return true;
}

void TrackMap::Save(IniDoc& doc) const {
  if (!complete_) return;
  char buf[32];
  std::string xs, zs;
  xs.reserve(x_.size() * 8);
  zs.reserve(z_.size() * 8);
  for (size_t i = 0; i < x_.size(); ++i) {
    snprintf(buf, sizeof(buf), "%.1f,", x_[i]);
    xs += buf;
    snprintf(buf, sizeof(buf), "%.1f,", z_[i]);
    zs += buf;
  }
  xs.pop_back();
  zs.pop_back();
  snprintf(buf, sizeof(buf), "%.1f", kBin);
  doc.Set("map", "bin", buf);
  snprintf(buf, sizeof(buf), "%.3f", length_);
  doc.Set("map", "length", buf);
  doc.Set("map", "smoothed", "1");
  doc.Set("map", "x", std::move(xs));
  doc.Set("map", "z", std::move(zs));
  snprintf(buf, sizeof(buf), "%.1f", s2_);
  doc.Set("sectors", "s2", buf);
  snprintf(buf, sizeof(buf), "%.1f", s3_);
  doc.Set("sectors", "s3", buf);
}
