#include "SharedMemoryReader.h"
#include "Clock.h"
#include <algorithm>

SharedMemoryReader::SharedMemoryReader(TripleBuffer<Snapshot>& out, int pollHz)
    : out_(out), period_(1.0 / std::max(pollHz, 1)) {}

SharedMemoryReader::~SharedMemoryReader() { Stop(); }

void SharedMemoryReader::Start() {
  thread_ = std::jthread([this](std::stop_token st) { Run(st); });
}

void SharedMemoryReader::Stop() {
  if (thread_.joinable()) {
    thread_.request_stop();
    thread_.join();
  }
  Disconnect();
}

bool SharedMemoryReader::Connect() {
  // The update events only exist while a recent-enough LMU build is running.
  waiter_ = SharedMemoryUpdateWaiter::MakeSharedMemoryUpdateWaiter();
  if (!waiter_) return false;

  mapping_ = OpenFileMappingA(FILE_MAP_READ, FALSE, LMU_SHARED_MEMORY_FILE);
  if (!mapping_) { Disconnect(); return false; }

  // Read-only view: the overlay can never corrupt the game's data.
  view_ = static_cast<const SharedMemoryLayout*>(MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, sizeof(SharedMemoryLayout)));
  if (!view_) { Disconnect(); return false; }

  lock_ = SharedMemoryLock::MakeSharedMemoryLock();
  if (!lock_) { Disconnect(); return false; }
  return true;
}

void SharedMemoryReader::Disconnect() {
  lock_.reset();
  if (view_) { UnmapViewOfFile(view_); view_ = nullptr; }
  if (mapping_) { CloseHandle(mapping_); mapping_ = nullptr; }
  waiter_.reset();
}

bool SharedMemoryReader::CopyFrame(Snapshot& s) {
  const SharedMemoryObjectOut& src = view_->data;

  const double t0 = NowSeconds();
  if (!lock_->Lock(50)) return false;
  const double t1 = NowSeconds();

  // ---- critical section: plain memcpys only, no allocation, no logging ----
  s.gameVersion = src.generic.gameVersion;
  std::memcpy(&s.app, &src.generic.appInfo, sizeof(ApplicationStateV01));
  std::memcpy(&s.scoring, &src.scoring.scoringInfo, sizeof(ScoringInfoV01));
  const int n = std::clamp<int>(s.scoring.mNumVehicles, 0, kMaxVehicles);
  std::memcpy(s.vehicles, src.scoring.vehScoringInfo, n * sizeof(VehicleScoringInfoV01));

  const SharedMemoryTelemetryData& tel = src.telemetry;
  const int active = std::min<int>(tel.activeVehicles, kMaxVehicles);
  const int idx = tel.playerVehicleIdx;
  s.hasPlayerTelem = tel.playerHasVehicle && idx < active;
  if (s.hasPlayerTelem) std::memcpy(&s.telem, &tel.telemInfo[idx], sizeof(TelemInfoV01));
  for (int k = 0; k < active; ++k) { // ~110 bytes per car
    const TelemInfoV01& ti = tel.telemInfo[k];
    Snapshot::CarModel& cm = s.models[k];
    cm.id = ti.mID;
    std::memcpy(cm.name, ti.mVehicleModel, sizeof(cm.name));
    cm.pos = ti.mPos;
    cm.virtualEnergy = ti.mVirtualEnergy;
    std::memcpy(cm.tyreF, ti.mFrontTireCompoundName, sizeof(cm.tyreF));
    std::memcpy(cm.tyreR, ti.mRearTireCompoundName, sizeof(cm.tyreR));
    for (int w = 0; w < 4; ++w) cm.wear[w] = ti.mWheel[w].mWear;
  }
  s.numModels = active;
  // ---- end critical section ----

  lock_->Unlock();
  const double t2 = NowSeconds();

  s.numVehicles = n;
  s.DetectOthersWear(s.hasPlayerTelem ? s.telem.mID : -1);
  s.scoring.mVehicle = nullptr;       // pointers into the game's address space
  s.scoring.mResultsStream = nullptr;
  s.copyTime = t2;
  s.lockWaitUs = static_cast<float>((t1 - t0) * 1e6);
  s.lockHoldUs = static_cast<float>((t2 - t1) * 1e6);
  return true;
}

void SharedMemoryReader::PublishDisconnected() {
  Snapshot& s = out_.WriteBuffer();
  s.connected = false;
  s.seq = ++seq_;
  out_.Publish();
}

void SharedMemoryReader::Run(std::stop_token st) {
  SetThreadDescription(GetCurrentThread(), L"LMU shared memory reader");
  // This thread sleeps almost all the time and wakes only for a few microseconds.
  // High priority means it can't be preempted while holding LMU's lock, which
  // would otherwise make the game wait on us (priority inversion).
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

  PreciseTimer timer;
  bool connected = false;
  double lastFrame = 0.0;

  while (!st.stop_requested()) {
    if (!view_ && !Connect()) {
      if (connected) { PublishDisconnected(); connected = false; }
      timer.Sleep(1.0);
      continue;
    }

    // 200ms timeout only so we notice stop requests / a closed game.
    if (!waiter_->WaitForNextFrame(200)) {
      if (connected && NowSeconds() - lastFrame > 2.0) { PublishDisconnected(); connected = false; }
      continue;
    }

    const double frameTime = NowSeconds();
    lastFrame = frameTime;
    Snapshot& s = out_.WriteBuffer();
    if (CopyFrame(s)) {
      // Our open handles keep the mapping alive after LMU exits; don't show stale data.
      if (s.app.mAppWindow && !IsWindow(s.app.mAppWindow)) {
        if (connected) { PublishDisconnected(); connected = false; }
        timer.Sleep(0.5);
        continue;
      }
      s.connected = true;
      s.demo = false;
      s.seq = ++seq_;
      out_.Publish();
      connected = true;
    }

    // Rate limit: LMU signals every frame (144+ Hz); we don't need that many copies.
    const double remaining = frameTime + period_ - NowSeconds();
    if (remaining > 0) timer.Sleep(remaining);
  }
}
