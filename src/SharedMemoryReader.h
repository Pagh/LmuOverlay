#pragma once
#include "Snapshot.h"
#include <optional>
#include <thread>

// Reads LMU's native shared memory ("LMU_Data") from outside the game process.
// No DLL is loaded into LMU. Runs on its own thread:
//   wait for the game's "new frame" event -> take the game's lock -> memcpy what
//   we need -> unlock -> publish to the triple buffer -> sleep until next poll.
// The lock is held only for the memcpy (typically a few microseconds).
class SharedMemoryReader {
public:
  SharedMemoryReader(TripleBuffer<Snapshot>& out, int pollHz);
  ~SharedMemoryReader();

  void Start();
  void Stop();

private:
  void Run(std::stop_token st);
  bool Connect();
  void Disconnect();
  bool CopyFrame(Snapshot& dst);
  void PublishDisconnected();

  TripleBuffer<Snapshot>& out_;
  double period_;
  uint64_t seq_ = 0;

  std::optional<SharedMemoryUpdateWaiter> waiter_;
  std::optional<SharedMemoryLock> lock_;
  HANDLE mapping_ = nullptr;
  const SharedMemoryLayout* view_ = nullptr;

  std::jthread thread_;
};
