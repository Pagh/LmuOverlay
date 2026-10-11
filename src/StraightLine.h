#pragma once
#include "LmuSdk.h"

// What damage costs on the straights. The car's acceleration at full throttle is recorded against
// its speed, before the damage and since: compared at the same speed, how well you came out of the
// corner doesn't matter. Your last clean lap's full-throttle stretches are then driven again with
// each curve, from the same entry speeds: the difference is the time lost on the straights and the
// top speed lost. Everything is fixed-size, nothing allocated per sample.
class StraightLine {
public:
  struct Result {
    bool valid = false;       // enough measured since the damage
    double timeLoss = 0;      // s per lap on the full-throttle stretches
    double topSpeedLoss = 0;  // km/h at the end of the fastest stretch
    double coverage = 0;      // share of those stretches covered by measurements (0..1)
  };

  void Reset();
  // Every new telemetry sample. damageSerial changes whenever new damage appears; damaged = has
  // damage now (body, aero, suspension, detached parts).
  void Sample(const TelemInfoV01& t, double lapDist, double trackLength, bool inPits, int damageSerial, bool damaged);
  // Before / since comparison (a few hundred integration steps; call it a few times per second at most).
  Result Compare() const;
  bool HasReference() const { return refSteps_ > 0; }

private:
  static constexpr double kMinKph = 60.0, kBinKph = 5.0;
  static constexpr int kBins = 64;           // 60 .. 380 km/h
  static constexpr double kStep = 10.0;      // m, lap profile resolution
  static constexpr int kMaxSteps = 2000;     // tracks up to 20 km
  static constexpr int kMinSamples = 8;      // per speed bin

  struct Curve {
    float sum[kBins]{};
    int n[kBins]{};
    void Add(double kph, double accel);
    bool At(double kph, double& accel) const; // interpolated between measured bins
    void Clear() { *this = Curve{}; }
  };
  static double Simulate(const Curve& c, double v0, int steps, double& vEnd, bool& ok);

  Curve before_, since_;
  int serial_ = -1;
  bool wasDamaged_ = false;
  double lastET_ = -1, shiftET_ = -100;
  long gear_ = 0;

  // Speed and full-throttle flag every kStep metres: the lap in progress and the last clean one.
  float lapV_[kMaxSteps]{}, refV_[kMaxSteps]{};
  bool lapFull_[kMaxSteps]{}, refFull_[kMaxSteps]{};
  bool lapSeen_[kMaxSteps]{};
  int lapSteps_ = 0, refSteps_ = 0;
  long lap_ = -1;
  bool lapClean_ = false;
  int lastStep_ = -1;
};
