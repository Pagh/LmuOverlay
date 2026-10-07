#include "WheelButtons.h"
#include "Ini.h"
#include <cstdio>

namespace {
struct ActionInfo { const char* key; const char* label; };
const ActionInfo kActions[] = {
  {"next_delta_reference", "Next delta reference"},
  {"prev_delta_reference", "Previous delta reference"},
  {"next_profile", "Next profile"},
  {"toggle_overlay", "Show / hide overlay"},
};
static_assert(sizeof(kActions) / sizeof(kActions[0]) == static_cast<int>(WheelAction::Count));

std::string GuidString(const GUID& g) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", g.Data1, g.Data2, g.Data3, g.Data4[0],
           g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
  return buf;
}
} // namespace

const char* WheelActionKey(WheelAction a) { return kActions[static_cast<int>(a)].key; }
const char* WheelActionLabel(WheelAction a) { return kActions[static_cast<int>(a)].label; }

std::string ButtonBinding::Serialize() const {
  if (!Valid()) return "";
  return guid + "|" + std::to_string(button) + "|" + name;
}

ButtonBinding ButtonBinding::Parse(const std::string& s) {
  ButtonBinding b;
  const size_t a = s.find('|');
  if (a == std::string::npos) return b;
  const size_t c = s.find('|', a + 1);
  b.guid = s.substr(0, a);
  b.button = std::atoi(s.substr(a + 1, c == std::string::npos ? std::string::npos : c - a - 1).c_str());
  if (c != std::string::npos) b.name = s.substr(c + 1);
  if (b.button < 0 || b.button > 127) b = ButtonBinding{};
  return b;
}

bool WheelButtons::Init(HWND hwnd) {
  hwnd_ = hwnd;
  return SUCCEEDED(DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W,
                                      reinterpret_cast<void**>(di_.ReleaseAndGetAddressOf()), nullptr));
}

void WheelButtons::Shutdown() {
  for (Device& d : devices_) if (d.dev) d.dev->Unacquire();
  devices_.clear();
  di_.Reset();
}

void WheelButtons::SetBindings(const std::vector<ButtonBinding>& bindings) {
  bindings_ = bindings;
  bool any = false;
  for (const ButtonBinding& b : bindings_) any |= b.Valid();
  if (any && !enumerated_) rescanAt_ = 1; // open the devices on the next poll
}

BOOL CALLBACK WheelButtons::EnumCallback(LPCDIDEVICEINSTANCEW inst, LPVOID self) {
  auto* w = static_cast<WheelButtons*>(self);
  Device d;
  d.guid = GuidString(inst->guidInstance);
  d.name = Narrow(inst->tszProductName);
  if (FAILED(w->di_->CreateDevice(inst->guidInstance, &d.dev, nullptr))) return DIENUM_CONTINUE;
  if (FAILED(d.dev->SetDataFormat(&c_dfDIJoystick2)) ||
      FAILED(d.dev->SetCooperativeLevel(w->hwnd_, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE)))
    return DIENUM_CONTINUE;
  DIPROPDWORD buf{};
  buf.diph.dwSize = sizeof(buf);
  buf.diph.dwHeaderSize = sizeof(DIPROPHEADER);
  buf.diph.dwHow = DIPH_DEVICE;
  buf.dwData = 64; // events kept between polls
  d.dev->SetProperty(DIPROP_BUFFERSIZE, &buf.diph);
  d.dev->Acquire();
  w->devices_.push_back(std::move(d));
  return DIENUM_CONTINUE;
}

void WheelButtons::Enumerate() {
  for (Device& d : devices_) if (d.dev) d.dev->Unacquire();
  devices_.clear();
  enumerated_ = true;
  if (di_) di_->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumCallback, this, DIEDFL_ATTACHEDONLY);
}

bool WheelButtons::Wanted(const Device& d) const {
  if (capturing_) return true;
  for (const ButtonBinding& b : bindings_)
    if (b.Valid() && (b.guid == d.guid || (!b.name.empty() && b.name == d.name))) return true;
  return false;
}

void WheelButtons::Poll(std::vector<WheelAction>& fired) {
  if (!di_) return;
  if (rescanAt_ && GetTickCount64() >= rescanAt_) { rescanAt_ = 0; Enumerate(); }
  for (Device& d : devices_) {
    // Drain every device (so presses made before a binding existed never fire later), act on wanted ones.
    const bool wanted = Wanted(d);
    DIDEVICEOBJECTDATA data[64];
    DWORD n = 64;
    HRESULT hr = d.dev->GetDeviceData(sizeof(DIDEVICEOBJECTDATA), data, &n, 0);
    if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
      d.dev->Acquire();
      n = 64;
      hr = d.dev->GetDeviceData(sizeof(DIDEVICEOBJECTDATA), data, &n, 0);
    }
    if (FAILED(hr) || !wanted) continue;
    for (DWORD i = 0; i < n; ++i) {
      if (data[i].dwOfs < DIJOFS_BUTTON0 || data[i].dwOfs > DIJOFS_BUTTON(127)) continue;
      if (!(data[i].dwData & 0x80)) continue; // presses only
      const int button = static_cast<int>(data[i].dwOfs - DIJOFS_BUTTON0);
      if (capturing_) {
        capture_ = ButtonBinding{d.guid, d.name, button};
        captured_ = true;
        capturing_ = false;
        continue;
      }
      for (size_t a = 0; a < bindings_.size(); ++a) {
        const ButtonBinding& b = bindings_[a];
        if (b.Valid() && b.button == button && (b.guid == d.guid || b.name == d.name))
          fired.push_back(static_cast<WheelAction>(a));
      }
    }
  }
}

void WheelButtons::StartCapture() {
  captured_ = false;
  capturing_ = true;
  Enumerate(); // pick up anything plugged in since
}

bool WheelButtons::TakeCaptured(ButtonBinding& out) {
  if (!captured_) return false;
  out = capture_;
  captured_ = false;
  return true;
}

std::string WheelButtons::Describe(const ButtonBinding& b) const {
  if (!b.Valid()) return "not set";
  return (b.name.empty() ? std::string("device") : b.name) + "  ·  button " + std::to_string(b.button + 1);
}
