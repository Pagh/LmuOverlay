#pragma once
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

enum class Font { Small, Text, TextBold, Mid, Big, Huge, Count };
enum class Align { Left, Center, Right };

namespace Col {
constexpr D2D1_COLOR_F Rgb(unsigned rgb, float a = 1.f) {
  return {((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, a};
}
inline constexpr D2D1_COLOR_F Text = Rgb(0xF2F4F7);
inline constexpr D2D1_COLOR_F Dim = Rgb(0x8A93A3);
inline constexpr D2D1_COLOR_F Panel = Rgb(0x15181E);
inline constexpr D2D1_COLOR_F Row = Rgb(0x222731);
inline constexpr D2D1_COLOR_F PlayerRow = Rgb(0x3A4458);
inline constexpr D2D1_COLOR_F Good = Rgb(0x3DDC84);
inline constexpr D2D1_COLOR_F Bad = Rgb(0xFF5A5A);
inline constexpr D2D1_COLOR_F Warn = Rgb(0xFFC23D);
inline constexpr D2D1_COLOR_F Info = Rgb(0x4DA3FF);
inline constexpr D2D1_COLOR_F Purple = Rgb(0xB87CFF);
inline constexpr D2D1_COLOR_F Throttle = Rgb(0x3DDC84);
inline constexpr D2D1_COLOR_F Brake = Rgb(0xFF4D4D);
inline constexpr D2D1_COLOR_F Clutch = Rgb(0x4DA3FF);
inline constexpr D2D1_COLOR_F EditOutline = Rgb(0xFFC23D);
} // namespace Col

// Device-independent drawing helpers. All coordinates are in unscaled widget
// pixels; the global UI scale is applied through the transform.
// Text formats and the brush are created once; nothing allocates per frame.
class Painter {
public:
  bool Init(ID2D1DeviceContext* resourceDc, IDWriteFactory* dwrite);

  // Called by the overlay around each widget draw.
  void Begin(ID2D1DeviceContext* dc, POINT surfaceOffset, float scale, D2D1_COLOR_F background, bool editMode);
  void End();

  void Fill(float x, float y, float w, float h, D2D1_COLOR_F c);
  void FillRounded(float x, float y, float w, float h, float r, D2D1_COLOR_F c);
  void Outline(float x, float y, float w, float h, float r, D2D1_COLOR_F c, float stroke = 1.f);
  void Text(float x, float y, float w, float h, const wchar_t* s, Font f, D2D1_COLOR_F c, Align a = Align::Left);
  void Textf(float x, float y, float w, float h, Font f, D2D1_COLOR_F c, Align a, const wchar_t* fmt, ...);
  // Width of a single line of text (allocates a layout: call from Draw only, not per frame).
  float Measure(const wchar_t* s, Font f);
  // Connected line through n points (unscaled widget pixels).
  void Polyline(const D2D1_POINT_2F* pts, int n, D2D1_COLOR_F c, float stroke = 1.f);
  // Horizontal bar filled to `frac` (0..1) over a dim track.
  void HBar(float x, float y, float w, float h, float frac, D2D1_COLOR_F c);
  void VBar(float x, float y, float w, float h, float frac, D2D1_COLOR_F c);

  // Shapes (geometry is in unscaled widget pixels, like everything else).
  ID2D1Factory* Factory() const { return factory_.Get(); }
  void FillGeometry(ID2D1Geometry* g, D2D1_COLOR_F c);
  void DrawGeometry(ID2D1Geometry* g, D2D1_COLOR_F c, float stroke = 1.f);
  void PushClip(ID2D1Geometry* g); // everything until PopClip() is masked by g
  void PopClip();

  // Standard widget background (panel + edit-mode outline).
  void Panel(float w, float h);

  bool EditMode() const { return editMode_; }

private:
  ID2D1DeviceContext* dc_ = nullptr;
  ComPtr<ID2D1SolidColorBrush> brush_;
  ComPtr<ID2D1Factory> factory_;
  ComPtr<IDWriteFactory> dwrite_;
  ComPtr<IDWriteTextFormat> fonts_[static_cast<int>(Font::Count)];
  D2D1_COLOR_F bg_{};
  bool editMode_ = false;
};

// Formatting helpers (write into caller-provided buffers).
void FormatLapTime(wchar_t* out, size_t n, double seconds); // m:ss.mmm, "-:--.---" if invalid
D2D1_COLOR_F ClassColor(const char* vehicleClass);
