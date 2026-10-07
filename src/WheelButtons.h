#pragma once
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <wrl/client.h>
#include <string>
#include <vector>

// Actions you can put on a wheel button.
enum class WheelAction { NextDeltaRef, PrevDeltaRef, NextProfile, ToggleOverlay, Count };
const char* WheelActionKey(WheelAction a);   // settings.ini key
const char* WheelActionLabel(WheelAction a); // settings window

struct ButtonBinding {
  std::string guid;     // DirectInput instance GUID of the device
  std::string name;     // product name (shown, and used if the GUID changed)
  int button = -1;      // 0-based
  bool Valid() const { return button >= 0 && !guid.empty(); }
  std::string Serialize() const;               // "guid|button|name"
  static ButtonBinding Parse(const std::string& s);
};

// Reads wheel / button box buttons through DirectInput, non-exclusive and in the background,
// so LMU keeps full control of the wheel (force feedback included). Only devices with a binding
// are read; events are buffered by DirectInput, so a press between two polls is never lost.
// Devices are listed once at start and again only when Windows reports a device change.
class WheelButtons {
public:
  ~WheelButtons() { Shutdown(); }
  bool Init(HWND hwnd);
  void Shutdown();
  void SetBindings(const std::vector<ButtonBinding>& bindings); // indexed by WheelAction
  void DevicesChanged() { rescanAt_ = GetTickCount64() + 1500; } // WM_DEVICECHANGE (debounced)

  // Call regularly (a few times per second at least). Appends the actions that were pressed.
  void Poll(std::vector<WheelAction>& fired);

  // Binding: the next button pressed on any device.
  void StartCapture();
  void CancelCapture() { capturing_ = false; }
  bool Capturing() const { return capturing_; }
  bool TakeCaptured(ButtonBinding& out);

  std::string Describe(const ButtonBinding& b) const;
  int DeviceCount() const { return static_cast<int>(devices_.size()); }

private:
  struct Device {
    Microsoft::WRL::ComPtr<IDirectInputDevice8W> dev;
    std::string guid, name;
  };
  void Enumerate();
  bool Wanted(const Device& d) const;
  static BOOL CALLBACK EnumCallback(LPCDIDEVICEINSTANCEW inst, LPVOID self);

  HWND hwnd_ = nullptr;
  Microsoft::WRL::ComPtr<IDirectInput8W> di_;
  std::vector<Device> devices_;
  std::vector<ButtonBinding> bindings_;
  bool capturing_ = false, captured_ = false;
  ButtonBinding capture_;
  ULONGLONG rescanAt_ = 0;
  bool enumerated_ = false;
};
