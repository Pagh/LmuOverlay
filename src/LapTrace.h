#pragma once
#include <vector>

// A lap as "elapsed time at every 5 m of lap distance": what's needed to compute a
// live delta against any lap (yours, or another car's).
constexpr double kTraceBin = 5.0; // metres

struct LapTrace {
  std::vector<float> t;   // seconds since the lap started, at distance i * kTraceBin
  double lap = 0;         // lap time (= time at distance `length`)
  double length = 0;      // track length the trace was recorded on
  bool Valid() const { return lap > 0 && t.size() >= 2; }
  double At(double distance) const; // interpolated elapsed time at a lap distance
};

// Records one car's current lap from (lap distance, lap time) samples, at any rate:
// 60 Hz for the player (telemetry), ~6 Hz for other cars (scoring).
class LapRecorder {
public:
  // Returns true when this sample started a new lap and the previous one was recorded
  // completely (then Finished() holds it until the next completed lap).
  bool Sample(double lapDist, double lapTime, double lapStartET, double trackLength);
  const LapTrace& Finished() const { return finished_; }

  bool Recording() const { return startET_ >= 0 && !partial_; } // current lap is being captured from the line
  double Distance() const { return lastD_; }

private:
  void FillTo(double d, double t);

  std::vector<float> cur_;
  double startET_ = -1;
  double lastD_ = 0, lastT_ = 0;
  double length_ = 0;
  bool partial_ = true;    // joined mid-lap, teleported, etc.: don't keep this lap
  LapTrace finished_;
};
