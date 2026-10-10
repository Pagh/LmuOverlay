#pragma once
#include "Painter.h"
#include "Settings.h"
#include "widgets/Widget.h"
#include <d3d11.h>
#include <dcomp.h>
#include <functional>

// One click-through, topmost window (covering one monitor) with no redirection
// bitmap. Each widget is a DirectComposition visual backed by its own small GPU surface:
//  - DWM only composes the widget rectangles (no full-screen layered bitmap);
//  - a widget is re-rendered only when its content changes;
//  - moving a widget only changes a visual offset (no re-render).
class Overlay {
public:
  using MessageHook = std::function<bool(HWND, UINT, WPARAM, LPARAM, LRESULT&)>;
  using MoveCallback = std::function<void(const char* section, int x, int y)>;
  using ScaleCallback = std::function<void(const char* section, float scale)>;

  bool Create(HINSTANCE inst, const GeneralSettings& gs, MessageHook hook, MoveCallback onMoved, ScaleCallback onScaled);
  void Destroy();

  void SetWidgets(WidgetList widgets);   // replaces the current set (profile change / option edit)
  void SetSettings(const GeneralSettings& gs);
  void SetMonitorRect(const RECT& rc);   // screen rectangle the overlay covers

  // Renders whatever is due. forceVisible shows the overlay even off track (settings preview).
  // Returns the number of widgets redrawn.
  int Tick(const Model& m, double now, bool forceVisible);

  void SetEditMode(bool on);
  bool EditMode() const { return editMode_; }
  void SetUserHidden(bool hidden) { userHidden_ = hidden; }
  bool UserHidden() const { return userHidden_; }
  bool Visible() const { return visible_; }
  HWND Hwnd() const { return hwnd_; }

private:
  struct Slot {
    Widget* widget;
    ComPtr<IDCompositionVisual2> visual;
    ComPtr<IDCompositionSurface> surface;
  };

  static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

  bool CreateDevices();
  void ReleaseDevices();
  bool CreateSlots();
  void ClearSlots();
  float ScaleOf(const Widget& w) const { return w.Scale() * gs_.scale; }
  POINT ClampToMonitor(const Widget& w, int x, int y) const; // keeps at least 40 px of the widget visible
  void ApplyPositions();                                     // re-clamps every visual (monitor change)
  bool DrawWidget(Slot& s);
  bool ResizeSlot(Slot& s);                                  // new surface after a scale change
  Slot* HitTest(POINT pt, bool* corner);                     // topmost widget under pt (edit mode)
  void SetVisible(bool v);
  void UpdateBackdrop();

  HWND hwnd_ = nullptr;
  RECT rect_{};
  GeneralSettings gs_;
  WidgetList widgets_;
  MessageHook hook_;
  MoveCallback onMoved_;
  ScaleCallback onScaled_;

  ComPtr<ID3D11Device> d3d_;
  ComPtr<ID2D1Factory1> d2dFactory_;
  ComPtr<ID2D1Device> d2dDevice_;
  ComPtr<ID2D1DeviceContext> resourceDc_;
  ComPtr<IDWriteFactory> dwrite_;
  ComPtr<IDCompositionDesktopDevice> dcomp_;
  ComPtr<IDCompositionTarget> target_;
  ComPtr<IDCompositionVisual2> root_;
  ComPtr<IDCompositionVisual2> backdrop_;
  ComPtr<IDCompositionSurface> backdropSurface_;
  std::vector<Slot> slots_;
  Painter painter_;

  bool deviceLost_ = false;
  bool forceRedraw_ = true;
  bool editMode_ = false;
  bool userHidden_ = false;
  bool visible_ = false;

  // Edit-mode dragging: moves the widget, or resizes it when grabbed by its bottom-right corner.
  Slot* drag_ = nullptr;
  bool resizing_ = false;
  POINT dragOffset_{};
};
