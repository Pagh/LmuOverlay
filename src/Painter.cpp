#include "Painter.h"
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <cmath>

namespace {

struct FontSpec {
  float size;
  DWRITE_FONT_WEIGHT weight;
};

constexpr FontSpec kFonts[] = {
  {11.f, DWRITE_FONT_WEIGHT_NORMAL},   // Small
  {14.f, DWRITE_FONT_WEIGHT_NORMAL},   // Text
  {14.f, DWRITE_FONT_WEIGHT_SEMI_BOLD},// TextBold
  {18.f, DWRITE_FONT_WEIGHT_SEMI_BOLD},// Mid
  {24.f, DWRITE_FONT_WEIGHT_BOLD},     // Big
  {44.f, DWRITE_FONT_WEIGHT_BOLD},     // Huge
};
static_assert(sizeof(kFonts) / sizeof(kFonts[0]) == static_cast<int>(Font::Count));

} // namespace

bool Painter::Init(ID2D1DeviceContext* resourceDc, IDWriteFactory* dwrite) {
  resourceDc->GetFactory(&factory_);
  dwrite_ = dwrite;
  if (FAILED(resourceDc->CreateSolidColorBrush(Col::Text, &brush_))) return false;
  for (int i = 0; i < static_cast<int>(Font::Count); ++i) {
    // Bahnschrift ships with Windows 10+ and is designed for at-a-glance reading (DIN-style).
    if (FAILED(dwrite->CreateTextFormat(L"Bahnschrift", nullptr, kFonts[i].weight, DWRITE_FONT_STYLE_NORMAL,
                                        DWRITE_FONT_STRETCH_NORMAL, kFonts[i].size, L"en-us", &fonts_[i])))
      return false;
    fonts_[i]->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    fonts_[i]->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    fonts_[i]->SetTrimming(&trim, nullptr);
  }
  return true;
}

void Painter::Begin(ID2D1DeviceContext* dc, POINT off, float scale, D2D1_COLOR_F background, bool editMode) {
  dc_ = dc;
  bg_ = background;
  editMode_ = editMode;
  dc_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE); // ClearType needs an opaque target
  dc_->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) *
                    D2D1::Matrix3x2F::Translation(static_cast<float>(off.x), static_cast<float>(off.y)));
}

void Painter::End() { dc_ = nullptr; }

void Painter::Fill(float x, float y, float w, float h, D2D1_COLOR_F c) {
  brush_->SetColor(c);
  dc_->FillRectangle(D2D1::RectF(x, y, x + w, y + h), brush_.Get());
}

void Painter::FillRounded(float x, float y, float w, float h, float r, D2D1_COLOR_F c) {
  brush_->SetColor(c);
  dc_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), r, r), brush_.Get());
}

void Painter::Outline(float x, float y, float w, float h, float r, D2D1_COLOR_F c, float stroke) {
  brush_->SetColor(c);
  const float i = stroke * 0.5f;
  dc_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x + i, y + i, x + w - i, y + h - i), r, r), brush_.Get(), stroke);
}

void Painter::Text(float x, float y, float w, float h, const wchar_t* s, Font f, D2D1_COLOR_F c, Align a) {
  IDWriteTextFormat* fmt = fonts_[static_cast<int>(f)].Get();
  fmt->SetTextAlignment(a == Align::Left ? DWRITE_TEXT_ALIGNMENT_LEADING
                        : a == Align::Center ? DWRITE_TEXT_ALIGNMENT_CENTER
                                             : DWRITE_TEXT_ALIGNMENT_TRAILING);
  brush_->SetColor(c);
  dc_->DrawText(s, static_cast<UINT32>(wcslen(s)), fmt, D2D1::RectF(x, y, x + w, y + h), brush_.Get(),
                D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void Painter::Textf(float x, float y, float w, float h, Font f, D2D1_COLOR_F c, Align a, const wchar_t* fmt, ...) {
  wchar_t buf[128];
  va_list args;
  va_start(args, fmt);
  _vsnwprintf_s(buf, _TRUNCATE, fmt, args);
  va_end(args);
  Text(x, y, w, h, buf, f, c, a);
}

float Painter::Measure(const wchar_t* s, Font f) {
  ComPtr<IDWriteTextLayout> layout;
  if (FAILED(dwrite_->CreateTextLayout(s, static_cast<UINT32>(wcslen(s)), fonts_[static_cast<int>(f)].Get(), 4096.f, 100.f,
                                       &layout)))
    return 0.f;
  DWRITE_TEXT_METRICS m{};
  layout->GetMetrics(&m);
  return m.widthIncludingTrailingWhitespace;
}

void Painter::Polyline(const D2D1_POINT_2F* pts, int n, D2D1_COLOR_F c, float stroke) {
  if (n < 2) return;
  ComPtr<ID2D1PathGeometry> g;
  if (FAILED(factory_->CreatePathGeometry(&g))) return;
  ComPtr<ID2D1GeometrySink> sink;
  if (FAILED(g->Open(&sink))) return;
  sink->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_HOLLOW);
  sink->AddLines(pts + 1, static_cast<UINT32>(n - 1));
  sink->EndFigure(D2D1_FIGURE_END_OPEN);
  sink->Close();
  brush_->SetColor(c);
  dc_->DrawGeometry(g.Get(), brush_.Get(), stroke);
}

void Painter::HBar(float x, float y, float w, float h, float frac, D2D1_COLOR_F c) {
  frac = frac < 0.f ? 0.f : frac > 1.f ? 1.f : frac;
  Fill(x, y, w, h, Col::Rgb(0x2B313C, 0.9f));
  if (frac > 0.f) Fill(x, y, w * frac, h, c);
}

void Painter::VBar(float x, float y, float w, float h, float frac, D2D1_COLOR_F c) {
  frac = frac < 0.f ? 0.f : frac > 1.f ? 1.f : frac;
  Fill(x, y, w, h, Col::Rgb(0x2B313C, 0.9f));
  if (frac > 0.f) Fill(x, y + h * (1.f - frac), w, h * frac, c);
}

void Painter::FillGeometry(ID2D1Geometry* g, D2D1_COLOR_F c) {
  brush_->SetColor(c);
  dc_->FillGeometry(g, brush_.Get());
}

void Painter::DrawGeometry(ID2D1Geometry* g, D2D1_COLOR_F c, float stroke) {
  brush_->SetColor(c);
  dc_->DrawGeometry(g, brush_.Get(), stroke);
}

void Painter::PushClip(ID2D1Geometry* g) {
  dc_->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), g, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE), nullptr);
}

void Painter::PopClip() { dc_->PopLayer(); }

void Painter::Panel(float w, float h) {
  FillRounded(0, 0, w, h, 6.f, bg_);
  if (editMode_) Outline(0, 0, w, h, 6.f, Col::EditOutline, 2.f);
}

void FormatLapTime(wchar_t* out, size_t n, double t) {
  if (!(t > 0.0) || t > 3600.0) { swprintf_s(out, n, L"-:--.---"); return; }
  const int m = static_cast<int>(t / 60.0);
  swprintf_s(out, n, L"%d:%06.3f", m, t - m * 60.0);
}

D2D1_COLOR_F ClassColor(const char* cls) {
  auto has = [cls](const char* s) { return strstr(cls, s) != nullptr; };
  if (has("Hyper") || has("LMH") || has("LMDh")) return Col::Rgb(0xE5322D);
  if (has("LMP2")) return Col::Rgb(0x2F7DE1);
  if (has("LMP3")) return Col::Rgb(0x9B59D0);
  if (has("GTE")) return Col::Rgb(0xF08A24);
  if (has("GT3")) return Col::Rgb(0x2DB84D);
  return Col::Rgb(0x8A93A3);
}
