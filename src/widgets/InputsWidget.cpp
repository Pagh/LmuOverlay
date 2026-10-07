// Inputs & car: RPM / shift light, gear, speed, driver aids, brake bias, water / oil
// temperature, and a throttle / brake trace of the last seconds.
#include "widgets/Widget.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr float kPad = 8.f, kH = 112.f, kGearW = 54.f, kCellW = 44.f, kCellGap = 3.f;
constexpr float kAidsW = 4 * kCellW + 3 * kCellGap;
constexpr int kSamples = 240;            // trace resolution

struct View {
  bool valid = false;
  int rpmPermille = 0;   // 0..1000 of rev limit, quantised to 0.5 %
  int gear = 0;
  int speed = 0;         // km/h or mph
  int tc = 0, tcMax = 0, cut = 0, cutMax = 0, slip = 0, slipMax = 0, abs = 0, absMax = 0, map = 0, mapMax = 0;
  int bbTenths = 0;      // front brake bias, 0.1 %
  int water = 0, oil = 0;
  bool tcActive = false, absActive = false, limiter = false;
  int battery = -1;      // %, -1 = no hybrid
  int motorState = 0;    // 0 n/a, 1 idle, 2 deploy, 3 regen
  uint32_t traceSeq = 0; // changes with every new trace sample
  bool operator==(const View&) const = default;
};

const OptionDef kOptions[] = {
  OptChoice("speed_unit", "Speed unit", "km/h|mph", 0),
  OptBool("show_aids", "Driver aids, bias, temperatures", true),
  OptBool("show_trace", "Throttle / brake trace", true),
  OptInt("trace_seconds", "Trace length (s)", 12, 3, 30),
  OptInt("trace_width", "Trace width (px)", 340, 120, 1200),
  OptBool("show_steering", "Steering in the trace", false),
  OptFloat("shift_yellow", "Shift light: yellow at", 0.85, 0.5, 1.0, "Fraction of the rev limit"),
  OptFloat("shift_red", "Shift light: red at", 0.93, 0.5, 1.0),
  OptFloat("shift_blue", "Shift light: shift now at", 0.97, 0.5, 1.0),
  OptInt("water_hot", "Water hot above (°C)", 105, 60, 150),
  OptInt("oil_hot", "Oil hot above (°C)", 125, 60, 180),
  OptColor("throttle", "Throttle", 0x3DDC84FF),
  OptColor("brake", "Brake", 0xFF4D4DFF),
  OptColor("steering", "Steering", 0x9AA3B2FF),
};

class InputsWidget final : public Widget {
public:
  InputsWidget(const WidgetType& t, const OptionSet& o)
      : Widget(t, o),
        mph_(o.Choice("speed_unit") == 1),
        aids_(o.Bool("show_aids")),
        trace_(o.Bool("show_trace")),
        traceSeconds_(o.Int("trace_seconds")),
        traceW_(static_cast<float>(o.Int("trace_width"))),
        steering_(o.Bool("show_steering")),
        yellow_(o.Float("shift_yellow")),
        red_(o.Float("shift_red")),
        blue_(o.Float("shift_blue")),
        waterHot_(o.Int("water_hot")),
        oilHot_(o.Int("oil_hot")),
        throttle_(o.Color("throttle")),
        brake_(o.Color("brake")),
        steer_(o.Color("steering")) {}

  float Width() const override { return kPad + kGearW + (aids_ ? kAidsW + 10 : 0) + (trace_ ? traceW_ + 10 : 0) + kPad - 2; }
  float Height() const override { return aids_ ? kH : 84.f; }

  bool Prepare(const Model& m) override {
    View v;
    if (m.onTrack && m.telem) {
      Build(*m.telem, v);
      Sample(m, *m.telem);
      v.traceSeq = seq_;
    }
    if (v == view_) return false;
    view_ = v;
    return true;
  }

  void Draw(Painter& p) override {
    const float w = Width(), h = Height();
    p.Panel(w, h);
    if (!view_.valid) {
      p.Text(0, 0, w, h, L"INPUTS", Font::TextBold, Col::Dim, Align::Center);
      return;
    }
    const View& v = view_;

    // RPM bar with shift-light colours.
    const float frac = v.rpmPermille / 1000.f;
    const D2D1_COLOR_F rpmCol = frac >= blue_ ? Col::Info : frac >= red_ ? Col::Bad : frac >= yellow_ ? Col::Warn : Col::Good;
    p.HBar(kPad, 6, w - 2 * kPad, 4, frac, rpmCol);

    // Gear + speed.
    const D2D1_COLOR_F gc = v.limiter ? Col::Warn : Col::Text;
    const float gy = h > 100 ? 16.f : 12.f;
    if (v.gear < 0) p.Text(kPad, gy, kGearW, 46, L"R", Font::Huge, gc, Align::Center);
    else if (v.gear == 0) p.Text(kPad, gy, kGearW, 46, L"N", Font::Huge, gc, Align::Center);
    else p.Textf(kPad, gy, kGearW, 46, Font::Huge, gc, Align::Center, L"%d", v.gear);
    p.Textf(kPad, gy + 46, kGearW, 18, Font::TextBold, Col::Text, Align::Center, L"%d", v.speed);
    if (h > 100 || v.limiter)
      p.Text(kPad, gy + 60, kGearW, 14, v.limiter ? L"PIT LIM" : mph_ ? L"mph" : L"km/h", Font::Small,
             v.limiter ? Col::Warn : Col::Dim, Align::Center);
    float x = kPad + kGearW + 8;

    if (aids_) {
      const float y1 = 16.f, y2 = 60.f;
      Cell(p, x + 0 * (kCellW + kCellGap), y1, L"TC", v.tc, v.tcMax, v.tcActive);
      Cell(p, x + 1 * (kCellW + kCellGap), y1, L"CUT", v.cut, v.cutMax, false);
      Cell(p, x + 2 * (kCellW + kCellGap), y1, L"SLIP", v.slip, v.slipMax, false);
      Cell(p, x + 3 * (kCellW + kCellGap), y1, L"ABS", v.abs, v.absMax, v.absActive);
      wchar_t buf[16];
      swprintf_s(buf, L"%.1f", v.bbTenths / 10.0);
      TextCell(p, x + 0 * (kCellW + kCellGap), y2, L"BIAS", buf, Col::Text);
      Cell(p, x + 1 * (kCellW + kCellGap), y2, L"MAP", v.map, v.mapMax, false);
      swprintf_s(buf, L"%d°", v.water);
      TextCell(p, x + 2 * (kCellW + kCellGap), y2, L"WATER", buf, v.water > waterHot_ ? Col::Bad : Col::Text);
      if (v.battery >= 0) {
        swprintf_s(buf, L"%d%%", v.battery);
        const D2D1_COLOR_F c = v.motorState == 3 ? Col::Good : v.motorState == 2 ? Col::Info : Col::Text;
        TextCell(p, x + 3 * (kCellW + kCellGap), y2, v.motorState == 3 ? L"REGEN" : v.motorState == 2 ? L"BOOST" : L"BATT", buf, c);
      } else {
        swprintf_s(buf, L"%d°", v.oil);
        TextCell(p, x + 3 * (kCellW + kCellGap), y2, L"OIL", buf, v.oil > oilHot_ ? Col::Bad : Col::Text);
      }
      x += kAidsW + 10;
    }
    if (trace_) DrawTrace(p, x, 14.f, traceW_, h - 14.f - kPad + 2);
  }

private:
  static void Cell(Painter& p, float x, float y, const wchar_t* label, int val, int max, bool active) {
    p.FillRounded(x, y, kCellW, 40, 3, active ? Col::Rgb(0x8A6A12) : Col::Row);
    p.Text(x, y + 1, kCellW, 14, label, Font::Small, Col::Dim, Align::Center);
    if (max > 0) p.Textf(x, y + 15, kCellW, 22, Font::TextBold, Col::Text, Align::Center, L"%d", val);
    else p.Text(x, y + 15, kCellW, 22, L"—", Font::Text, Col::Dim, Align::Center);
  }
  static void TextCell(Painter& p, float x, float y, const wchar_t* label, const wchar_t* val, D2D1_COLOR_F c) {
    p.FillRounded(x, y, kCellW, 40, 3, Col::Row);
    p.Text(x, y + 1, kCellW, 14, label, Font::Small, Col::Dim, Align::Center);
    p.Text(x, y + 15, kCellW, 22, val, Font::TextBold, c, Align::Center);
  }

  void DrawTrace(Painter& p, float x, float y, float w, float h) {
    p.Text(x, y - 2, w, 14, L"THROTTLE", Font::Small, throttle_);
    p.Text(x + 58, y - 2, w, 14, L"BRAKE", Font::Small, brake_);
    p.Textf(x, y - 2, w, 14, Font::Small, Col::Dim, Align::Right, L"last %d s", traceSeconds_);
    const float ty = y + 14, th = h - 14;
    p.Fill(x, ty + th / 2, w, 1, Col::Rgb(0x2B313C));
    for (int i = 1; i < 6; ++i) p.Fill(x + w * i / 6.f, ty, 1, th, Col::Rgb(0x232831));
    if (count_ < 2) return;
    // Oldest sample on the left, newest on the right edge.
    static D2D1_POINT_2F pts[kSamples];
    auto plot = [&](const uint8_t* src, D2D1_COLOR_F c, float stroke, bool centered) {
      for (int i = 0; i < count_; ++i) {
        const int k = (head_ - count_ + i + kSamples) % kSamples;
        const float vx = x + w * (kSamples - count_ + i) / static_cast<float>(kSamples - 1);
        const float val = src[k] / 200.f;
        const float vy = centered ? ty + th * (1.f - val) : ty + 1 + (th - 2) * (1.f - val);
        pts[i] = {vx, vy};
      }
      p.Polyline(pts, count_, c, stroke);
    };
    if (steering_) plot(steer_buf_, steer_, 1.2f, true);
    plot(thr_, throttle_, 2.f, false);
    plot(brk_, brake_, 2.f, false);
  }

  void Sample(const Model& m, const TelemInfoV01& t) {
    const double step = static_cast<double>(traceSeconds_) / kSamples;
    if (m.now - lastSample_ < step && lastSample_ > 0) return;
    // After a pause (menus, monitor) start a fresh trace instead of joining across the gap.
    if (m.now - lastSample_ > 1.0) count_ = 0;
    lastSample_ = m.now;
    thr_[head_] = static_cast<uint8_t>(std::lround(std::clamp(t.mUnfilteredThrottle, 0.0, 1.0) * 200.0));
    brk_[head_] = static_cast<uint8_t>(std::lround(std::clamp(t.mUnfilteredBrake, 0.0, 1.0) * 200.0));
    steer_buf_[head_] = static_cast<uint8_t>(std::lround((std::clamp(t.mUnfilteredSteering, -1.0, 1.0) + 1.0) * 100.0));
    head_ = (head_ + 1) % kSamples;
    count_ = std::min(count_ + 1, kSamples);
    ++seq_;
  }

  void Build(const TelemInfoV01& t, View& v) const {
    v.valid = true;
    const double rev = t.mEngineMaxRPM > 0 ? t.mEngineRPM / t.mEngineMaxRPM : 0.0;
    v.rpmPermille = Quantize(std::clamp(rev, 0.0, 1.0) * 1000.0, 5.0) * 5;
    v.gear = t.mGear;
    const double speed = std::sqrt(t.mLocalVel.x * t.mLocalVel.x + t.mLocalVel.y * t.mLocalVel.y + t.mLocalVel.z * t.mLocalVel.z);
    v.speed = static_cast<int>(speed * (mph_ ? 2.23694 : 3.6) + 0.5);
    v.tc = t.mTC; v.tcMax = t.mTCMax;
    v.cut = t.mTCCut; v.cutMax = t.mTCCutMax;
    v.slip = t.mTCSlip; v.slipMax = t.mTCSlipMax;
    v.abs = t.mABS; v.absMax = t.mABSMax;
    v.map = t.mMotorMap; v.mapMax = t.mMotorMapMax;
    v.bbTenths = Quantize((1.0 - t.mRearBrakeBias) * 100.0, 0.1);
    v.water = static_cast<int>(std::lround(t.mEngineWaterTemp));
    v.oil = static_cast<int>(std::lround(t.mEngineOilTemp));
    v.tcActive = t.mTCActive;
    v.absActive = t.mABSActive;
    v.limiter = t.mSpeedLimiterActive;
    if (t.mElectricBoostMotorState != 0) {
      v.battery = Quantize(std::clamp(t.mBatteryChargeFraction, 0.0, 1.0) * 100.0, 1.0);
      v.motorState = t.mElectricBoostMotorState;
    }
  }

  bool mph_, aids_, trace_;
  int traceSeconds_;
  float traceW_;
  bool steering_;
  float yellow_, red_, blue_;
  int waterHot_, oilHot_;
  D2D1_COLOR_F throttle_, brake_, steer_;
  View view_;

  uint8_t thr_[kSamples]{}, brk_[kSamples]{}, steer_buf_[kSamples]{};
  int head_ = 0, count_ = 0;
  uint32_t seq_ = 0;
  double lastSample_ = 0;
};

} // namespace

const WidgetType kInputsWidget{
  "inputs", "Inputs & car", "Gear, speed, RPM, TC / ABS / map, brake bias, water / oil, and a throttle / brake trace.",
  true, 684, 872, 16, kOptions, CreateWidget<InputsWidget>};
