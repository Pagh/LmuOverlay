// Damage: a small car drawing where each kind of damage has its own place (body panels, front
// splitter and rear wing for aero, suspension arms, tyres), labels naming the kinds, aero /
// suspension amounts, the repair time and what it costs: per lap, split into the straights
// (from the car's acceleration before / since the damage, see StraightLine) and the corners.
// Only shown once you're damaged (option), and for a few seconds after any contact, so a touch
// that left no damage is confirmed too. Body dents, wheel state and impacts come from shared
// memory; aero / suspension damage and the repair time from LMU's REST API (polled rarely).
#include "widgets/Widget.h"
#include <algorithm>
#include <climits>
#include <cmath>

namespace {

constexpr float kW = 360.f, kH = 108.f, kPad = 10.f;
// Car (top view, nose up) in widget coordinates. The outline is designed on a 64 x 108 grid.
constexpr float kCarX = 22.f, kCarY = 12.f, kCarW = 38.f, kCarH = 82.f;
constexpr float kSX = kCarW / 64.f, kSY = kCarH / 108.f;
constexpr float kWheelW = 7.f, kWheelH = 15.f, kArm = 5.f; // wheels sit kArm px off the body, on two arms

// LMU mDentSeverity index for each zone, row by row:
// front-left, front, front-right, left, right, rear-left, rear, rear-right.
constexpr int kZoneIndex[8] = {1, 0, 7, 2, 6, 3, 4, 5};
const wchar_t* const kWheelName[4] = {L"FL", L"FR", L"RL", L"RR"};

enum class Status : uint8_t { Ok, Damaged, Repair, Overheating, Detached };

struct View {
  bool valid = false;
  uint8_t zone[8]{};         // 0..2 per zone
  bool flat[4]{}, wheelOff[4]{};
  bool rest = false;         // REST data available
  int aero = 0;              // aero damage, 0.1 %
  int susp[4]{};             // suspension damage, 0.1 %
  int repairS = 0;           // repair time, s
  bool detached = false;     // body parts came off
  int lossMs = INT_MIN;      // lap time lost per lap
  int straightMs = INT_MIN;  // of which on the straights
  int topKph = 0;            // top speed lost, 0.1 km/h
  uint8_t straightState = 0; // 0 no reference lap, 1 measuring, 2 known
  bool damaged = false;
  bool contact = false;      // a contact in the last few seconds
  int impact = 0;            // its strength (LMU's impact magnitude)
  int contacts = 0;          // contacts this session
  Status status = Status::Ok;
  bool operator==(const View&) const = default;
};

const OptionDef kOptions[] = {
  OptBool("show_when_clean", "Show when there's no damage", false, "Off: the widget only appears once you have damage"),
  OptBool("show_contact", "Show after a contact", true,
          "Also appears for a few seconds after any contact (car or wall), saying whether it left damage"),
  OptInt("contact_seconds", "Contact shown for (s)", 6, 2, 30),
  OptInt("min_impact", "Ignore contacts weaker than", 20, 0, 2000, "LMU's impact magnitude: kerb bumps are small"),
  OptColor("light", "Light damage", 0xFFB52EFF),
  OptColor("heavy", "Heavy damage", 0xFF4D4DFF),
  OptColor("ok", "No damage", 0x3B4452FF),
};

class DamageWidget final : public Widget {
public:
  DamageWidget(const WidgetType& t, const OptionSet& o)
      : Widget(t, o),
        showClean_(o.Bool("show_when_clean")),
        showContact_(o.Bool("show_contact")),
        contactS_(o.Int("contact_seconds")),
        minImpact_(o.Int("min_impact")),
        light_(o.Color("light")),
        heavy_(o.Color("heavy")),
        ok_(o.Color("ok")) {}

  float Width() const override { return kW; }
  float Height() const override { return kH; }

  bool Prepare(const Model& m) override {
    View v;
    if (m.onTrack && m.telem) {
      TrackContacts(*m.telem);
      Build(m, v);
    }
    if (v == view_) return false;
    view_ = v;
    return true;
  }

  void Draw(Painter& p) override {
    if (!view_.valid || (!view_.damaged && !view_.contact && !showClean_)) {
      if (p.EditMode()) DrawPlaceholder(p, kW, kH, L"DAMAGE (shown after a hit)");
      return;
    }
    const View& v = view_;
    p.Panel(kW, kH);
    DrawCar(p);

    const float x = kCarX + kCarW + kArm + kWheelW + 14.f, w = kW - x - kPad;
    // Line 1: one label per kind of damage, coloured by how bad it is.
    float cx = x;
    auto chip = [&](const wchar_t* label, D2D1_COLOR_F c) {
      const float cw = p.Measure(label, Font::Small) + 10.f;
      if (cx + cw > x + w - 64.f) return;
      p.FillRounded(cx, 6, cw, 16, 3, c);
      p.Text(cx, 6, cw, 16, label, Font::Small, Col::Rgb(0x15181E), Align::Center);
      cx += cw + 4.f;
    };
    int worstZone = 0, worstSusp = 0, suspPm = 0;
    for (int z = 0; z < 8; ++z) worstZone = std::max<int>(worstZone, v.zone[z]);
    for (int i = 0; i < 4; ++i) if (v.susp[i] > suspPm) { suspPm = v.susp[i]; worstSusp = i; }
    if (v.status == Status::Overheating) chip(L"ENGINE HOT", heavy_);
    if (worstZone > 0) chip(L"BODYWORK", Severity(worstZone));
    if (v.detached) chip(L"PARTS OFF", heavy_);
    if (v.aero > 0) chip(L"AERO", ForPermille(v.aero));
    if (suspPm > 0) chip(L"SUSPENSION", ForPermille(suspPm));
    for (int i = 0; i < 4; ++i) {
      if (v.wheelOff[i]) { wchar_t b[16]; swprintf_s(b, L"WHEEL OFF %ls", kWheelName[i]); chip(b, heavy_); }
      else if (v.flat[i]) { wchar_t b[16]; swprintf_s(b, L"FLAT %ls", kWheelName[i]); chip(b, heavy_); }
    }
    if (cx == x) {
      if (v.contact) p.Text(x, 6, w, 16, L"CONTACT · NO DAMAGE", Font::TextBold, Col::Good);
      else p.Text(x, 6, w, 16, v.damaged ? L"DAMAGED" : L"NO DAMAGE", Font::TextBold, v.damaged ? light_ : Col::Good);
    }
    if (v.repairS > 0) p.Textf(x, 6, w, 16, Font::Small, Col::Dim, Align::Right, L"repair %d s", v.repairS);
    else if (v.contact) p.Textf(x, 6, w, 16, Font::Small, Col::Warn, Align::Right, L"hit %d", v.impact);

    // Lines 2-3: how much aero / suspension (LMU's REST API).
    static const wchar_t* const kCorner[] = {L"Susp FL", L"Susp FR", L"Susp RL", L"Susp RR"};
    if (v.rest) {
      Reading(p, x, 28, w, L"Aero", v.aero);
      Reading(p, x, 46, w, suspPm > 0 ? kCorner[worstSusp] : L"Suspension", suspPm);
    } else {
      p.Text(x, 28, w, 34, L"aero / suspension: enable the\nLMU REST API in Settings > General", Font::Small, Col::Dim);
    }

    // Lines 4-5: what it costs per lap, and where.
    if (v.contacts > 0) p.Textf(x, 68, w, 18, Font::Small, Col::Dim, Align::Left, L"Lost per lap  ·  %d contact%ls",
                                v.contacts, v.contacts == 1 ? L"" : L"s");
    else p.Text(x, 68, w, 18, L"Lost per lap", Font::Small, Col::Dim);
    if (v.lossMs != INT_MIN) p.Textf(x, 68, w, 18, Font::TextBold, LossColor(v.lossMs), Align::Right, L"%+.2f s", v.lossMs / 1000.0);
    else p.Text(x, 68, w, 18, v.damaged ? L"after next clean lap" : L"—", Font::Small, Col::Dim, Align::Right);
    if (!v.damaged) return;
    wchar_t buf[96];
    if (v.straightState == 2) {
      const int corners = v.lossMs != INT_MIN ? v.lossMs - v.straightMs : INT_MIN;
      if (corners != INT_MIN)
        swprintf_s(buf, L"straights %+.2f s (top %+.0f km/h)  ·  corners %+.2f s", v.straightMs / 1000.0, -v.topKph / 10.0,
                   corners / 1000.0);
      else swprintf_s(buf, L"straights %+.2f s / lap  ·  top speed %+.0f km/h", v.straightMs / 1000.0, -v.topKph / 10.0);
    } else {
      wcscpy_s(buf, v.straightState == 1 ? L"straights: measuring (drive the straights flat out)"
                                         : L"straights: needs a clean lap before the damage");
    }
    p.Text(x, 86, w, 16, buf, Font::Small, Col::Dim);
  }

private:
  D2D1_COLOR_F Severity(int s) const { return s >= 2 ? heavy_ : s == 1 ? light_ : ok_; }
  D2D1_COLOR_F ForPermille(int pm) const { return pm >= 250 ? heavy_ : pm > 0 ? light_ : Col::Text; }
  D2D1_COLOR_F LossColor(int ms) const { return ms > 300 ? heavy_ : ms > 50 ? light_ : Col::Text; }

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
    const D2D1_COLOR_F idle = Col::Rgb(0x5A6270);

    // Suspension: two arms from the body to each wheel, coloured by that corner's damage.
    // Tyres: grey, red ring = flat, outline only = wheel lost.
    const float wx[2] = {kCarX - kArm - kWheelW, kCarX + kCarW + kArm};
    const float wy[2] = {kCarY + 16 * kSY, kCarY + kCarH - 18 * kSY - kWheelH};
    for (int i = 0; i < 4; ++i) {
      const bool left = i % 2 == 0;
      const float x = wx[i % 2], y = wy[i / 2];
      const D2D1_COLOR_F arm = v.susp[i] > 0 ? ForPermille(v.susp[i]) : idle;
      const float inner = left ? x + kWheelW : x, body = left ? kCarX + 6 * kSX : kCarX + kCarW - 6 * kSX;
      for (float dy : {4.f, kWheelH - 4.f}) {
        const D2D1_POINT_2F pts[2] = {{inner, y + dy}, {body, y + kWheelH / 2}};
        p.Polyline(pts, 2, arm, v.susp[i] > 0 ? 2.f : 1.2f);
      }
      if (v.wheelOff[i]) p.Outline(x, y, kWheelW, kWheelH, 2, heavy_, 1.5f);
      else p.FillRounded(x, y, kWheelW, kWheelH, 2, Col::Rgb(0x9AA3B2));
      if (v.flat[i]) p.Outline(x - 2, y - 2, kWheelW + 4, kWheelH + 4, 3, heavy_, 1.5f);
    }
    if (!body_) return;

    // Body panels (dents), masked by the car's shape.
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

    // Aero: front splitter and rear wing.
    const D2D1_COLOR_F aero = v.aero > 0 ? ForPermille(v.aero) : idle;
    p.FillRounded(kCarX + 10 * kSX, kCarY - 6, 44 * kSX, 3, 1.5f, aero);
    p.FillRounded(kCarX - 3, kCarY + kCarH + 2, kCarW + 6, 4, 2, aero);
  }

  // Counts contacts this session and remembers the last one (LMU only reports the latest impact).
  void TrackContacts(const TelemInfoV01& t) {
    const double et = t.mElapsedTime;
    if (et < lastET_ - 1.0) { contacts_ = 0; seenImpactET_ = -1; } // new session / restart
    lastET_ = et;
    if (seenImpactET_ < 0) { seenImpactET_ = t.mLastImpactET; return; } // not one from before we looked
    if (t.mLastImpactET > seenImpactET_ + 0.05) {
      seenImpactET_ = t.mLastImpactET;
      if (t.mLastImpactMagnitude >= minImpact_) {
        ++contacts_;
        contactET_ = t.mLastImpactET;
        impact_ = static_cast<int>(std::lround(t.mLastImpactMagnitude));
      }
    }
  }

  void Build(const Model& m, View& v) const {
    const TelemInfoV01& t = *m.telem;
    v.valid = true;
    v.contacts = contacts_;
    if (showContact_ && contactET_ > 0 && t.mElapsedTime - contactET_ < contactS_) {
      v.contact = true;
      v.impact = impact_;
    }
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
    v.detached = t.mDetached;
    v.damaged = dents > 0 || t.mDetached || v.aero > 0 || v.repairS > 0;
    for (int i = 0; i < 4; ++i) v.damaged |= v.flat[i] || v.wheelOff[i] || v.susp[i] > 0;

    if (t.mOverheating) v.status = Status::Overheating;
    else if (t.mDetached) v.status = Status::Detached;
    else if (v.repairS > 0) v.status = Status::Repair;
    else if (v.damaged) v.status = Status::Damaged;

    // Your average clean lap since the damage minus the average before it.
    if (v.damaged && m.timing && m.timing->PaceBeforeDamage() > 0 && m.timing->PaceSinceDamage() > 0)
      v.lossMs = static_cast<int>(std::lround((m.timing->PaceSinceDamage() - m.timing->PaceBeforeDamage()) * 1000.0));
    // On the straights: acceleration before / since the damage at the same speeds.
    if (v.damaged) {
      const StraightLine::Result& st = m.straightLoss;
      v.straightState = st.valid ? 2 : m.straight.HasReference() ? 1 : 0;
      if (st.valid) {
        v.straightMs = Quantize(st.timeLoss, 0.01) * 10; // 0.01 s steps: the measurement isn't finer
        v.topKph = Quantize(st.topSpeedLoss, 1.0) * 10;
      }
    }
  }

  bool showClean_, showContact_;
  int contactS_, minImpact_;
  D2D1_COLOR_F light_, heavy_, ok_;
  double lastET_ = -1, seenImpactET_ = -1, contactET_ = -1;
  int contacts_ = 0, impact_ = 0;
  ComPtr<ID2D1PathGeometry> body_;
  ID2D1Factory* factory_ = nullptr;
  View view_;
};

} // namespace

const WidgetType kDamageWidget{
  "damage", "Damage",
  "Kinds of damage (bodywork, aero, suspension, tyres), how much, repair time, the lap time you're losing "
  "(straights / corners), and contacts.",
  true, 1544, 750, 250, kOptions, CreateWidget<DamageWidget>};
