// Tyres & brakes: surface temperature (with an inner / centre / outer strip), pressure,
// tread left and brake temperature per corner, plus an optional "ready to push" badge.
#include "widgets/Widget.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr float kPad = 5.f, kGap = 4.f, kBoxW = 103.f, kBoxH = 49.f, kBadgeH = 20.f;
constexpr float kW = kPad * 2 + kBoxW * 2 + kGap;

struct Wheel {
  int strip[3]{};     // °C left/centre/right
  int temp = 0;       // average surface °C
  int kpa10 = 0;      // 0.1 kPa
  int wear = 0;       // % tread remaining
  int brake = 0;      // °C
  bool flat = false;
  bool operator==(const Wheel&) const = default;
};

enum class Ready : uint8_t { Cold, Ready, Hot };

struct View {
  bool valid = false;
  Wheel w[4];
  Ready ready = Ready::Cold;
  bool operator==(const View&) const = default;
};

const OptionDef kOptions[] = {
  OptInt("cold_c", "Tyre cold below (°C)", 70, 0, 200),
  OptInt("hot_c", "Tyre hot above (°C)", 105, 0, 200),
  OptChoice("pressure_unit", "Pressure unit", "kPa|psi|bar", 0),
  OptBool("show_brakes", "Show brake temperature", true),
  OptInt("brake_hot_c", "Brake hot above (°C)", 800, 100, 1500),
  OptBool("show_ready", "\"Ready to push\" badge", false, "All four tyres in the window (handy in qualifying)"),
  OptColor("cold", "Cold", 0x4DA3FFFF),
  OptColor("ok", "In window", 0x3DDC84FF),
  OptColor("hot", "Hot", 0xFF5A5AFF),
};

class TyresWidget final : public Widget {
public:
  TyresWidget(const WidgetType& t, const OptionSet& o)
      : Widget(t, o),
        cold_(o.Int("cold_c")),
        hot_(o.Int("hot_c")),
        unit_(o.Choice("pressure_unit")),
        showBrakes_(o.Bool("show_brakes")),
        brakeHot_(o.Int("brake_hot_c")),
        showReady_(o.Bool("show_ready")),
        coldCol_(o.Color("cold")),
        okCol_(o.Color("ok")),
        hotCol_(o.Color("hot")) {}

  float Width() const override { return kW; }
  float Height() const override { return kPad * 2 + kBoxH * 2 + kGap + (showReady_ ? kBadgeH + 3 : 0); }

  bool Prepare(const Model& m) override {
    View v;
    if (m.onTrack && m.telem) Build(*m.telem, v);
    if (v == view_) return false;
    view_ = v;
    return true;
  }

  void Draw(Painter& p) override {
    p.Panel(kW, Height());
    if (!view_.valid) {
      p.Text(0, 0, kW, Height(), L"TYRES", Font::TextBold, Col::Dim, Align::Center);
      return;
    }
    float top = kPad;
    if (showReady_) {
      const wchar_t* txt = view_.ready == Ready::Ready ? L"READY TO PUSH" : view_.ready == Ready::Hot ? L"TOO HOT" : L"WARMING UP";
      const D2D1_COLOR_F c = view_.ready == Ready::Ready ? okCol_ : view_.ready == Ready::Hot ? hotCol_ : coldCol_;
      const float bw = 118.f;
      p.FillRounded((kW - bw) / 2, top, bw, kBadgeH - 2, 3, c);
      p.Text((kW - bw) / 2, top, bw, kBadgeH - 2, txt, Font::Small, Col::Rgb(0x0D1A12), Align::Center);
      top += kBadgeH + 3;
    }
    for (int i = 0; i < 4; ++i) {
      const float x = kPad + (i % 2) * (kBoxW + kGap), y = top + (i / 2) * (kBoxH + kGap);
      DrawWheel(p, x, y, view_.w[i]);
    }
  }

private:
  D2D1_COLOR_F TempColor(int c) const {
    if (c < cold_) return coldCol_;
    if (c > hot_) return hotCol_;
    return okCol_;
  }

  void DrawWheel(Painter& p, float x, float y, const Wheel& w) const {
    p.FillRounded(x, y, kBoxW, kBoxH, 4, Col::Row);
    const float sw = (kBoxW - 8) / 3.f;
    for (int k = 0; k < 3; ++k) p.Fill(x + 4 + k * sw, y + 3, sw - 1, 3, TempColor(w.strip[k]));

    if (w.flat) p.Text(x + 5, y + 7, kBoxW * 0.55f, 22, L"FLAT", Font::TextBold, Col::Bad);
    else p.Textf(x + 5, y + 7, kBoxW * 0.55f, 22, Font::Mid, TempColor(w.temp), Align::Left, L"%d°", w.temp);
    if (unit_ == 0) p.Textf(x, y + 7, kBoxW - 5, 22, Font::Text, Col::Text, Align::Right, L"%d", w.kpa10 / 10);
    else p.Textf(x, y + 7, kBoxW - 5, 22, Font::Text, Col::Text, Align::Right, unit_ == 1 ? L"%.1f" : L"%.2f",
                 w.kpa10 / 10.0 * (unit_ == 1 ? 0.145038 : 0.01));

    const D2D1_COLOR_F wearCol = w.wear < 30 ? Col::Bad : w.wear < 60 ? Col::Warn : Col::Dim;
    p.Textf(x + 5, y + 30, kBoxW * 0.5f, 16, Font::Small, wearCol, Align::Left, L"%d%%", w.wear);
    if (showBrakes_)
      p.Textf(x + kBoxW * 0.4f, y + 30, kBoxW * 0.6f - 5, 16, Font::Small, w.brake > brakeHot_ ? hotCol_ : Col::Dim,
              Align::Right, L"B %d°", w.brake);
  }

  void Build(const TelemInfoV01& t, View& v) const {
    v.valid = true;
    bool cold = false, hot = false;
    for (int i = 0; i < 4; ++i) {
      const TelemWheelV01& src = t.mWheel[i];
      Wheel& w = v.w[i];
      double sum = 0;
      for (int k = 0; k < 3; ++k) {
        const double c = src.mTemperature[k] - 273.15;
        w.strip[k] = static_cast<int>(std::lround(c));
        sum += c;
      }
      w.temp = static_cast<int>(std::lround(sum / 3.0));
      w.kpa10 = static_cast<int>(std::lround(src.mPressure * 10.0));
      w.wear = static_cast<int>(std::lround(std::clamp(src.mWear, 0.0, 1.0) * 100.0));
      // The SDK comment says Celsius, but the game reports Kelvin (TinyPedal converts it too).
      w.brake = static_cast<int>(std::lround(src.mBrakeTemp - 273.15));
      w.flat = src.mFlat;
      cold |= w.temp < cold_;
      hot |= w.temp > hot_;
    }
    v.ready = hot ? Ready::Hot : cold ? Ready::Cold : Ready::Ready;
  }

  int cold_, hot_, unit_;
  bool showBrakes_;
  int brakeHot_;
  bool showReady_;
  D2D1_COLOR_F coldCol_, okCol_, hotCol_;
  View view_;
};

} // namespace

const WidgetType kTyresWidget{
  "tyres", "Tyres & brakes", "Tyre temperature, pressure, tread left and brake temperature; optional ready-to-push badge.",
  true, 1306, 872, 200, kOptions, CreateWidget<TyresWidget>};
