#pragma once
#include "Ini.h"
#include <cstdint>
#include <vector>

// Track outline learned from every car's world position (scoring rate, so a busy session
// fills it within a lap or two), indexed by lap distance in 10 m bins. Saved per track.
// LMU's world is left-handed with y up: seen from above, x goes right and z goes *up*.
class TrackMap {
public:
  static constexpr double kBin = 10.0;

  void Reset(double length);
  // A car at lap distance d was at world (x, z). Returns true when the map just became complete.
  bool Add(double d, double x, double z);
  // A car at lap distance d is in LMU sector lmuSector (0 = sector 3, 1 = sector 1, 2 = sector 2).
  // Returns true when a boundary moved.
  bool NoteSector(int lmuSector, double d);

  bool Complete() const { return complete_; }
  float Progress() const { return n_.empty() ? 0.f : static_cast<float>(filled_) / static_cast<float>(n_.size()); }
  double Length() const { return length_; }
  uint32_t Version() const { return version_; } // changes when the outline or sectors change
  const std::vector<float>& X() const { return x_; }
  const std::vector<float>& Z() const { return z_; }
  // Lap distance where sector 2 / 3 start (<= 0 if not seen yet).
  double SectorStart(int sector /*1 or 2*/) const { return sector == 1 ? s2_ : s3_; }
  // Sector (0..2) of a lap distance, -1 if the boundaries aren't known.
  int SectorOf(double d) const;
  // World position of a lap distance (interpolated).
  bool Position(double d, float& x, float& z) const;

  bool Load(const IniDoc& doc);
  void Save(IniDoc& doc) const;

private:
  void FillGaps();
  void Smooth();

  double length_ = 0;
  std::vector<float> x_, z_;
  std::vector<uint8_t> n_;
  int filled_ = 0;
  bool complete_ = false;
  double s2_ = -1, s3_ = -1;
  uint32_t version_ = 0;
};
