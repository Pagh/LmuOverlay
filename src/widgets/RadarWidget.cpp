// Radar: a bar on each side of the screen that lights up while a car is alongside,
// placed where that car is (front / middle / rear of yours). Invisible otherwise.
// Positions come from telemetry (full rate); LMU's car frame is x = left, y = up, z = back.
#include "widgets/Widget.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr float kBarW = 12.f, kTextH = 24.f;

struct Side {
  bool on = false;
  int pos = 0;      // where the overlap is: -10 (their car ahead of yours) .. 10 (behind), quantised
  bool operator==(const Side&) const = default;
};

struct View {
  bool valid = false;
  Side left, right;
  bool operator==(const View&) const = default;
};

const OptionDef kOptions[] = {
  OptInt("spread", "Distance between the bars (px)", 880, 200, 3000),
  OptInt("bar_height", "Bar height (px)", 180, 60, 600),
  OptFloat("car_length", "Car length (m)", 4.9, 3.5, 6.0, "Cars overlap when they're closer than this front to back"),
  OptFloat("max_side", "Side distance (m)", 6.0, 3.0, 12.0, "How far to the side a car still counts as alongside"),
  OptBool("show_text", "CAR LEFT / RIGHT text", true),
  OptColor("color", "Bar colour", 0xFF8C28FF),
};

class RadarWidget final : public Widget {
public:
  RadarWidget(const WidgetType& t, const OptionSet& o)
      : Widget(t, o),
        spread_(static_cast<float>(o.Int("spread"))),
        barH_(static_cast<float>(o.Int("bar_height"))),
        carLen_(o.Float("car_length")),
        maxSide_(o.Float("max_side")),
        text_(o.Bool("show_text")),
        color_(o.Color("color")) {}

  float Width() const override { return spread_ + 2 * kBarW + (text_ ? 2 * 100.f : 0.f); }
  float Height() const override { return barH_; }

  bool Prepare(const Model& m) override {
    View v;
    if (m.onTrack && m.telem && m.player && !m.player->mInPits) Build(m, v);
    if (v == view_) return false;
    view_ = v;
    return true;
  }

  void Draw(Painter& p) override {
    const float w = Width();
    const float lx = text_ ? 100.f : 0.f, rx = w - lx - kBarW;
    if (p.EditMode()) {
      // Show the two bar positions so they can be lined up with the screen.
      p.FillRounded(lx, 0, kBarW, barH_, kBarW / 2, Col::Rgb(0x8A93A3, 0.5f));
      p.FillRounded(rx, 0, kBarW, barH_, kBarW / 2, Col::Rgb(0x8A93A3, 0.5f));
      p.Outline(0, 0, w, barH_, 6.f, Col::EditOutline, 2.f);
      p.Text(0, 0, w, barH_, L"RADAR (bars appear when a car is alongside)", Font::TextBold, Col::Dim, Align::Center);
      return;
    }
    if (!view_.valid) return;
    DrawSide(p, lx, view_.left, true);
    DrawSide(p, rx, view_.right, false);
  }

private:
  void DrawSide(Painter& p, float x, const Side& s, bool left) const {
    if (!s.on) return;
    D2D1_COLOR_F dim = color_;
    dim.a = 0.28f;
    p.FillRounded(x, 0, kBarW, barH_, kBarW / 2, dim);
    // The bright part spans the other car, scaled so the bar is two car lengths.
    const float seg = barH_ * 0.5f, mid = barH_ * 0.5f + s.pos / 10.f * barH_ * 0.5f;
    const float y0 = std::clamp(mid - seg / 2, 0.f, barH_ - seg);
    p.FillRounded(x, y0, kBarW, seg, kBarW / 2, color_);
    if (!text_) return;
    const wchar_t* label = view_.left.on && view_.right.on ? L"3-WIDE" : left ? L"CAR LEFT" : L"CAR RIGHT";
    const float tw = 92.f, ty = barH_ * 0.5f - kTextH / 2;
    const float tx = left ? x + kBarW + 8 : x - 8 - tw;
    p.FillRounded(tx, ty, tw, kTextH, 5.f, Background());
    p.Text(tx, ty, tw, kTextH, label, Font::TextBold, color_, Align::Center);
  }

  void Build(const Model& m, View& v) const {
    const Snapshot& s = *m.snap;
    const TelemInfoV01& t = *m.telem;
    v.valid = true;
    const long myId = m.player->mID;
    for (int i = 0; i < s.numVehicles; ++i) {
      const VehicleScoringInfoV01& o = s.vehicles[i];
      if (o.mID == myId || o.mInPits || o.mInGarageStall) continue;
      const Snapshot::CarModel* c = s.CarFor(o.mID);
      if (!c) continue;
      const double dx = c->pos.x - t.mPos.x, dy = c->pos.y - t.mPos.y, dz = c->pos.z - t.mPos.z;
      if (dx * dx + dz * dz > 30.0 * 30.0) continue;
      // World -> car frame: dot product with the orientation matrix columns.
      const double sideM = t.mOri[0].x * dx + t.mOri[1].x * dy + t.mOri[2].x * dz;  // + = left
      const double backM = t.mOri[0].z * dx + t.mOri[1].z * dy + t.mOri[2].z * dz;  // + = behind
      if (std::fabs(backM) >= carLen_ || std::fabs(sideM) > maxSide_ || std::fabs(sideM) < 1.2) continue;
      Side& sd = sideM > 0 ? v.left : v.right;
      const int pos = std::clamp(static_cast<int>(std::lround(backM / carLen_ * 10.0)), -10, 10);
      if (!sd.on || std::abs(pos) < std::abs(sd.pos)) sd.pos = pos;
      sd.on = true;
    }
  }

  float spread_, barH_;
  double carLen_, maxSide_;
  bool text_;
  D2D1_COLOR_F color_;
  View view_;
};

} // namespace

const WidgetType kRadarWidget{
  "radar", "Radar", "Side bars that light up while a car is alongside you.",
  true, 410, 420, 33, kOptions, CreateWidget<RadarWidget>};
