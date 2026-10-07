// Damage: a small car silhouette with the hit zones and wheels, aero / suspension damage,
// the repair time and the lap time you're losing because of it. Only shown once you're
// damaged (option). Body dents and wheel state come from shared memory; aero / suspension
// damage and the repair time from LMU's REST API (polled rarely, see App).
#include "widgets/Widget.h"
#include <algorithm>
#include <climits>
#include <cmath>

namespace {

constexpr float kW = 360.f, kH = 90.f, kPad = 10.f;
// Car (top view, nose up) in widget coordinates. The outline is designed on a 64 x 108 grid.
constexpr float kCarX = 14.f, kCarY = 10.f, kCarW = 40.f, kCarH = 70.f;
constexpr float kSX = kCarW / 64.f, kSY = kCarH / 108.f;
constexpr float kWheelW = 6.f, kWheelH = 15.f;

// LMU mDentSeverity index for each zone, row by row:
// front-left, front, front-right, left, right, rear-left, rear, rear-right.
constexpr int kZoneIndex[8] = {1, 0, 7, 2, 6, 3, 4, 5};
const wchar_t* const kZoneName[8] = {L"FRONT-LEFT", L"FRONT", L"FRONT-RIGHT", L"LEFT SIDE", L"RIGHT SIDE",
                                     L"REAR-LEFT", L"REAR", L"REAR-RIGHT"};

enum class Status : uint8_t { Ok, Damaged, Repair, Overheating, Detached };

struct View {
  bool valid = false;
  uint8_t zone[8]{};         // 0..2 per zone
  bool flat[4]{}, wheelOff[4]{};
  bool rest = false;         // REST data available
  int aero = 0;              // aero damage, 0.1 %
  int susp[4]{};             // suspension damage, 0.1 %
  int repairS = 0;           // repair time, s
  int lossMs = INT_MIN;      // lap time lost per lap
  bool damaged = false;
  Status status = Status::Ok;
  bool operator==(const View&) const = default;
};

const OptionDef kOptions[] = {
  OptBool("show_when_clean", "Show when there's no damage", false, "Off: the widget only appears once you have damage"),
  OptColor("light", "Light damage", 0xFFB52EFF),
  OptColor("heavy", "Heavy damage", 0xFF4D4DFF),
  OptColor("ok", "No damage", 0x3B4452FF),
};

class DamageWidget final : public Widget {
public:
  DamageWidget(const WidgetType& t, const OptionSet& o)
      : Widget(t, o),
        showClean_(o.Bool("show_when_clean")),
        light_(o.Color("light")),
        heavy_(o.Color("heavy")),
        ok_(o.Color("ok")) {}

  float Width() const override { return kW; }
  float Height() const override { return kH; }

  bool Prepare(const Model& m) override {
    View v;
    if (m.onTrack && m.telem) Build(m, v);
    if (v == view_) return false;
    view_ = v;
    return true;
  }

  void Draw(Painter& p) override {
    if (!view_.valid || (!view_.damaged && !showClean_)) {
      if (p.EditMode()) DrawPlaceholder(p, kW, kH, L"DAMAGE (shown after a hit)");
      return;
    }
    const View& v = view_;
    p.Panel(kW, kH);
    DrawCar(p);

    const float x = kCarX + kCarW + 18.f, w = kW - x - kPad;
    // Title: the worst hit zone, or the overall state.
    wchar_t title[40];
    D2D1_COLOR_F tc = Col::Good;
    int worst = -1;
    for (int z = 0; z < 8; ++z) if (v.zone[z] > 0 && (worst < 0 || v.zone[z] > v.zone[worst])) worst = z;
    switch (v.status) {
      case Status::Overheating: wcscpy_s(title, L"OVERHEATING"); tc = heavy_; break;
      case Status::Detached: wcscpy_s(title, L"PARTS DETACHED"); tc = heavy_; break;
      default:
        if (worst >= 0) { swprintf_s(title, L"%ls DAMAGE", kZoneName[worst]); tc = Severity(v.zone[worst]); }
        else if (v.damaged) { wcscpy_s(title, v.aero > 0 ? L"AERO DAMAGE" : L"DAMAGED"); tc = light_; }
        else wcscpy_s(title, L"NO DAMAGE");
    }
    p.Text(x, 6, w, 18, title, Font::TextBold, tc);
    if (v.repairS > 0) p.Textf(x, 6, w, 18, Font::Small, Col::Dim, Align::Right, L"repair  %d s", v.repairS);

    static const wchar_t* const kCorner[] = {L"Susp FL", L"Susp FR", L"Susp RL", L"Susp RR"};
    if (v.rest) {
      int ws = 0, wi = 0;
      for (int i = 0; i < 4; ++i) if (v.susp[i] > ws) { ws = v.susp[i]; wi = i; }
      Reading(p, x, 26, w, L"Aero", v.aero);
      Reading(p, x, 44, w, ws > 0 ? kCorner[wi] : L"Suspension", ws);
    } else {
      p.Text(x, 26, w, 34, L"aero / suspension: enable the\nLMU REST API in Settings > General", Font::Small, Col::Dim);
    }
    p.Text(x, 64, w, 18, L"Lap time lost", Font::Small, Col::Dim);
    if (v.lossMs != INT_MIN) {
      const D2D1_COLOR_F c = v.lossMs > 300 ? heavy_ : v.lossMs > 50 ? light_ : Col::Text;
      p.Textf(x, 64, w, 18, Font::TextBold, c, Align::Right, L"%+.2f s / lap", v.lossMs / 1000.0);
    } else {
      p.Text(x, 64, w, 18, v.damaged ? L"after next clean lap" : L"—", Font::Small, Col::Dim, Align::Right);
    }
  }

private:
  D2D1_COLOR_F Severity(int s) const { return s >= 2 ? heavy_ : s == 1 ? light_ : ok_; }
  D2D1_COLOR_F ForPermille(int pm) const { return pm >= 250 ? heavy_ : pm > 0 ? light_ : Col::Text; }

  void Reading(Painter& p, float x, float y, float w, const wchar_t* label, int permille) const {
    p.Text(x, y, 70, 16, label, Font::Small, Col::Dim);
    const float bx = x + 70, bw = w - 70 - 44;
    p.FillRounded(bx, y + 6, bw, 5, 2.5f, Col::Rgb(0x2B313C, 0.9f));
    if (permille > 0) p.FillRounded(bx, y + 6, std::max(5.f, bw * std::min(permille, 1000) / 1000.f), 5, 2.5f, ForPermille(permille));
    p.Textf(x, y, w, 16, Font::Small, ForPermille(permille), Align::Right, L"%.1f%%", permille / 10.0);
  }

  void EnsureGeometry(Painter& p) {
    if (body_ && factory_ == p.Factory()) return;
    factory_ = p.Factory();
    body_.Reset();
    if (FAILED(factory_->CreatePathGeometry(&body_))) return;
    ComPtr<ID2D1GeometrySink> s;
    body_->Open(&s);
    auto P = [](float x, float y) { return D2D1::Point2F(kCarX + x * kSX, kCarY + y * kSY); };
    s->BeginFigure(P(16, 4), D2D1_FIGURE_BEGIN_FILLED);
    s->AddQuadraticBezier(D2D1::QuadraticBezierSegment(P(32, -2), P(48, 4)));   // nose
    const D2D1_POINT_2F right[] = {P(58, 18), P(60, 40), P(56, 52), P(58, 66), P(62, 90)};
    s->AddLines(right, 5);
    s->AddQuadraticBezier(D2D1::QuadraticBezierSegment(P(62, 106), P(48, 108))); // rear right
    s->AddLine(P(16, 108));
    s->AddQuadraticBezier(D2D1::QuadraticBezierSegment(P(2, 106), P(2, 90)));    // rear left
    const D2D1_POINT_2F left[] = {P(6, 66), P(8, 52), P(4, 40), P(6, 18)};
    s->AddLines(left, 4);
    s->EndFigure(D2D1_FIGURE_END_CLOSED);
    s->Close();
  }

  void DrawCar(Painter& p) {
    EnsureGeometry(p);
    const View& v = view_;

    // Wheels, coloured by suspension damage; red ring = flat tyre, outline only = wheel lost.
    const float wx[2] = {kCarX - kWheelW + 1, kCarX + kCarW - 1};
    const float wy[2] = {kCarY + 18 * kSY, kCarY + kCarH - 20 * kSY - kWheelH};
    for (int i = 0; i < 4; ++i) {
      const float x = wx[i % 2], y = wy[i / 2];
      const D2D1_COLOR_F c = v.wheelOff[i] ? heavy_ : v.susp[i] > 0 ? ForPermille(v.susp[i]) : Col::Rgb(0x6A7382);
      if (v.wheelOff[i]) p.Outline(x, y, kWheelW, kWheelH, 2, c, 1.5f);
      else p.FillRounded(x, y, kWheelW, kWheelH, 2, c);
      if (v.flat[i]) p.Outline(x - 2, y - 2, kWheelW + 4, kWheelH + 4, 3, heavy_, 1.5f);
    }
    if (!body_) return;

    // Body zones, masked by the car's shape.
    p.PushClip(body_.Get());
    const float colX[4] = {kCarX, kCarX + 20 * kSX, kCarX + 44 * kSX, kCarX + kCarW};
    const float rowY[4] = {kCarY - 4, kCarY + 38 * kSY, kCarY + 74 * kSY, kCarY + kCarH};
    const int cells[8][2] = {{0, 0}, {1, 0}, {2, 0}, {0, 1}, {2, 1}, {0, 2}, {1, 2}, {2, 2}};
    for (int z = 0; z < 8; ++z) {
      const int c = cells[z][0], r = cells[z][1];
      p.Fill(colX[c] + 1, rowY[r] + 1, colX[c + 1] - colX[c] - 2, rowY[r + 1] - rowY[r] - 2, Severity(v.zone[z]));
    }
    p.Fill(colX[1] + 1, rowY[1] + 1, colX[2] - colX[1] - 2, rowY[2] - rowY[1] - 2, ok_);
    p.PopClip();
    p.FillRounded(kCarX + 23 * kSX, kCarY + 42 * kSY, 18 * kSX, 28 * kSY, 4, Col::Rgb(0x15181E, 0.95f)); // cockpit
    p.DrawGeometry(body_.Get(), Col::Rgb(0xC8CED8, 0.55f), 1.2f);
  }

  void Build(const Model& m, View& v) const {
    const TelemInfoV01& t = *m.telem;
    v.valid = true;
    for (int z = 0; z < 8; ++z) v.zone[z] = std::min<uint8_t>(t.mDentSeverity[kZoneIndex[z]], 2);
    for (int i = 0; i < 4; ++i) {
      v.flat[i] = t.mWheel[i].mFlat;
      v.wheelOff[i] = t.mWheel[i].mDetached;
    }
    if (m.rest && m.rest->valid) {
      v.rest = true;
      v.aero = Quantize(std::clamp(m.rest->aero, 0.f, 1.f) * 1000.0, 1.0);
      for (int i = 0; i < 4; ++i) v.susp[i] = Quantize(std::clamp(m.rest->suspension[i], 0.f, 1.f) * 1000.0, 1.0);
      if (m.rest->repairSeconds > 0.5f) v.repairS = static_cast<int>(std::lround(m.rest->repairSeconds));
    }
    int dents = 0;
    for (uint8_t z : v.zone) dents += z;
    v.damaged = dents > 0 || t.mDetached || v.aero > 0 || v.repairS > 0;
    for (int i = 0; i < 4; ++i) v.damaged |= v.flat[i] || v.wheelOff[i] || v.susp[i] > 0;

    if (t.mOverheating) v.status = Status::Overheating;
    else if (t.mDetached) v.status = Status::Detached;
    else if (v.repairS > 0) v.status = Status::Repair;
    else if (v.damaged) v.status = Status::Damaged;

    // Your average clean lap since the damage minus the average before it.
    if (v.damaged && m.timing && m.timing->PaceBeforeDamage() > 0 && m.timing->PaceSinceDamage() > 0)
      v.lossMs = static_cast<int>(std::lround((m.timing->PaceSinceDamage() - m.timing->PaceBeforeDamage()) * 1000.0));
  }

  bool showClean_;
  D2D1_COLOR_F light_, heavy_, ok_;
  ComPtr<ID2D1PathGeometry> body_;
  ID2D1Factory* factory_ = nullptr;
  View view_;
};

} // namespace

const WidgetType kDamageWidget{
  "damage", "Damage", "Body zones, aero and suspension damage, repair time, and the lap time you're losing.",
  true, 1544, 750, 250, kOptions, CreateWidget<DamageWidget>};
