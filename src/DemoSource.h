#pragma once
#include "Snapshot.h"
#include <thread>

// Synthetic race (run with --demo) so the overlay can be laid out and tested
// without the game running. Writes into the same triple buffer as the real reader.
class DemoSource {
public:
  DemoSource(TripleBuffer<Snapshot>& out, int pollHz);
  ~DemoSource();
  void Start();
  void Stop();

private:
  void Run(std::stop_token st);
  void Fill(Snapshot& s, double t);

  TripleBuffer<Snapshot>& out_;
  double period_;
  uint64_t seq_ = 0;
  std::jthread thread_;
};
