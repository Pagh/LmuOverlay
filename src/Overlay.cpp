#include "Overlay.h"
#include <dxgi1_3.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>

namespace {
constexpr wchar_t kClassName[] = L"LmuOverlayWindow";
constexpr DWORD kExStyle = WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
                           WS_EX_LAYERED | WS_EX_TRANSPARENT;
}

bool Overlay::Create(HINSTANCE inst, const GeneralSettings& gs, MessageHook hook, MoveCallback onMoved) {
  gs_ = gs;
  hook_ = std::move(hook);
  onMoved_ = std::move(onMoved);
  visible_ = editMode_ = false;
  drag_ = nullptr;

  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursorW(nullptr, IDC_SIZEALL);
  wc.lpszClassName = kClassName;
  RegisterClassExW(&wc); // may already exist after a reload

  // Starts on the primary monitor; the app moves it with SetMonitorRect().
  // With no redirection bitmap the window itself costs nothing; only widget visuals are composed.
  rect_ = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
  hwnd_ = CreateWindowExW(kExStyle, kClassName, L"LMU Overlay", WS_POPUP, rect_.left, rect_.top,
                          rect_.right - rect_.left, rect_.bottom - rect_.top, nullptr, nullptr, inst, this);
  if (!hwnd_) return false;
  // Layered + transparent = mouse clicks pass straight through to the game.
  SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
  return CreateDevices();
}

void Overlay::Destroy() {
  ReleaseDevices();
  if (hwnd_) { DestroyWindow(hwnd_); hwnd_ = nullptr; }
  widgets_.clear();
}

bool Overlay::CreateDevices() {
  // BGRA support is required for Direct2D interop.
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                               D3D11_SDK_VERSION, &d3d_, nullptr, nullptr)))
    return false;
  ComPtr<IDXGIDevice1> dxgi;
  if (FAILED(d3d_.As(&dxgi))) return false;
  dxgi->SetMaximumFrameLatency(1);

  D2D1_FACTORY_OPTIONS opts{};
  if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &opts,
                               reinterpret_cast<void**>(d2dFactory_.ReleaseAndGetAddressOf()))))
    return false;
  if (FAILED(d2dFactory_->CreateDevice(dxgi.Get(), &d2dDevice_))) return false;
  if (FAILED(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &resourceDc_))) return false;
  if (!dwrite_ && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                             reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()))))
    return false;
  if (!painter_.Init(resourceDc_.Get(), dwrite_.Get())) return false;

  if (FAILED(DCompositionCreateDevice3(d2dDevice_.Get(), IID_PPV_ARGS(&dcomp_)))) return false;
  if (FAILED(dcomp_->CreateTargetForHwnd(hwnd_, TRUE, &target_))) return false;
  if (FAILED(dcomp_->CreateVisual(&root_))) return false;
  if (FAILED(target_->SetRoot(root_.Get()))) return false;
  if (!CreateSlots()) return false;
  UpdateBackdrop();

  deviceLost_ = false;
  forceRedraw_ = true;
  return SUCCEEDED(dcomp_->Commit());
}

void Overlay::ReleaseDevices() {
  ClearSlots();
  backdropSurface_.Reset();
  backdrop_.Reset();
  root_.Reset();
  target_.Reset();
  dcomp_.Reset();
  resourceDc_.Reset();
  d2dDevice_.Reset();
  d2dFactory_.Reset();
  d3d_.Reset();
}

void Overlay::ClearSlots() {
  if (drag_) { ReleaseCapture(); drag_ = nullptr; }
  if (root_)
    for (Slot& s : slots_) root_->RemoveVisual(s.visual.Get());
  slots_.clear();
}

bool Overlay::CreateSlots() {
  ClearSlots();
  for (auto& w : widgets_) {
    Slot s{w.get()};
    const UINT pw = std::max(1u, static_cast<UINT>(std::ceil(w->Width() * ScaleOf(*w))));
    const UINT ph = std::max(1u, static_cast<UINT>(std::ceil(w->Height() * ScaleOf(*w))));
    if (FAILED(dcomp_->CreateVisual(&s.visual))) return false;
    if (FAILED(dcomp_->CreateSurface(pw, ph, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &s.surface)))
      return false;
    s.visual->SetContent(s.surface.Get());
    const POINT p = ClampToMonitor(*w, w->X(), w->Y());
    s.visual->SetOffsetX(static_cast<float>(p.x));
    s.visual->SetOffsetY(static_cast<float>(p.y));
    root_->AddVisual(s.visual.Get(), TRUE, nullptr); // above the edit-mode backdrop
    w->nextUpdate = 0.0;
    slots_.push_back(std::move(s));
  }
  forceRedraw_ = true;
  return true;
}

POINT Overlay::ClampToMonitor(const Widget& w, int x, int y) const {
  const int width = static_cast<int>(rect_.right - rect_.left), height = static_cast<int>(rect_.bottom - rect_.top);
  const int ww = static_cast<int>(w.Width() * ScaleOf(w));
  return {std::clamp(x, std::min(0, 40 - ww), std::max(0, width - 40)), std::clamp(y, 0, std::max(0, height - 40))};
}

void Overlay::ApplyPositions() {
  for (Slot& s : slots_) {
    const POINT p = ClampToMonitor(*s.widget, s.widget->X(), s.widget->Y());
    s.visual->SetOffsetX(static_cast<float>(p.x));
    s.visual->SetOffsetY(static_cast<float>(p.y));
  }
  if (dcomp_) dcomp_->Commit();
}

void Overlay::SetWidgets(WidgetList widgets) {
  ClearSlots(); // before the old widgets die: slots point at them
  widgets_ = std::move(widgets);
  if (dcomp_ && CreateSlots()) dcomp_->Commit();
  else deviceLost_ = true;
}

void Overlay::SetSettings(const GeneralSettings& gs) {
  const bool rescale = gs.scale != gs_.scale;
  gs_ = gs;
  if (rescale && dcomp_ && CreateSlots()) dcomp_->Commit();
}

void Overlay::SetMonitorRect(const RECT& rc) {
  if (EqualRect(&rc, &rect_) || !hwnd_) return;
  rect_ = rc;
  SetWindowPos(hwnd_, HWND_TOPMOST, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, SWP_NOACTIVATE);
  ApplyPositions();
  if (backdrop_) { // edit-mode backdrop is sized to the monitor
    root_->RemoveVisual(backdrop_.Get());
    backdrop_.Reset();
    backdropSurface_.Reset();
    UpdateBackdrop();
    dcomp_->Commit();
  }
}

bool Overlay::DrawWidget(Slot& s) {
  POINT off{};
  ComPtr<ID2D1DeviceContext> dc;
  HRESULT hr = s.surface->BeginDraw(nullptr, IID_PPV_ARGS(&dc), &off);
  if (FAILED(hr)) {
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == D2DERR_RECREATE_TARGET) deviceLost_ = true;
    return false;
  }
  const float scale = ScaleOf(*s.widget);
  // The surface may live inside a shared atlas: clip to our rect before clearing.
  const float w = std::ceil(s.widget->Width() * scale), h = std::ceil(s.widget->Height() * scale);
  dc->SetTransform(D2D1::Matrix3x2F::Identity());
  dc->PushAxisAlignedClip(D2D1::RectF(static_cast<float>(off.x), static_cast<float>(off.y), off.x + w, off.y + h),
                          D2D1_ANTIALIAS_MODE_ALIASED);
  dc->Clear(D2D1::ColorF(0, 0, 0, 0));
  painter_.Begin(dc.Get(), off, scale, s.widget->Background(), editMode_);
  s.widget->Draw(painter_);
  painter_.End();
  dc->PopAxisAlignedClip();
  hr = s.surface->EndDraw();
  if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) deviceLost_ = true;
  return SUCCEEDED(hr);
}

void Overlay::SetVisible(bool v) {
  if (v == visible_) return;
  visible_ = v;
  if (v) {
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    // The game can push itself above us when it takes focus; re-assert on show.
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    forceRedraw_ = true;
  } else {
    ShowWindow(hwnd_, SW_HIDE);
  }
}

int Overlay::Tick(const Model& m, double now, bool forceVisible) {
  if (deviceLost_) {
    ReleaseDevices();
    if (!CreateDevices()) return 0; // try again next tick
  }

  const bool wantVisible =
      !userHidden_ && (editMode_ || forceVisible || (m.connected && (m.onTrack || !gs_.onlyOnTrack)));
  SetVisible(wantVisible);
  if (!visible_) return 0;

  int drawn = 0;
  for (auto& s : slots_) {
    Widget& w = *s.widget;
    if (!forceRedraw_ && now < w.nextUpdate) continue;
    w.nextUpdate = now + w.Interval();
    const bool changed = w.Prepare(m);
    if ((changed || forceRedraw_) && DrawWidget(s)) ++drawn;
  }
  forceRedraw_ = false;
  if (drawn) dcomp_->Commit();
  return drawn;
}

void Overlay::UpdateBackdrop() {
  if (editMode_ && !backdrop_ && dcomp_) {
    const LONG w = rect_.right - rect_.left, h = rect_.bottom - rect_.top;
    if (FAILED(dcomp_->CreateVisual(&backdrop_)) ||
        FAILED(dcomp_->CreateSurface(w, h, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &backdropSurface_))) {
      backdrop_.Reset();
      return;
    }
    POINT off{};
    ComPtr<ID2D1DeviceContext> dc;
    if (SUCCEEDED(backdropSurface_->BeginDraw(nullptr, IID_PPV_ARGS(&dc), &off))) {
      dc->SetTransform(D2D1::Matrix3x2F::Identity());
      dc->PushAxisAlignedClip(D2D1::RectF(float(off.x), float(off.y), float(off.x + w), float(off.y + h)),
                              D2D1_ANTIALIAS_MODE_ALIASED);
      dc->Clear(D2D1::ColorF(0, 0, 0, 0.35f));
      painter_.Begin(dc.Get(), off, 1.f, Col::Panel, false);
      painter_.FillRounded(w / 2.f - 280, 16, 560, 34, 6, Col::Rgb(0x15181E, 0.95f));
      painter_.Text(w / 2.f - 280, 16, 560, 34, L"EDIT MODE  ·  drag widgets  ·  Ctrl+Alt+E to finish",
                    Font::TextBold, Col::EditOutline, Align::Center);
      painter_.End();
      dc->PopAxisAlignedClip();
      backdropSurface_->EndDraw();
    }
    backdrop_->SetContent(backdropSurface_.Get());
    root_->AddVisual(backdrop_.Get(), FALSE, nullptr); // behind the widgets
  } else if (!editMode_ && backdrop_) {
    root_->RemoveVisual(backdrop_.Get());
    backdrop_.Reset();
    backdropSurface_.Reset();
  }
}

void Overlay::SetEditMode(bool on) {
  if (on == editMode_ || !hwnd_) return;
  editMode_ = on;
  LONG_PTR ex = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
  ex = on ? (ex & ~WS_EX_TRANSPARENT) : (ex | WS_EX_TRANSPARENT); // take mouse input only while editing
  SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, ex);
  if (!on && drag_) { ReleaseCapture(); drag_ = nullptr; }
  UpdateBackdrop();
  forceRedraw_ = true;
  if (dcomp_) dcomp_->Commit();
}

LRESULT CALLBACK Overlay::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    static_cast<Overlay*>(cs->lpCreateParams)->hwnd_ = hwnd; // before CreateWindowEx returns
  }
  auto* self = reinterpret_cast<Overlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self) {
    LRESULT result = 0;
    if (self->hook_ && self->hook_(hwnd, msg, wp, lp, result)) return result;
    return self->HandleMessage(msg, wp, lp);
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Overlay::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
    case WM_LBUTTONDOWN:
      if (editMode_) {
        const POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        for (auto it = slots_.rbegin(); it != slots_.rend(); ++it) {
          Widget& w = *it->widget;
          const POINT p = ClampToMonitor(w, w.X(), w.Y());
          const RECT rc{p.x, p.y, p.x + static_cast<LONG>(w.Width() * ScaleOf(w)),
                        p.y + static_cast<LONG>(w.Height() * ScaleOf(w))};
          if (PtInRect(&rc, pt)) {
            drag_ = &*it;
            dragOffset_ = {pt.x - p.x, pt.y - p.y};
            SetCapture(hwnd_);
            break;
          }
        }
      }
      return 0;
    case WM_MOUSEMOVE:
      if (drag_) {
        constexpr int kGrid = 4; // snap so widgets line up easily
        const POINT p = ClampToMonitor(*drag_->widget, static_cast<int>(GET_X_LPARAM(lp) - dragOffset_.x) / kGrid * kGrid,
                                       static_cast<int>(GET_Y_LPARAM(lp) - dragOffset_.y) / kGrid * kGrid);
        const int x = p.x, y = p.y;
        drag_->widget->SetPos(x, y);
        drag_->visual->SetOffsetX(static_cast<float>(x));
        drag_->visual->SetOffsetY(static_cast<float>(y));
        dcomp_->Commit();
      }
      return 0;
    case WM_LBUTTONUP:
      if (drag_) {
        Slot* s = drag_;
        drag_ = nullptr;
        ReleaseCapture();
        if (onMoved_) onMoved_(s->widget->Section(), s->widget->X(), s->widget->Y());
      }
      return 0;
    case WM_CAPTURECHANGED:
      drag_ = nullptr;
      return 0;
  }
  return DefWindowProcW(hwnd_, msg, wp, lp);
}
