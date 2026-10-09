#include "SettingsWindow.h"
#include "WheelButtons.h"
#include "Version.h"
#include "App.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include <shellapi.h>
#include <algorithm>
#include <cstdio>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

constexpr wchar_t kClassName[] = L"LmuOverlaySettings";
const ImVec4 kAccent{0.90f, 0.22f, 0.20f, 1.f};
const ImVec4 kGood{0.24f, 0.86f, 0.52f, 1.f};

ImVec4 WithAlpha(ImVec4 c, float a) { return {c.x, c.y, c.z, a}; }

void ApplyTheme(ImGuiStyle& st) {
  ImGui::StyleColorsDark(&st);
  st.WindowRounding = 0.f;
  st.ChildRounding = st.FrameRounding = st.PopupRounding = st.GrabRounding = st.TabRounding = 4.f;
  st.WindowPadding = {14, 12};
  st.FramePadding = {8, 5};
  st.ItemSpacing = {8, 7};
  st.ScrollbarSize = 12.f;
  ImVec4* c = st.Colors;
  c[ImGuiCol_WindowBg] = {0.08f, 0.09f, 0.11f, 1.f};
  c[ImGuiCol_ChildBg] = {0.10f, 0.11f, 0.13f, 1.f};
  c[ImGuiCol_PopupBg] = {0.11f, 0.12f, 0.14f, 1.f};
  c[ImGuiCol_Border] = {0.22f, 0.24f, 0.28f, 1.f};
  c[ImGuiCol_FrameBg] = {0.16f, 0.18f, 0.21f, 1.f};
  c[ImGuiCol_FrameBgHovered] = {0.20f, 0.22f, 0.26f, 1.f};
  c[ImGuiCol_FrameBgActive] = {0.24f, 0.27f, 0.31f, 1.f};
  c[ImGuiCol_Button] = {0.20f, 0.22f, 0.26f, 1.f};
  c[ImGuiCol_ButtonHovered] = WithAlpha(kAccent, 0.75f);
  c[ImGuiCol_ButtonActive] = kAccent;
  c[ImGuiCol_Header] = WithAlpha(kAccent, 0.35f);
  c[ImGuiCol_HeaderHovered] = WithAlpha(kAccent, 0.55f);
  c[ImGuiCol_HeaderActive] = WithAlpha(kAccent, 0.80f);
  c[ImGuiCol_CheckMark] = kAccent;
  c[ImGuiCol_SliderGrab] = WithAlpha(kAccent, 0.85f);
  c[ImGuiCol_SliderGrabActive] = kAccent;
  c[ImGuiCol_Tab] = {0.14f, 0.15f, 0.18f, 1.f};
  c[ImGuiCol_TabHovered] = WithAlpha(kAccent, 0.75f);
  c[ImGuiCol_TabSelected] = WithAlpha(kAccent, 0.60f);
  c[ImGuiCol_TabSelectedOverline] = kAccent;
  c[ImGuiCol_TitleBgActive] = {0.12f, 0.13f, 0.16f, 1.f};
}

void HelpMarker(const char* text) {
  if (!text) return;
  ImGui::SameLine();
  ImGui::TextDisabled("(?)");
  ImGui::SetItemTooltip("%s", text);
}

// One control per option type. Returns true when the user changed the value.
bool OptionControl(IniDoc& doc, const std::string& section, const OptionDef& d, float width) {
  double v = GetOptionValue(doc, section, d);
  bool changed = false;
  ImGui::PushID(d.key);
  switch (d.type) {
    case OptType::Bool: {
      bool b = v != 0;
      changed = ImGui::Checkbox(d.label, &b);
      v = b;
      break;
    }
    case OptType::Int: {
      int i = static_cast<int>(v);
      ImGui::SetNextItemWidth(width);
      if (d.max - d.min <= 200) changed = ImGui::SliderInt(d.label, &i, int(d.min), int(d.max));
      else changed = ImGui::DragInt(d.label, &i, 1.f, int(d.min), int(d.max));
      v = std::clamp<double>(i, d.min, d.max);
      break;
    }
    case OptType::Float: {
      float f = static_cast<float>(v);
      ImGui::SetNextItemWidth(width);
      changed = ImGui::SliderFloat(d.label, &f, float(d.min), float(d.max), "%.2f");
      v = std::clamp<double>(f, d.min, d.max);
      break;
    }
    case OptType::Color: {
      const uint32_t c = static_cast<uint32_t>(v);
      float col[4] = {((c >> 24) & 0xFF) / 255.f, ((c >> 16) & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, (c & 0xFF) / 255.f};
      ImGui::SetNextItemWidth(width);
      changed = ImGui::ColorEdit4(d.label, col, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoInputs);
      auto b = [](float f) { return static_cast<uint32_t>(std::clamp(f, 0.f, 1.f) * 255.f + 0.5f); };
      v = double((b(col[0]) << 24) | (b(col[1]) << 16) | (b(col[2]) << 8) | b(col[3]));
      break;
    }
    case OptType::Choice: {
      const auto list = SplitChoices(d.choices);
      int idx = std::clamp(static_cast<int>(v), 0, static_cast<int>(list.size()) - 1);
      ImGui::SetNextItemWidth(width);
      if (ImGui::BeginCombo(d.label, list[idx].c_str())) {
        for (int k = 0; k < static_cast<int>(list.size()); ++k)
          if (ImGui::Selectable(list[k].c_str(), k == idx)) { idx = k; changed = true; }
        ImGui::EndCombo();
      }
      v = idx;
      break;
    }
  }
  HelpMarker(d.help);
  ImGui::PopID();
  if (changed) SetOptionValue(doc, section, d, v);
  return changed;
}

} // namespace

// ----------------------------------------------------------------------------
// Window / device lifetime

void SettingsWindow::Open(App& app, HINSTANCE inst) {
  if (hwnd_) {
    if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
    SetForegroundWindow(hwnd_);
    return;
  }
  app_ = &app;
  closeRequested_ = false;
  lastActive_ = 0;

  WNDCLASSEXW wc{sizeof(wc)};
  wc.style = CS_CLASSDC;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = inst;
  wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
  wc.hIconSm = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                             GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = kClassName;
  RegisterClassExW(&wc);

  // Centre on the primary monitor (the game usually owns the other one).
  const HMONITOR mon = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO mi{sizeof(mi)};
  GetMonitorInfoW(mon, &mi);
  const float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(mon);
  const int w = static_cast<int>(1040 * scale), h = static_cast<int>(720 * scale);
  const int x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - w) / 2;
  const int y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - h) / 2;
  hwnd_ = CreateWindowExW(0, kClassName, L"LMU Overlay \u2014 Settings", WS_OVERLAPPEDWINDOW, x, y, w, h, nullptr,
                          nullptr, inst, this);
  if (!hwnd_ || !CreateDevice()) { Close(); return; }

  IMGUI_CHECKVERSION();
  ctx_ = ImGui::CreateContext();
  ImGui::SetCurrentContext(ctx_);
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr; // nothing to persist: all settings live in our own files
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  if (!io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 17.f)) io.Fonts->AddFontDefault();
  ApplyDpi(ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd_));
  ImGui_ImplWin32_Init(hwnd_);
  ImGui_ImplDX11_Init(device_.Get(), context_.Get());

  ShowWindow(hwnd_, SW_SHOWNORMAL);
  SetForegroundWindow(hwnd_);
}

void SettingsWindow::Close() {
  if (ctx_) {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext(ctx_);
    ctx_ = nullptr;
  }
  rtv_.Reset();
  swapChain_.Reset();
  context_.Reset();
  device_.Reset();
  if (hwnd_) {
    HWND h = hwnd_;
    hwnd_ = nullptr; // IsOpen() turns false before WM_DESTROY arrives
    DestroyWindow(h);
  }
}

bool SettingsWindow::CreateDevice() {
  DXGI_SWAP_CHAIN_DESC sd{};
  sd.BufferCount = 2;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = hwnd_;
  sd.SampleDesc.Count = 1;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
  if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                           &sd, &swapChain_, &device_, nullptr, &context_)))
    return false;
  CreateRenderTarget();
  return rtv_ != nullptr;
}

void SettingsWindow::CreateRenderTarget() {
  Microsoft::WRL::ComPtr<ID3D11Texture2D> back;
  if (SUCCEEDED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&back)))) device_->CreateRenderTargetView(back.Get(), nullptr, &rtv_);
}

void SettingsWindow::ApplyDpi(float scale) {
  dpiScale_ = scale;
  ImGuiStyle& st = ImGui::GetStyle();
  st = ImGuiStyle();
  ApplyTheme(st);
  st.ScaleAllSizes(scale);
  st.FontScaleDpi = scale;
}

LRESULT CALLBACK SettingsWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_NCCREATE)
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
  auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self && self->ctx_ && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return TRUE;
  if (self) {
    switch (msg) {
      case WM_SIZE:
        if (wp != SIZE_MINIMIZED) { self->resizeW_ = LOWORD(lp); self->resizeH_ = HIWORD(lp); }
        return 0;
      case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize = {static_cast<LONG>(780 * self->dpiScale_), static_cast<LONG>(520 * self->dpiScale_)};
        return 0;
      }
      case WM_DPICHANGED: {
        self->pendingDpi_ = HIWORD(wp) / 96.f;
        const RECT* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
      }
      case WM_SYSCOMMAND:
        if ((wp & 0xfff0) == SC_KEYMENU) return 0; // no Alt menu beep
        break;
      case WM_CLOSE:
        self->closeRequested_ = true; // destroyed between frames, not inside a message handler
        return 0;
    }
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

bool SettingsWindow::InUse() const {
  if (!hwnd_ || IsIconic(hwnd_)) return false;
  const ULONGLONG now = GetTickCount64();
  if (GetForegroundWindow() == hwnd_ || lastActive_ == 0) lastActive_ = now;
  return now - lastActive_ < 120000;
}

void SettingsWindow::Frame() {
  if (!hwnd_) return;
  if (closeRequested_) { Close(); return; }
  if (IsIconic(hwnd_)) return;
  if (pendingDpi_ > 0.f) { ApplyDpi(pendingDpi_); pendingDpi_ = 0.f; }
  if (resizeW_ && resizeH_) {
    rtv_.Reset();
    swapChain_->ResizeBuffers(0, resizeW_, resizeH_, DXGI_FORMAT_UNKNOWN, 0);
    resizeW_ = resizeH_ = 0;
    CreateRenderTarget();
  }

  ImGui_ImplDX11_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();
  DrawUi();
  ImGui::Render();

  const float clear[4] = {0.08f, 0.09f, 0.11f, 1.f};
  context_->OMSetRenderTargets(1, rtv_.GetAddressOf(), nullptr);
  context_->ClearRenderTargetView(rtv_.Get(), clear);
  ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
  swapChain_->Present(1, 0);
}

// ----------------------------------------------------------------------------
// UI

void SettingsWindow::DrawUi() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(vp->WorkSize);
  ImGui::Begin("##root", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  DrawStatusBar();
  if (ImGui::BeginTabBar("tabs")) {
    if (ImGui::BeginTabItem("Widgets")) { DrawWidgetsTab(); ImGui::EndTabItem(); }
    if (ImGui::BeginTabItem("Profiles")) { DrawProfilesTab(); ImGui::EndTabItem(); }
    if (ImGui::BeginTabItem("General")) { DrawGeneralTab(); ImGui::EndTabItem(); }
    ImGui::EndTabBar();
  }
  ImGui::End();
}

void SettingsWindow::DrawStatusBar() {
  App& app = *app_;
  Overlay& ov = app.GetOverlay();

  if (ImGui::Button(ov.EditMode() ? "Done moving" : "Move widgets")) ov.SetEditMode(!ov.EditMode());
  ImGui::SetItemTooltip("Drag widgets with the mouse on the overlay's monitor (Ctrl+Alt+E)");
  ImGui::SameLine();
  bool preview = app.Preview();
  if (ImGui::Checkbox("Preview", &preview)) app.SetPreview(preview);
  ImGui::SetItemTooltip("Show the overlay while this window is open.\nUses demo data when you're not on track.");
  ImGui::SameLine();
  bool hidden = ov.UserHidden();
  if (ImGui::Checkbox("Hide overlay", &hidden)) ov.SetUserHidden(hidden);
  ImGui::SetItemTooltip("Ctrl+Alt+O");

  ImGui::SameLine(0, 24 * dpiScale_);
  if (app.LiveConnected()) {
    ImGui::TextColored(kGood, "LMU connected");
    ImGui::SameLine();
    ImGui::TextDisabled("%s%s", SessionKindName(app.LiveSession()), app.LiveOnTrack() ? ", on track" : ", in garage / menus");
  } else {
    ImGui::TextDisabled("LMU not running");
  }

  const char* quit = "Quit overlay";
  const float quitW = ImGui::CalcTextSize(quit).x + ImGui::GetStyle().FramePadding.x * 2;
  ImGui::SameLine();
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - quitW));
  if (ImGui::Button(quit)) app.Quit();
  ImGui::SetItemTooltip("Closes the overlay completely (Ctrl+Alt+Q).\nClosing this window keeps the overlay running in the tray.");
  ImGui::Spacing();
}

bool SettingsWindow::ProfileCombo(const char* label, std::string& value, float width) {
  bool changed = false;
  ImGui::SetNextItemWidth(width);
  const bool exists = app_->Profiles().Exists(value);
  const std::string preview = exists ? value : value + " (missing)";
  if (ImGui::BeginCombo(label, preview.c_str())) {
    for (const std::string& name : app_->Profiles().List())
      if (ImGui::Selectable(name.c_str(), name == value)) { value = name; changed = true; }
    ImGui::EndCombo();
  }
  return changed;
}

void SettingsWindow::DrawWidgetsTab() {
  App& app = *app_;
  const float s = dpiScale_;

  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Profile");
  ImGui::SameLine();
  std::string sel = app.ProfileName();
  if (ProfileCombo("##profile", sel, 220 * s)) app.SelectProfile(sel);
  ImGui::SameLine();
  ImGui::TextDisabled("Changes are saved automatically and shown live on the overlay.");

  IniDoc& doc = app.ProfileDoc();
  const auto types = WidgetTypes();
  selectedWidget_ = std::clamp(selectedWidget_, 0, static_cast<int>(types.size()) - 1);

  ImGui::BeginChild("list", ImVec2(250 * s, 0), ImGuiChildFlags_Borders);
  ImGui::TextDisabled("Tick to show, click to configure");
  ImGui::Separator();
  for (int i = 0; i < static_cast<int>(types.size()); ++i) {
    const WidgetType& t = types[i];
    const std::vector<OptionDef> schema = FullSchema(t);
    ImGui::PushID(i);
    bool on = GetOptionValue(doc, t.id, schema[0]) != 0; // schema[0] is "enabled"
    if (ImGui::Checkbox("##on", &on)) {
      SetOptionValue(doc, t.id, schema[0], on);
      app.ProfileEdited();
    }
    ImGui::SameLine();
    if (ImGui::Selectable(t.name, selectedWidget_ == i, 0, ImVec2(0, ImGui::GetFrameHeight()))) selectedWidget_ = i;
    ImGui::PopID();
  }
  ImGui::EndChild();
  ImGui::SameLine();

  ImGui::BeginChild("options", ImVec2(0, 0), ImGuiChildFlags_Borders);
  const WidgetType& t = types[selectedWidget_];
  ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.35f);
  ImGui::TextUnformatted(t.name);
  ImGui::PopFont();
  ImGui::TextWrapped("%s", t.description);

  const std::vector<OptionDef> schema = FullSchema(t);
  const float width = 240 * s;
  bool edited = false;
  for (const char* group : {"Layout", "Options", "Colours"}) {
    bool header = false;
    for (const OptionDef& d : schema) {
      if (strcmp(d.group, group) != 0 || strcmp(d.key, "enabled") == 0) continue;
      if (!header) { ImGui::SeparatorText(group); header = true; }
      edited |= OptionControl(doc, t.id, d, width);
    }
  }

  ImGui::Spacing();
  ImGui::Separator();
  if (ImGui::Button("Reset to defaults")) {
    for (const OptionDef& d : schema)
      if (strcmp(d.key, "x") && strcmp(d.key, "y") && strcmp(d.key, "enabled")) SetOptionValue(doc, t.id, d, d.def);
    edited = true;
  }
  ImGui::SetItemTooltip("Resets everything for this widget except whether it's shown and where it is");
  ImGui::SameLine();
  if (ImGui::Button("Copy to all profiles")) {
    // Same look in every session; each profile keeps its own position and on/off state.
    for (const std::string& name : app.Profiles().List()) {
      if (name == app.ProfileName()) continue;
      IniDoc other;
      if (!app.Profiles().Load(name, other)) continue;
      for (const OptionDef& d : schema)
        if (strcmp(d.key, "x") && strcmp(d.key, "y") && strcmp(d.key, "enabled"))
          SetOptionValue(other, t.id, d, GetOptionValue(doc, t.id, d));
      app.Profiles().Save(name, other);
    }
  }
  ImGui::SetItemTooltip("Copies this widget's options (not its position or on/off) to every other profile");
  ImGui::EndChild();

  if (edited) app.ProfileEdited();
}

void SettingsWindow::DrawProfilesTab() {
  App& app = *app_;
  GeneralSettings& gs = app.Settings();
  ProfileManager& pm = app.Profiles();
  const float s = dpiScale_;
  const std::vector<std::string> names = pm.List();
  const std::string current = app.ProfileName();

  ImGui::BeginChild("profiles", ImVec2(300 * s, 0), ImGuiChildFlags_Borders);
  ImGui::TextDisabled("Profiles (click to edit)");
  ImGui::Separator();
  for (const std::string& name : names) {
    std::string tags;
    for (int k = 0; k < static_cast<int>(SessionKind::Count); ++k)
      if (gs.autoSwitch && gs.profileFor[k] == name) tags += std::string(tags.empty() ? "" : ", ") + SessionKindName(SessionKind(k));
    if (!gs.autoSwitch && gs.manualProfile == name) tags = "always";
    if (ImGui::Selectable(name.c_str(), name == current)) app.SelectProfile(name);
    if (!tags.empty()) {
      ImGui::SameLine(150 * s);
      ImGui::TextDisabled("%s", tags.c_str());
    }
  }
  ImGui::Spacing();
  ImGui::Separator();

  auto openNamePopup = [this](const char* id, const std::string& initial) {
    snprintf(nameBuf_, sizeof(nameBuf_), "%s", initial.c_str());
    popupError_.clear();
    ImGui::OpenPopup(id);
  };
  if (ImGui::Button("New")) openNamePopup("New profile", "");
  ImGui::SetItemTooltip("Create a profile with default settings");
  ImGui::SameLine();
  if (ImGui::Button("Duplicate")) openNamePopup("Duplicate profile", current + " copy");
  ImGui::SameLine();
  if (ImGui::Button("Rename")) openNamePopup("Rename profile", current);
  ImGui::SameLine();
  ImGui::BeginDisabled(names.size() <= 1);
  if (ImGui::Button("Delete")) ImGui::OpenPopup("Delete profile");
  ImGui::EndDisabled();

  // Name prompt shared by New / Duplicate / Rename.
  for (const char* id : {"New profile", "Duplicate profile", "Rename profile"}) {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) continue;
    ImGui::TextUnformatted("Name (letters, numbers, spaces, - and _):");
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(320 * s);
    const bool enter = ImGui::InputText("##name", nameBuf_, sizeof(nameBuf_), ImGuiInputTextFlags_EnterReturnsTrue);
    if (!popupError_.empty()) ImGui::TextColored(kAccent, "%s", popupError_.c_str());
    if (ImGui::Button("OK", ImVec2(120 * s, 0)) || enter) {
      const std::string name = nameBuf_;
      bool ok = false;
      if (!ProfileManager::ValidName(name)) popupError_ = "Invalid name.";
      else if (pm.Exists(name)) popupError_ = "A profile with that name already exists.";
      else if (strcmp(id, "New profile") == 0) ok = pm.Create(name, IniDoc{});
      else if (strcmp(id, "Duplicate profile") == 0) ok = pm.Create(name, app.ProfileDoc());
      else if ((ok = pm.Rename(current, name))) app.ProfileRenamed(current, name);
      if (ok) {
        if (strcmp(id, "Rename profile") != 0) app.SelectProfile(name);
        ImGui::CloseCurrentPopup();
      } else if (popupError_.empty()) {
        popupError_ = "Could not write the profile file.";
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120 * s, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal("Delete profile", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Delete profile \"%s\"? This can't be undone.", current.c_str());
    if (ImGui::Button("Delete", ImVec2(120 * s, 0))) {
      std::string next;
      for (const std::string& n : names) if (n != current) { next = n; break; }
      app.SelectProfile(next); // switch first so a pending save can't recreate the file
      pm.Delete(current);
      bool remapped = false;
      for (std::string& p : gs.profileFor) if (p == current) { p = next; remapped = true; }
      if (gs.manualProfile == current) { gs.manualProfile = next; remapped = true; }
      if (remapped) app.SettingsChanged();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120 * s, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  ImGui::EndChild();
  ImGui::SameLine();

  ImGui::BeginChild("mapping", ImVec2(0, 0), ImGuiChildFlags_Borders);
  ImGui::SeparatorText("Which profile is used");
  bool changed = ImGui::Checkbox("Switch automatically by session type", &gs.autoSwitch);
  if (gs.autoSwitch) {
    for (int k = 0; k < static_cast<int>(SessionKind::Count); ++k) {
      ImGui::PushID(k);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(SessionKindName(SessionKind(k)));
      ImGui::SameLine(130 * s);
      changed |= ProfileCombo("##map", gs.profileFor[k], 220 * s);
      ImGui::PopID();
    }
    ImGui::TextDisabled("Practice also covers test day and warm-up.");
  } else {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Always use");
    ImGui::SameLine(130 * s);
    changed |= ProfileCombo("##manual", gs.manualProfile, 220 * s);
  }
  if (changed) app.SettingsChanged();

  ImGui::Spacing();
  ImGui::SeparatorText("Right now");
  if (app.LiveConnected())
    ImGui::Text("LMU session: %s  \xE2\x86\x92  profile \"%s\"", SessionKindName(app.LiveSession()),
                gs.autoSwitch ? gs.profileFor[static_cast<int>(app.LiveSession())].c_str() : gs.manualProfile.c_str());
  else
    ImGui::TextDisabled("LMU isn't running.");
  ImGui::TextWrapped("While this window is open the overlay shows the profile you're editing (\"%s\"), so you can see "
                     "your changes. When you close it, the profile for the current session is used again.",
                     current.c_str());
  ImGui::EndChild();
}

void SettingsWindow::DrawGeneralTab() {
  App& app = *app_;
  GeneralSettings& gs = app.Settings();
  const float s = dpiScale_, width = 260 * s;
  bool changed = false;

  ImGui::BeginChild("general", ImVec2(0, 0), ImGuiChildFlags_Borders);
  ImGui::SeparatorText("Race plan (practice and qualifying)");
  changed |= ImGui::Checkbox("Plan the strategy for this race", &gs.planEnabled);
  HelpMarker("Practice and qualifying don't know which race comes next. Set its length here and the\n"
             "Strategy widget shows the plan from your pace and fuel / energy use in this session:\n"
             "laps, what to start with, how many stops, when and how much to add.\n"
             "In a race session the real race is used instead.");
  ImGui::BeginDisabled(!gs.planEnabled);
  {
    int kind = gs.planByLaps ? 1 : 0;
    changed |= ImGui::RadioButton("Time", &kind, 0);
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Laps", &kind, 1);
    gs.planByLaps = kind == 1;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120 * s);
    if (gs.planByLaps) {
      if (ImGui::InputInt("laps##plan", &gs.planLaps, 1, 5)) { gs.planLaps = std::clamp(gs.planLaps, 1, 999); changed = true; }
    } else {
      if (ImGui::InputInt("minutes##plan", &gs.planMinutes, 5, 30)) { gs.planMinutes = std::clamp(gs.planMinutes, 5, 1440); changed = true; }
    }
    const RacePlan& plan = app.LivePlan();
    if (plan.valid && plan.ready)
      ImGui::TextDisabled("Now: about %.0f laps, %s", plan.totalLaps,
                          plan.stops == 0 ? "no pit stop" : plan.stops == 1 ? "1 pit stop" : (std::to_string(plan.stops) + " pit stops").c_str());
    else if (plan.valid)
      ImGui::TextDisabled("Drive a clean lap to measure your pace and fuel use.");
  }
  ImGui::EndDisabled();

  ImGui::SeparatorText("Display");
  {
    const std::vector<MonitorInfo> mons = EnumMonitors();
    auto describe = [](const MonitorInfo& m) {
      char buf[128];
      const std::string dev = m.device.substr(m.device.find_last_of('\\') + 1);
      snprintf(buf, sizeof(buf), "%s  %ldx%ld at (%ld, %ld)%s", dev.c_str(), m.rect.right - m.rect.left,
               m.rect.bottom - m.rect.top, m.rect.left, m.rect.top, m.primary ? "  primary" : "");
      return std::string(buf);
    };
    std::string preview = "Automatic (follow LMU's window)";
    for (const MonitorInfo& m : mons) if (m.device == gs.monitor) preview = describe(m);
    ImGui::SetNextItemWidth(width * 1.5f);
    if (ImGui::BeginCombo("Monitor", preview.c_str())) {
      if (ImGui::Selectable("Automatic (follow LMU's window)", gs.monitor == "auto")) { gs.monitor = "auto"; changed = true; }
      for (const MonitorInfo& m : mons)
        if (ImGui::Selectable(describe(m).c_str(), gs.monitor == m.device)) { gs.monitor = m.device; changed = true; }
      ImGui::EndCombo();
    }
    HelpMarker("Widget positions are relative to the top-left corner of this monitor.\n"
               "Automatic uses the monitor LMU is on, and remembers it while the game is closed.");
    ImGui::TextDisabled("Overlay is on %s", app.OverlayMonitor().c_str());
  }
  ImGui::SetNextItemWidth(width);
  changed |= ImGui::SliderFloat("Global scale", &gs.scale, 0.5f, 3.f, "%.2f");
  HelpMarker("Multiplies every widget's own scale.");
  changed |= ImGui::Checkbox("Show only while driving", &gs.onlyOnTrack);
  HelpMarker("Hide the overlay in menus and on the garage monitor.");

  ImGui::SeparatorText("Performance");
  ImGui::SetNextItemWidth(width);
  changed |= ImGui::SliderInt("Data rate (Hz)", &gs.pollHz, 10, 120);
  HelpMarker("How often data is copied from LMU. Each copy briefly takes the game's data lock: 30-60 is plenty.");
  ImGui::SetNextItemWidth(width);
  changed |= ImGui::SliderInt("Max redraws per second", &gs.maxFps, 10, 144);
  HelpMarker("Upper limit; widgets only redraw when what they show changes.");
  changed |= ImGui::Checkbox("Run below normal priority", &gs.lowerPriority);
  HelpMarker("Recommended: LMU always gets the CPU first.");

  bool customCores = gs.affinityMask != 0;
  if (ImGui::Checkbox("Restrict to specific CPU threads", &customCores)) {
    gs.affinityMask = customCores ? (1ull << (GetActiveProcessorCount(ALL_PROCESSOR_GROUPS) - 1)) : 0;
    changed = true;
  }
  HelpMarker("Advanced. Keep the overlay off the threads LMU uses most (usually the first ones).");
  if (customCores) {
    const int n = std::min<int>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS), 64);
    ImGui::Indent();
    for (int i = 0; i < n; ++i) {
      bool on = (gs.affinityMask >> i) & 1ull;
      char label[16];
      snprintf(label, sizeof(label), "%d", i);
      if (i % 8) ImGui::SameLine();
      if (ImGui::Checkbox(label, &on)) {
        const unsigned long long next = on ? gs.affinityMask | (1ull << i) : gs.affinityMask & ~(1ull << i);
        if (next) { gs.affinityMask = next; changed = true; } // at least one thread
      }
    }
    ImGui::Unindent();
  }

  ImGui::SeparatorText("LMU REST API");
  changed |= ImGui::Checkbox("Read damage details from LMU (aero, suspension, repair time)", &gs.restApi);
  HelpMarker("LMU only exposes these through its local web API (localhost:6397). The overlay asks\n"
             "on a background thread, right after an impact and every few seconds while driving.");
  ImGui::BeginDisabled(!gs.restApi);
  ImGui::SetNextItemWidth(width);
  changed |= ImGui::SliderInt("Refresh every (s)", &gs.restIntervalS, 2, 60);
  ImGui::EndDisabled();

  ImGui::SeparatorText("Startup");
  changed |= ImGui::Checkbox("Open this window when the overlay starts", &gs.openSettingsOnStart);
  changed |= ImGui::Checkbox("Show a tip notification at startup (when this window doesn't open)", &gs.startupTip);

  ImGui::SeparatorText("Wheel buttons");
  ImGui::TextDisabled("Use a free button on your wheel or button box (one LMU doesn't use).");
  {
    WheelButtons& wheel = app.Wheel();
    ButtonBinding got;
    if (bindingAction_ >= 0 && wheel.TakeCaptured(got)) {
      gs.wheelButtons[bindingAction_] = got.Serialize();
      bindingAction_ = -1;
      changed = true;
    }
    if (bindingAction_ >= 0 && !wheel.Capturing()) bindingAction_ = -1;
    for (int i = 0; i < GeneralSettings::kWheelActions; ++i) {
      ImGui::PushID(i);
      const ButtonBinding b = ButtonBinding::Parse(gs.wheelButtons[i]);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(WheelActionLabel(static_cast<WheelAction>(i)));
      ImGui::SameLine(230 * dpiScale_);
      if (bindingAction_ == i) {
        ImGui::TextColored(ImVec4(1.f, 0.76f, 0.24f, 1.f), "press a button on your wheel...");
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel")) { wheel.CancelCapture(); bindingAction_ = -1; }
      } else {
        ImGui::BeginDisabled(bindingAction_ >= 0);
        if (ImGui::SmallButton("Set")) { bindingAction_ = i; wheel.StartCapture(); }
        ImGui::SameLine();
        ImGui::BeginDisabled(!b.Valid());
        if (ImGui::SmallButton("Clear")) { gs.wheelButtons[i].clear(); changed = true; }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (b.Valid()) ImGui::TextUnformatted(wheel.Describe(b).c_str());
        else ImGui::TextDisabled("not set");
      }
      ImGui::PopID();
    }
  }

  ImGui::SeparatorText("Updates");
  {
    Updater& up = app.Updates();
    const Updater::State st = up.Get();
    ImGui::Text("This version: %s", LMU_OVERLAY_VERSION);
    changed |= ImGui::Checkbox("Check for updates when the overlay starts", &gs.updateCheckAtStart);
    char repoBuf[128];
    strncpy_s(repoBuf, gs.updateRepo.c_str(), _TRUNCATE);
    ImGui::SetNextItemWidth(260 * dpiScale_);
    if (ImGui::InputTextWithHint("GitHub repository", LMU_OVERLAY_REPO[0] ? LMU_OVERLAY_REPO : "owner/name", repoBuf,
                                 sizeof(repoBuf))) {
      gs.updateRepo = repoBuf;
      changed = true;
    }
    const bool busy = st.phase == Updater::Phase::Checking || st.phase == Updater::Phase::Downloading;
    ImGui::BeginDisabled(busy || app.UpdateRepo().empty());
    if (ImGui::Button("Check now")) up.Check(app.UpdateRepo());
    ImGui::EndDisabled();
    ImGui::SameLine();
    switch (st.phase) {
      case Updater::Phase::Idle: ImGui::TextDisabled("not checked yet"); break;
      case Updater::Phase::Checking: ImGui::TextDisabled("checking..."); break;
      case Updater::Phase::UpToDate: ImGui::TextColored(ImVec4(0.24f, 0.86f, 0.52f, 1.f), "up to date"); break;
      case Updater::Phase::Downloading: ImGui::TextDisabled("downloading v%s...", st.latest.c_str()); break;
      case Updater::Phase::Ready: ImGui::TextDisabled("restarting..."); break;
      case Updater::Phase::Failed: ImGui::TextColored(ImVec4(1.f, 0.35f, 0.35f, 1.f), "%s", st.error.c_str()); break;
      case Updater::Phase::Available:
        ImGui::TextColored(ImVec4(1.f, 0.76f, 0.24f, 1.f), "version %s available", st.latest.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Update now")) up.Install();
        if (!st.notes.empty()) ImGui::TextWrapped("%s", st.notes.c_str());
        break;
    }
    ImGui::TextDisabled("Updating keeps your settings, profiles, records and logs.");
  }

  ImGui::SeparatorText("Hotkeys");
  ImGui::BulletText("Ctrl+Alt+E  move widgets (drag them, press again to finish)");
  ImGui::BulletText("Ctrl+Alt+O  show / hide the overlay");
  ImGui::BulletText("Ctrl+Alt+D  switch the delta reference (your best / session / all-time / lobby fastest)");
  ImGui::BulletText("Ctrl+Alt+P  next profile (until the session type changes)");
  ImGui::BulletText("Ctrl+Alt+Q  quit");
  ImGui::BulletText("Tray icon: double-click for this window, right-click for the menu");

  ImGui::SeparatorText("Files");
  if (ImGui::Button("Open settings folder"))
    ShellExecuteW(nullptr, L"open", app.Dir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  ImGui::SameLine();
  ImGui::TextDisabled("settings.ini and the profiles folder");
  ImGui::EndChild();

  if (changed) app.SettingsChanged();
}
