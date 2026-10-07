#pragma once
#include <windows.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

// Damage details only available through LMU's local REST API (http://localhost:6397).
struct RestData {
  bool valid = false;
  double time = 0;              // NowSeconds() when received
  float aero = 0;               // aero damage, 0..1
  float suspension[4] = {};     // suspension damage per wheel, 0..1 (FL FR RL RR)
  float brakeWear[4] = {};      // as reported by LMU
  float repairSeconds = 0;      // pit time needed to repair the damage
  float pitTotalSeconds = 0;    // whole pit stop estimate with the current pit menu
  // Pit crew timings for this car (0 = unknown).
  float fuelFillRate = 0;       // litres per second
  float energyFillRate = 0;     // virtual energy fraction per second
  float fuelInsert = 0;         // s before fuel flows
  float tyreChange = 0;         // s for four tyres
  // LMU's own estimate from the garage screen ("42.0l (12.7 laps)"), 0 = unknown.
  float garageFuelPerLap = 0;   // litres per lap
  float garageEnergyPerLap = 0; // virtual energy fraction per lap
};

// Background client: requests run on their own low-priority thread, only when
// asked (after an impact, and every few seconds while driving), never per frame.
class LmuRest {
public:
  ~LmuRest() { Stop(); }
  void Start();
  void Stop();
  void Refresh(bool garage = false); // non-blocking; coalesces repeated calls. garage: also read the
                                      // garage setup (42 KB, only while there's no measured fuel use)
  RestData Latest() const;
  bool Running() const { return thread_.joinable(); }

private:
  void Run(std::stop_token st);
  bool Get(const wchar_t* path, std::string& body);

  std::jthread thread_;
  HANDLE wake_ = nullptr;
  std::atomic<bool> wantGarage_{false};
  float garageFuel_ = 0, garageEnergy_ = 0; // last garage estimate (kept between requests)
  void* session_ = nullptr;     // HINTERNET
  void* connection_ = nullptr;
  mutable std::mutex mutex_;
  RestData data_;
};
