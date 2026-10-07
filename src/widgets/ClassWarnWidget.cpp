// Faster class behind: a small red pill ("HYPERCAR 1.8 s behind · #6 TOY") while a car of
// a faster class is closing in. Invisible otherwise.
#include "widgets/Widget.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr float kW = 360.f, kH = 26.f;

struct View {
  bool valid = false;
  bool on = false;
  int gapTenths = 0;
  wchar_t cls[24]{};
  wchar_t number[8]{};
  Brand brand;
  bool operator==(const View&) const = default;
};

const OptionDef kOptions[] = {
  OptFloat("warn_seconds", "Warn when closer than (s)", 3.0, 0.5, 10.0),
  OptColor("color", "Pill colour", 0xE5322DE6),
};

class ClassWarnWidget final : public Widget {
public:
  ClassWarnWidget(const WidgetType& t, const OptionSet& o)
      : Widget(t, o), warn_(o.Float("warn_seconds")), color_(o.Color("color")) {}

  float Width() const override { return kW; }
  float Height() const override { return kH; }

  bool Prepare(const Model& m) override {
    View v;
    if (m.onTrack && m.player && m.trackLength > 0) Build(m, v);
    if (v == view_) return false;
    view_ = v;
    return true;
  }

  void Draw(Painter& p) override {
    if (!view_.on) { if (p.EditMode()) DrawPlaceholder(p, kW, kH, L"FASTER CLASS WARNING"); return; }
    wchar_t gap[24], who[24];
    swprintf_s(gap, L"%.1f s behind", view_.gapTenths / 10.0);
    swprintf_s(who, view_.number[0] ? L"#%ls" : L"%ls", view_.number);
    const float clsW = p.Measure(view_.cls, Font::TextBold), gapW = p.Measure(gap, Font::Text), whoW = p.Measure(who, Font::Text);
    const float brandW = view_.brand.code[0] ? 36.f : 0.f;
    const float w = std::min(kW, 34.f + clsW + 10 + gapW + 10 + whoW + (brandW ? brandW + 6 : 0) + 10);
    p.FillRounded(0, 0, w, kH, 6.f, color_);
    if (p.EditMode()) p.Outline(0, 0, w, kH, 6.f, Col::EditOutline, 2.f);
    const D2D1_POINT_2F arrow[] = {{12, 19}, {12, 7}, {7, 12}, {12, 7}, {17, 12}};
    p.Polyline(arrow, 5, Col::Text, 2.4f);
    float x = 26.f;
    p.Text(x, 0, clsW + 2, kH, view_.cls, Font::TextBold, Col::Text);
    x += clsW + 10;
    p.Text(x, 0, gapW + 2, kH, gap, Font::Text, Col::Text);
    x += gapW + 10;
    p.Text(x, 0, whoW + 2, kH, who, Font::Text, Col::Rgb(0xFFFFFF, 0.85f));
    x += whoW + 6;
    if (brandW) DrawBrand(p, x, 0, brandW, kH, view_.brand);
  }

private:
  void Build(const Model& m, View& v) const {
    const Snapshot& s = *m.snap;
    const VehicleScoringInfoV01& me = *m.player;
    const int myRank = ClassRank(me.mVehicleClass);
    const double L = m.trackLength;
    v.valid = true;
    if (me.mInPits) return;
    double bestGap = 1e9;
    int bestIdx = -1;
    for (int i = 0; i < s.numVehicles; ++i) {
      const VehicleScoringInfoV01& o = s.vehicles[i];
      if (i == m.playerIdx || o.mInPits || o.mFinishStatus != 0 || ClassRank(o.mVehicleClass) <= myRank) continue;
      double d = me.mLapDist - o.mLapDist; // metres they are behind
      if (d < 0) d += L;
      if (d > L * 0.5) continue;
      const double speed = std::sqrt(o.mLocalVel.x * o.mLocalVel.x + o.mLocalVel.z * o.mLocalVel.z);
      if (speed < 20.0) continue;
      const double gap = d / speed;
      // Hysteresis: once shown, keep it until it's clearly gone.
      const double limit = view_.on ? warn_ + 0.5 : warn_;
      if (gap < limit && gap < bestGap) { bestGap = gap; bestIdx = i; }
    }
    if (bestIdx < 0) return;
    const VehicleScoringInfoV01& o = s.vehicles[bestIdx];
    v.on = true;
    v.gapTenths = std::max(1, static_cast<int>(std::lround(bestGap * 10.0)));
    // Class name as LMU reports it, upper case ("HYPERCAR", "LMP2").
    wchar_t cls[33];
    CopyName(cls, o.mVehicleClass);
    for (int k = 0; cls[k] && k < 23; ++k) v.cls[k] = static_cast<wchar_t>(towupper(cls[k] == L'_' ? L' ' : cls[k]));
    CarNumber(v.number, o);
    v.brand = BrandFor(s, o);
  }

  double warn_;
  D2D1_COLOR_F color_;
  View view_;
};

} // namespace

const WidgetType kClassWarnWidget{
  "classwarn", "Faster class warning", "Red pill when a faster class is closing in behind you.",
  true, 16, 852, 150, kOptions, CreateWidget<ClassWarnWidget>};
