#pragma once
#include "LapTrace.h"
#include "Snapshot.h"
#include "TrackMap.h"
#include <string>
#include <unordered_map>

class IniDoc;

// Individual sector times (not cumulative) and the lap time. 0 = unknown.
struct SectorSet {
  double s[3] = {0, 0, 0};
  double lap = 0;
  bool Complete() const { return s[0] > 0 && s[1] > 0 && s[2] > 0; }
  double Ideal() const { return Complete() ? s[0] + s[1] + s[2] : 0.0; }
  void TakeBest(const SectorSet& o); // per-sector minimum + best lap
};

// Builds a SectorSet from LMU's cumulative values (s1, s1+s2, lap). Returns false if inconsistent.
bool SectorsFromCumulative(double s1, double s12, double lap, SectorSet& out);

// What the live delta is measured against.
enum class DeltaRef { LmuBest, SessionBest, AllTime, Lobby, LastLap, Count };
const wchar_t* DeltaRefLabel(DeltaRef r);

// Lap / sector bookkeeping for the whole field, plus the player's all-time records.
// Updated on the render thread only when scoring data changed (a few times per second).
class Timing {
public:
  struct Vehicle {
    double lastLapSeen = -2;   // mLastLapTime when we last looked
    SectorSet last;            // last completed lap
    SectorSet best;            // best per sector this session + best lap
    LapRecorder recorder;      // distance -> time of the lap in progress
    LapTrace trace;            // last completed lap's trace, waiting for its official time
    double traceET = -100;
    double pendingLap = 0;     // official lap time waiting for its trace
    bool pendingValid = false;
    double pendingET = -100;
  };
  struct ClassBest {
    SectorSet best;
    std::string holder[4];     // driver names for S1, S2, S3, lap
  };

  void SetRecordsDir(std::wstring dir) { recordsDir_ = std::move(dir); }
  void Update(const Snapshot& s, int playerIdx);

  const Vehicle* Find(long id) const;
  const ClassBest* Class(const char* vehicleClass) const;

  // Player
  const SectorSet& PlayerLast() const { return playerLast_; }
  bool PlayerLastValid() const { return playerLastValid_; }
  int PlayerLapSerial() const { return playerLapSerial_; } // increments with every lap time you set
  const SectorSet& PlayerSession() const { return playerSession_; }
  const SectorSet& PlayerAllTime() const { return allTime_; }
  bool CurrentLapInvalid() const { return curLapInvalid_; }

  // Live delta of the lap in progress against a reference lap. Returns false if unavailable.
  // On an out lap (the lap started in the pits) the delta starts at sector 2, from zero.
  bool Delta(DeltaRef ref, double& delta) const;
  // Lap time of a reference (lobby: the fastest lap in your class as LMU reports it, fixed for
  // the length of each of your laps). 0 = none.
  double ReferenceLap(DeltaRef ref) const;
  // Spread (standard deviation, s) of your last valid laps; 0 until 3 of them. laps = how many.
  double Consistency(int* laps = nullptr) const;
  const std::string& LobbyHolder() const { return lobbyRefHolder_; }
  const SectorSet& LobbyLap() const { return lobbyRef_; }     // sectors of that lap (may be incomplete)
  bool OutLap() const { return outLap_; }
  bool OutLapWaiting() const { return outLap_ && !outBaseSet_; } // out lap, sector 2 not reached yet
  double CurrentLapTime() const { return curT_; }
  // Seconds it takes to drive from lap distance a forward to b, at your recorded pace
  // (best trace available). < 0 if no lap has been recorded here yet.
  double TimeAlong(double a, double b) const;

  // All-time bests (PlayerAllTime, the ALL-TIME delta trace) are shared by the cars of a class;
  // fuel / virtual energy use per lap is remembered for this track and car (0 = unknown).
  double SavedFuelPerLap() const { return fuelPerLap_; }
  double SavedEnergyPerLap() const { return energyPerLap_; }
  void RecordConsumption(double fuelPerLap, double energyPerLap);

  // Track outline (learned from all cars, saved per track).
  const TrackMap& Map() const { return map_; }

  // Pace effect of damage: average valid lap before the first damage vs since (0 = unknown).
  double PaceBeforeDamage() const { return paceBefore_; }
  double PaceSinceDamage() const { return paceSince_; }

private:
  bool RawDelta(DeltaRef ref, double& delta) const;
  bool TraceDelta(const LapTrace& tr, double& delta) const;
  const LapTrace* LobbyShape() const;
  double WarpAt(const LapTrace& shape, const SectorSet& ref, double d) const;
  void Reset();
  void OnPlayerLap(const SectorSet& lap, bool valid, bool damaged);
  void PairTrace(Vehicle& vt, const VehicleScoringInfoV01& v, bool isPlayer, bool playerClass, double et);
  void LoadRecords(const std::string& track, const std::string& car, const std::string& cls);
  void SaveRecords() const;              // all-time best (+ trace) to BestFile()
  void PutUsage(IniDoc& doc) const;
  std::wstring CarFile() const;          // records\<track> - <car>.ini: fuel / energy use
  std::wstring BestFile() const;         // records\<track> - class <class>.ini (car file without a class)
  void LoadMap(const std::string& track, double length);
  void SaveMap();

  std::wstring recordsDir_;
  std::unordered_map<long, Vehicle> vehicles_;
  std::unordered_map<std::string, ClassBest> classes_;

  long session_ = -1;
  double lastET_ = -1;
  std::string track_, car_, class_;

  SectorSet playerLast_, playerSession_, allTime_;
  bool playerLastValid_ = true;
  int playerLapSerial_ = 0;     // never reset
  long lapNumber_ = -1;
  bool curLapInvalid_ = false, prevLapInvalid_ = false;
  double lapChangeTime_ = -100; // telemetry time of the last lap-number change

  LapTrace sessionTrace_, allTimeTrace_, lobbyTrace_; // lobbyTrace_: fastest class lap recorded live
  LapTrace lastTrace_;                               // your last valid lap (consistency reference)
  static constexpr int kRecent = 5;
  double recent_[kRecent]{};                         // your last valid lap times
  int recentN_ = 0, recentHead_ = 0;
  SectorSet lobbyRef_;                               // lobby fastest lap (LMU's numbers), per lap of yours
  std::string lobbyRefHolder_;
  long lobbyRefKey_ = -100;                          // your lap count when it was taken
  double lmuDelta_ = 0, playerBest_ = 0;             // LMU's own delta and your best lap
  bool outLap_ = false, outBaseSet_ = false;
  double outBase_[static_cast<int>(DeltaRef::Count)]{};
  bool outBaseOk_[static_cast<int>(DeltaRef::Count)]{};
  double curD_ = 0, curT_ = 0;
  bool curValid_ = false;
  double curTrackLength_ = 0;

  double fuelPerLap_ = 0, energyPerLap_ = 0;

  TrackMap map_;
  std::string mapTrack_;
  uint32_t mapSavedVersion_ = 0;
  double mapSaveET_ = -1e9;

  int dentSum_ = 0;
  bool damaged_ = false;
  double preSum_ = 0, postSum_ = 0;
  int preCount_ = 0, postCount_ = 0;
  double paceBefore_ = 0, paceSince_ = 0;
  bool lapHadDamageChange_ = false;
};
