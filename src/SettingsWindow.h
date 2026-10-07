#pragma once
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <string>

class App;
struct ImGuiContext;

// The control panel (Dear ImGui). The window, its D3D device and the ImGui
// context only exist while it's open, so it costs nothing while you drive.
class SettingsWindow {
public:
  void Open(App& app, HINSTANCE inst);
  void Close();
  bool IsOpen() const { return hwnd_ != nullptr; }
  // Open, not minimised, and used in the last 2 minutes (the preview shouldn't run for an hour
  // because the window was left open behind something else).
  bool InUse() const;
  void Frame(); // build + present one UI frame (called from the app loop)

private:
  static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
  bool CreateDevice();
  void CreateRenderTarget();
  void ApplyDpi(float scale);

  void DrawUi();
  void DrawStatusBar();
  void DrawWidgetsTab();
  void DrawProfilesTab();
  void DrawGeneralTab();
  bool ProfileCombo(const char* label, std::string& value, float width);

  App* app_ = nullptr;
  HWND hwnd_ = nullptr;
  Microsoft::WRL::ComPtr<ID3D11Device> device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
  Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain_;
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv_;
  ImGuiContext* ctx_ = nullptr;
  UINT resizeW_ = 0, resizeH_ = 0;
  bool closeRequested_ = false;
  float dpiScale_ = 1.f, pendingDpi_ = 0.f;

  // UI state
  int selectedWidget_ = 0;
  char nameBuf_[48]{};
  std::string popupError_;
  int bindingAction_ = -1;   // wheel button being assigned
  mutable ULONGLONG lastActive_ = 0;
};
