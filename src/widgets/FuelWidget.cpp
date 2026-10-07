// Fuel and virtual energy: amount, average use per lap, laps left, and how much
// extra is needed to reach the end of the session.
#include "widgets/Widget.h"
#include <cmath>

namespace {

constexpr float kW = 300.f, kH = 132.f, kPad = 8.f, kRowH = 20.f;
constexpr float kLabelW = 78.f, kColW = (kW - 2 * kPad - kLabelW) / 2.f;

struct Column {
  bool present = false;
  bool hasRate = false;
  int now = 0;        // 0.1 L or 0.1 %
  int perLap = 0;     // 0.01 L or 0.01 %
  int lapsLeft = 0;   // 0.1 laps
  int need = 0;       // 0.1 L or 0.1 % extra needed to finish (<= 0 means enough)
  bool operator==(const Column&) const = default;
};

struct View {
  bool valid = false;
  int lapsToGo = -1;  // 0.1 laps
  Column fuel, energy;
  bool operator==(const View&) const = default;
};

const OptionDef kOptions[] = {
  OptBool("show_energy", "Show virtual energy column", true),
  OptBool("show_laps_to_go", "Show laps to go", true),
  OptColor("short", "Not enough to finish", 0xFF5A5AFF),
  OptColor("enough", "Enough to finish", 0x3DDC84FF),
};

class FuelWidget final : public Widget {
public:
  FuelWidget(const WidgetType& t, const OptionSet& o)
      : Widget(t, o),
        showEnergy_(o.Bool("show_energy")),
        showToGo_(o.Bool("show_laps_to_go")),
        short_(o.Color("short")),
        enough_(o.Color("enough")) {}

  float Width() const override { return showEnergy_ ? kW : kW - kColW; }
  float Height() const override { return kH; }

  bool Prepare(const Model& m) override {
    View v;
    if (m.onTrack && m.telem) Build(m, v);
    if (v == view_) return false;
    view_ = v;
    return true;
  }

  void Draw(Painter& p) override {
    const float w = Width();
    p.Panel(w, kH);
    if (!view_.valid) {
      p.Text(0, 0, w, kH, showEnergy_ ? L"FUEL / ENERGY" : L"FUEL", Font::TextBold, Col::Dim, Align::Center);
      return;
    }
    const float x1 = kPad + kLabelW, x2 = x1 + kColW;
    float y = kPad;
    p.Text(x1, y, kColW, kRowH, L"FUEL", Font::Small, Col::Dim, Align::Right);
    if (showEnergy_) p.Text(x2, y, kColW, kRowH, L"ENERGY", Font::Small, Col::Dim, Align::Right);
    y += kRowH;

    const wchar_t* labels[] = {L"Now", L"Per lap", L"Laps left", L"To finish"};
    for (int r = 0; r < 4; ++r, y += kRowH) {
      p.Text(kPad, y, kLabelW, kRowH, labels[r], Font::Text, Col::Dim);
      Cell(p, x1, y, view_.fuel, r, L"L");
      if (showEnergy_) Cell(p, x2, y, view_.energy, r, L"%");
    }
    if (showToGo_ && view_.lapsToGo >= 0)
      p.Textf(kPad, y + 2, w - 2 * kPad, kRowH, Font::Small, Col::Dim, Align::Right, L"%.1f laps to go", view_.lapsToGo / 10.0);
  }

private:
  void Cell(Painter& p, float x, float y, const Column& c, int row, const wchar_t* unit) const {
    if (!c.present) { p.Text(x, y, kColW, kRowH, L"—", Font::Text, Col::Dim, Align::Right); return; }
    switch (row) {
      case 0: p.Textf(x, y, kColW, kRowH, Font::TextBold, Col::Text, Align::Right, L"%.1f %ls", c.now / 10.0, unit); break;
      case 1:
        if (c.hasRate) p.Textf(x, y, kColW, kRowH, Font::Text, Col::Text, Align::Right, L"%.2f", c.perLap / 100.0);
        else p.Text(x, y, kColW, kRowH, L"—", Font::Text, Col::Dim, Align::Right);
        break;
      case 2:
        if (c.hasRate) p.Textf(x, y, kColW, kRowH, Font::TextBold, Col::Text, Align::Right, L"%.1f", c.lapsLeft / 10.0);
        else p.Text(x, y, kColW, kRowH, L"—", Font::Text, Col::Dim, Align::Right);
        break;
      case 3:
        if (!c.hasRate) p.Text(x, y, kColW, kRowH, L"—", Font::Text, Col::Dim, Align::Right);
        else if (c.need > 0) p.Textf(x, y, kColW, kRowH, Font::TextBold, short_, Align::Right, L"+%.1f %ls", c.need / 10.0, unit);
        else p.Textf(x, y, kColW, kRowH, Font::Text, enough_, Align::Right, L"OK %.1f", -c.need / 10.0);
        break;
    }
  }

  static void Fill(Column& c, double amount, double perLap, double lapsToGo) {
    c.present = true;
    c.now = Quantize(amount, 0.1);
    c.hasRate = perLap > 0;
    if (!c.hasRate) return;
    c.perLap = Quantize(perLap, 0.01);
    c.lapsLeft = Quantize(amount / perLap, 0.1);
    c.need = lapsToGo >= 0 ? Quantize(lapsToGo * perLap - amount, 0.1) : 0;
  }

  void Build(const Model& m, View& v) const {
    const TelemInfoV01& t = *m.telem;
    v.valid = true;
    v.lapsToGo = m.lapsToGo >= 0 ? Quantize(m.lapsToGo, 0.1) : -1;
    Fill(v.fuel, t.mFuel, m.fuel.PerLap(), m.lapsToGo);
    if (t.mVirtualEnergy > 0.f) // cars without virtual energy report 0
      Fill(v.energy, Model::Fraction(t.mVirtualEnergy) * 100.0, m.energy.PerLap() * 100.0, m.lapsToGo);
  }

  bool showEnergy_, showToGo_;
  D2D1_COLOR_F short_, enough_;
  View view_;
};

} // namespace

const WidgetType kFuelWidget{
  "fuel", "Fuel / Energy", "Detailed fuel / energy table (Strategy covers this in races; off by default).",
  false, 40, 660, 250, kOptions, CreateWidget<FuelWidget>};
