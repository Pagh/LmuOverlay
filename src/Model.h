#pragma once
#include "LmuRest.h"
#include "Snapshot.h"
#include "Timing.h"
#include <cstdint>

// Tracks per-lap usage of a depleting quantity (fuel litres, virtual energy fraction, tyre wear).
class ConsumptionTracker {
public:
  // lapFrac: how far round the lap you are (0..1), for the same-point comparison with the last lap.
  void Update(int lapNumber, double amount, bool inPits, double lapFrac);
  void Reset();

  double PerLap() const;        // average of recent clean laps, or <= 0 if unknown
  double CurrentLapUsed() const { return lapStart_ >= 0 ? lapStart_ - last_ : 0.0; }

  // The last lap that ended (counted even if pitted): how much it used and whether it was clean.
  int Closed() const { return closed_; }        // increments on every lap change
  double LastLapUsed() const { return lastUsed_; }
  bool LastLapClean() const { return lastClean_; }
  bool LastLapPartial() const { return lastPartial_; } // joined mid-lap: its use means nothing
  // This lap's use so far minus the last clean lap's use at the same point of the lap.
  bool VsLastLap(double lapFrac, double& delta) const;

private:
  static constexpr int kHistory = 5;
  static constexpr int kPoints = 100;  // use recorded every 1 % of the lap
  int lap_ = -1;
  double lapStart_ = -1.0;
  double last_ = 0.0;
  bool dirtyLap_ = true;        // pitted / refuelled / joined mid-lap: don't count it
  double history_[kHistory]{};
  int count_ = 0, next_ = 0;
  int closed_ = 0;
  double lastUsed_ = 0.0;
  bool lastClean_ = false;
  bool partial_ = true, lastPartial_ = true;
  // Use so far at each 1 % of the lap: this lap and the last clean one.
  float cur_[kPoints + 1]{}, ref_[kPoints + 1]{};
  int curTop_ = -1, refTop_ = -1;
  bool wrapped_ = false;        // lap distance has wrapped to the new lap (scoring lags telemetry)
};

// One of your completed laps, for the lap history widget.
struct LapRecord {
  int lap = 0;                  // lap number (completed laps count)
  double time = 0;              // s, <= 0 unknown
  bool valid = true;
  bool pit = false;             // went through the pit lane
  bool hasUse = false;          // fuel and wear known
  bool hasEnergy = false;       // energy known (cars with virtual energy)
  double fuel = 0;              // litres used (negative: refuelled)
  double energy = 0;            // virtual energy used, fraction (negative: refilled)
  double wear = 0;              // tread used, average of the four, fraction (negative: new tyres)
};

// Race length and fuel / energy planning, shared by the session header and the strategy widget.
// LMU doesn't expose a "mandatory pit stop" rule, so a stop counts as required when a full
// tank (or 100 % virtual energy) can't cover the estimated race distance.
struct RaceInfo {
  enum class Pace : uint8_t { None, Race, SessionBest, AllTime, Estimate };

  bool valid = false;
  bool race = false;            // race session
  bool started = false;         // green flag has been shown
  bool timed = false;           // ends on time (else on laps)
  double timeLeft = -1;         // s
  double sessionLength = 0;     // s, race length when the overlay saw the race before its start (else 0)
  int lap = 0;                  // lap the player is on (1-based)
  double totalLaps = -1;        // estimated laps the player completes in the whole session
  double lapsToGo = -1;         // laps left for the player, including the current one
  double pace = 0;              // lap time used for the estimate
  Pace paceSource = Pace::None;

  bool usesEnergy = false;      // car runs on LMU's virtual energy
  double fuel = 0, fuelCap = 0, fuelPerLap = 0;  // litres
  double energy = 0, energyPerLap = 0;           // fraction 0..1
  bool perLapMeasured = false;  // per-lap use measured this session (else from saved records)
  double lapsInTank = -1;       // laps the current fuel / energy lasts (the limiting one)
  double lapsOnFull = -1;       // laps a full tank / 100 % energy lasts
  bool energyLimited = false;   // energy runs out before fuel

  bool stopKnown = false;       // enough data to decide
  bool stopRequired = false;    // the race can't be done on one tank (before the start: whole race; after: from here)
  int stopsLeft = 0;
  double fillFuel = 0, fillEnergy = 0; // what to start the race with: race laps + 1 spare, capped
  double addFuel = 0, addEnergy = 0;   // to add at the next stop (same margin)
  int windowOpen = 0, windowClose = 0; // laps (1-based) between which the next stop works
  double spareLaps = 0;         // no stop needed: laps of fuel left over at the flag
};

// The race you're preparing for, set in Settings > General (practice and qualifying don't say).
struct RacePlanSettings {
  bool enabled = true;
  bool byLaps = false;          // else timed
  int minutes = 60;
  int laps = 25;
};

// Strategy for that planned race, from your pace and fuel / energy use in this session.
struct RacePlan {
  bool valid = false;           // outside races, with a plan set
  bool byLaps = false;
  int minutes = 0;
  bool ready = false;           // pace and use per lap known
  double totalLaps = 0;         // laps you'll complete
  double pace = 0;
  bool usesEnergy = false;
  double fuelPerLap = 0, energyPerLap = 0, fuelCap = 0;
  double lapsOnFull = -1;
  bool energyLimited = false;
  int stops = 0;
  double fillFuel = 0, fillEnergy = 0; // to start with
  double addFuel = 0, addEnergy = 0;   // at each stop
  int stopLap = 0;              // latest lap for the first stop
  double spareLaps = 0;         // no stop: laps left over at the flag
};

// Values derived from the latest snapshot, shared by all widgets.
// Updated once per overlay tick on the render thread.
struct Model {
  const Snapshot* snap = nullptr;
  double now = 0.0;

  bool connected = false;
  bool onTrack = false;                          // driving (not in menus / monitor)
  int playerIdx = -1;                            // index into snap->vehicles
  const VehicleScoringInfoV01* player = nullptr;
  const TelemInfoV01* telem = nullptr;           // null if no player telemetry

  double trackLength = 0.0;
  double refLapTime = 0.0;                       // best, else last, else estimated lap time of the player
  double lapsToGo = -1.0;                        // estimated laps left in the session, including the current one

  ConsumptionTracker fuel;
  ConsumptionTracker energy;                     // virtual energy, as fraction 0..1
  ConsumptionTracker wear;                       // average tread left of the four tyres, 1 = new
  double lapFrac = 0;                            // how far round the lap the player is (0..1)

  // Your last laps this session, newest first (filled by UpdateRace()).
  static constexpr int kLapHistory = 10;
  LapRecord laps[kLapHistory];
  int lapCount = 0;

  const Timing* timing = nullptr;                // sector / lap bookkeeping (set by the app)
  const RestData* rest = nullptr;                // damage details from LMU's REST API (may be !valid)
  DeltaRef deltaRef = DeltaRef::LmuBest;         // reference chosen for Delta / Sectors (Ctrl+Alt+D cycles it)
  double deltaRefChangedAt = -100;               // NowSeconds() of the last change (widgets flash the label)
  wchar_t notice[48] = L"";                      // short confirmation (profile / reference switched)
  double noticeAt = -100;                        // NowSeconds() when it was set

  RaceInfo race;                                 // see UpdateRace()
  RacePlanSettings planCfg;                      // set by the app from settings.ini
  RacePlan plan;                                 // filled by UpdateRace() outside races

  void Update(const Snapshot& s, double nowSeconds);
  // A car's lap distance right now: LMU's scoring data is ~6 Hz, so move it on by its speed
  // for the time since that update (telemetry is newer).
  double LiveLapDist(const VehicleScoringInfoV01& v) const;
  // After timing (records) and REST data are attached: race length and fuel planning.
  void UpdateRace();

  // Normalises LMU's virtual energy / SoC to 0..1 (handles either fraction or percent).
  static double Fraction(float v) { return v > 1.5f ? v / 100.0 : v; }

private:
  void UpdateRaceInfo();
  void UpdatePlan();
  void UpdateLapHistory();

  long lastSession_ = -1;
  char lastTrack_[64] = "";
  char lastCar_[30] = "";
  double maxTimeLeft_ = 0;
  bool sawPreStart_ = false;
  double lastET_ = 0.0;
  // Lap history: the lap time (scoring) and the lap's use (telemetry) arrive at slightly different times.
  int seenLapSerial_ = 0, seenClosed_ = 0;
  double closedAt_ = -100, timeAt_ = -100;
  bool lapInPits_ = false;
  LapRecord closedLap_, timedLap_;
};
