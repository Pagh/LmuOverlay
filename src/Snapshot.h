#pragma once
#include "LmuSdk.h"
#include <atomic>

// The subset of LMU's shared memory the overlay needs, copied out under the
// game's lock in one go. Only the player's telemetry is copied (not all 104
// cars) to keep the time we hold the game's lock as short as possible.
struct Snapshot {
  uint64_t seq = 0;            // increments on every successful copy
  double copyTime = 0.0;       // QPC seconds when copied
  bool connected = false;      // reader has a live mapping and recent frames
  bool demo = false;

  long gameVersion = 0;
  ApplicationStateV01 app{};

  ScoringInfoV01 scoring{};    // mVehicle / mResultsStream pointers are NOT valid here
  int numVehicles = 0;
  VehicleScoringInfoV01 vehicles[kMaxVehicles]{};

  bool hasPlayerTelem = false;
  TelemInfoV01 telem{};        // player vehicle

  // Per-car extras only available in the telemetry block: car model ("BMW M4 LMGT3") and the
  // world position at telemetry rate (scoring positions only update ~6 times per second).
  struct CarModel {
    long id;
    char name[30];     // not always NUL-terminated
    TelemVect3 pos;
  };
  int numModels = 0;
  CarModel models[kMaxVehicles]{};
  const CarModel* CarFor(long id) const {
    for (int i = 0; i < numModels; ++i) if (models[i].id == id) return &models[i];
    return nullptr;
  }
  const char* ModelFor(long id) const {
    const CarModel* c = CarFor(id);
    return c ? c->name : nullptr;
  }

  // Reader diagnostics (how long we held the game's lock).
  float lockWaitUs = 0.f;
  float lockHoldUs = 0.f;
};

// Lock-free single-producer / single-consumer triple buffer. The producer never
// waits for the consumer, so a slow render frame can never delay a copy.
template <typename T>
class TripleBuffer {
public:
  T& WriteBuffer() { return buf_[back_]; }
  void Publish() { back_ = state_.exchange(back_ | kDirty, std::memory_order_acq_rel) & kIndex; }

  // Returns true if a newer buffer was swapped in. Read() is always valid.
  bool Acquire() {
    if (!(state_.load(std::memory_order_relaxed) & kDirty)) return false;
    front_ = state_.exchange(front_, std::memory_order_acq_rel) & kIndex;
    return true;
  }
  const T& Read() const { return buf_[front_]; }

private:
  static constexpr uint8_t kDirty = 4, kIndex = 3;
  T buf_[3]{};
  std::atomic<uint8_t> state_{1};
  uint8_t back_ = 0, front_ = 2;
};
