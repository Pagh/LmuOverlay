#include "LmuRest.h"
#include <cstdio>
#include <cstring>
#include "Clock.h"
#include "Json.h"
#include <winhttp.h>

void LmuRest::Start() {
  if (thread_.joinable()) return;
  wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  thread_ = std::jthread([this](std::stop_token st) { Run(st); });
}

void LmuRest::Stop() {
  if (thread_.joinable()) {
    thread_.request_stop();
    SetEvent(wake_);
    thread_.join();
  }
  if (wake_) { CloseHandle(wake_); wake_ = nullptr; }
}

void LmuRest::Refresh(bool garage) {
  if (garage) wantGarage_ = true;
  if (wake_) SetEvent(wake_);
}

RestData LmuRest::Latest() const {
  std::lock_guard lock(mutex_);
  return data_;
}

bool LmuRest::Get(const wchar_t* path, std::string& body) {
  body.clear();
  HINTERNET req = WinHttpOpenRequest(connection_, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
  if (!req) return false;
  bool ok = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(req, nullptr);
  DWORD status = 0, size = sizeof(status);
  if (ok) {
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    ok = status == 200;
  }
  while (ok) {
    DWORD avail = 0;
    if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
    const size_t old = body.size();
    body.resize(old + avail);
    DWORD read = 0;
    if (!WinHttpReadData(req, body.data() + old, avail, &read)) { ok = false; break; }
    body.resize(old + read);
    if (body.size() > 4 * 1024 * 1024) { ok = false; break; } // sanity limit
  }
  WinHttpCloseHandle(req);
  return ok;
}

void LmuRest::Run(std::stop_token st) {
  SetThreadDescription(GetCurrentThread(), L"LMU REST client");
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
  session_ = WinHttpOpen(L"LmuOverlay", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session_) return;
  WinHttpSetTimeouts(session_, 1000, 1000, 1000, 2000);
  connection_ = WinHttpConnect(session_, L"127.0.0.1", 6397, 0);

  std::string body;
  while (!st.stop_requested()) {
    WaitForSingleObject(wake_, INFINITE);
    if (st.stop_requested() || !connection_) break;

    RestData d;
    Json j;
    if (Get(L"/rest/garage/UIScreen/RepairAndRefuel", body) && Json::Parse(body, j)) {
      if (const Json* w = j.Get("wearables")) {
        d.valid = true;
        if (const Json* a = w->Path({"body", "aero"})) d.aero = static_cast<float>(a->Num());
        if (const Json* s = w->Get("suspension"); s && s->array.size() >= 4)
          for (int i = 0; i < 4; ++i) d.suspension[i] = static_cast<float>(s->array[i].Num());
        if (const Json* b = w->Get("brakes"); b && b->array.size() >= 4)
          for (int i = 0; i < 4; ++i) d.brakeWear[i] = static_cast<float>(b->array[i].Num());
      }
      if (const Json* t = j.Path({"pitStopTimes", "times"})) {
        auto num = [t](const char* key) { const Json* v = t->Get(key); return v ? static_cast<float>(v->Num()) : 0.f; };
        d.fuelFillRate = num("FuelFillRate");
        d.energyFillRate = num("virtualEnergyFillRate");
        d.fuelInsert = num("FuelInsert");
        d.tyreChange = num("FourTireChange");
      }
    }
    if (Get(L"/rest/strategy/pitstop-estimate", body) && Json::Parse(body, j)) {
      if (const Json* r = j.Get("damage")) d.repairSeconds = static_cast<float>(r->Num());
      if (const Json* t = j.Get("total")) d.pitTotalSeconds = static_cast<float>(t->Num());
    }
    if (wantGarage_.exchange(false) && Get(L"/rest/garage/getPlayerGarageData", body) && Json::Parse(body, j)) {
      // stringValue looks like "42.0l (12.7 laps)" or "85% (11.2 laps)".
      auto perLap = [&j](const char* key, double scale) -> float {
        const Json* v = j.Path({key, "stringValue"});
        if (!v || v->type != Json::Type::String) return 0.f;
        double amount = 0, laps = 0;
        const char* s = v->string.c_str();
        const char* open = strchr(s, '(');
        if (!open || sscanf_s(s, "%lf", &amount) != 1 || sscanf_s(open + 1, "%lf", &laps) != 1 || laps <= 0.1) return 0.f;
        return static_cast<float>(amount * scale / laps);
      };
      garageFuel_ = perLap("VM_FUEL_LEVEL", 1.0);
      garageEnergy_ = perLap("VM_VIRTUAL_ENERGY", 0.01);
    }
    d.garageFuelPerLap = garageFuel_;
    d.garageEnergyPerLap = garageEnergy_;
    d.time = NowSeconds();
    std::lock_guard lock(mutex_);
    data_ = d;
  }

  if (connection_) WinHttpCloseHandle(connection_);
  WinHttpCloseHandle(session_);
  connection_ = session_ = nullptr;
}
