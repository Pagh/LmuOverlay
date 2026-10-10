// Lap history: your last laps with time, gap to your session best, and what each lap used
// (fuel, virtual energy, tyre tread). The top row is the lap in progress: fuel / energy used so far
// compared with your last clean lap at the same point (green = using less).
#include "widgets/Widget.h"
#include <algorithm>
#include <climits>
#include <cmath>

namespace {

constexpr float kPad = 8.f, kHeadH = 18.f, kRowH = 20.f;
constexpr float kLapW = 30.f, kTimeW = 74.f, kDeltaW = 58.f, kFuelW = 52.f, kEnergyW = 54.f, kWearW = 48.f;
constexpr int kNone = INT_MIN;

struct Row {
  int lap = 0;
  int timeMs = 0;
  bool valid = true, pit = false, best = false;
  int deltaMs = kNone;          // to your session best
  int fuelCl = kNone;           // 0.01 L
  int energyCp = kNone;         // 0.01 %
  int wearCp = kNone;           // 0.01 %
  bool operator==(const Row&) const = default;
};

struct View {
  bool valid = false;
  bool energyCar = false;
  int count = 0;
  Row rows[Model::kLapHistory];
  int liveLap = 0;
  int fuelVsCl = kNone;         // this lap so far vs last clean lap, 0.01 L
  int energyVsCp = kNone;       // 0.01 %
  bool operator==(const View&) const = default;
};

const OptionDef kOptions[] = {
  OptInt("laps", "Laps shown", 5, 1, Model::kLapHistory),
  OptBool("show_live", "Lap in progress vs last lap", true,
          "Top row: fuel / energy used so far this lap minus what your last clean lap had used at the same point."),
  OptBool("show_delta", "Gap to your session best", true),
  OptBool("show_fuel", "Fuel used", true),
  OptBool("show_energy", "Virtual energy used", true),
  OptBool("show_wear", "Tyre wear per lap", true, "Tread used in the lap, average of the four tyres"),
  OptColor("less", "Using less than last lap", 0x3DDC84FF),
  OptColor("more", "Using more than last lap", 0xFFC23DFF),
};

class LapHistoryWidget final : public Widget {
public:
  LapHistoryWidget(const WidgetType& t, const OptionSet& o)
      : Widget(t, o),
        rows_(o.Int("laps")),
        live_(o.Bool("show_live")),
        delta_(o.Bool("show_delta")),
        fuel_(o.Bool("show_fuel")),
        energy_(o.Bool("show_energy")),
        wear_(o.Bool("show_wear")),
        less_(o.Color("less")),
        more_(o.Color("more")) {}

  float Width() const override {
    return 2 * kPad + kLapW + kTimeW + (delta_ ? kDeltaW : 0) + (fuel_ ? kFuelW : 0) + (energy_ ? kEnergyW : 0) +
           (wear_ ? kWearW : 0);
  }
  float Height() const override { return 2 * kPad - 2 + kHeadH + kRowH * (rows_ + (live_ ? 1 : 0)); }

  bool Prepare(const Model& m) override {
    View v;
    if (m.onTrack && m.player && m.timing) Build(m, v);
    if (v == view_) return false;
    view_ = v;
    return true;
  }

  void Draw(Painter& p) override {
    const float w = Width(), h = Height();
    p.Panel(w, h);
    const View& v = view_;
    if (!v.valid) { p.Text(0, 0, w, h, L"LAP HISTORY", Font::TextBold, Col::Dim, Align::Center); return; }

    float y = kPad - 2;
    Columns([&](int c, float x, float cw) {
      static const wchar_t* const kHead[] = {L"LAP", L"TIME", L"Δ BEST", L"FUEL", L"ENERGY", L"TYRE"};
      p.Text(x, y, cw, kHeadH, kHead[c], Font::Small, Col::Dim, c == 0 ? Align::Left : Align::Right);
    });
    y += kHeadH;

    if (live_) {
      p.FillRounded(kPad - 4, y + 1, w - 2 * kPad + 8, kRowH - 2, 3, Col::Row);
      Columns([&](int c, float x, float cw) {
        switch (c) {
          case 0: p.Textf(x, y, cw, kRowH, Font::TextBold, Col::Text, Align::Left, L"%d", v.liveLap); break;
          case 1: p.Text(x, y, cw, kRowH, L"now", Font::Small, Col::Dim, Align::Right); break;
          case 2: p.Text(x, y, cw, kRowH, L"vs last", Font::Small, Col::Dim, Align::Right); break;
          case 3: Versus(p, x, y, cw, v.fuelVsCl, L"%+.2f"); break;
          case 4: Versus(p, x, y, cw, v.energyCar ? v.energyVsCp : kNone, L"%+.2f%%"); break;
          default: break;
        }
      });
      y += kRowH;
    }

    if (v.count == 0) {
      p.Text(kPad, y, w - 2 * kPad, kRowH, L"no laps yet", Font::Small, Col::Dim, Align::Center);
      return;
    }
    for (int i = 0; i < v.count && i < rows_; ++i, y += kRowH) {
      const Row& r = v.rows[i];
      Columns([&](int c, float x, float cw) {
        wchar_t buf[24];
        switch (c) {
          case 0:
            p.Textf(x, y, cw, kRowH, Font::Text, Col::Dim, Align::Left, L"%d", r.lap);
            if (r.pit) p.Text(x + 18, y, 14, kRowH, L"P", Font::Small, Col::Warn, Align::Left);
            break;
          case 1:
            FormatTime(buf, 24, r.timeMs, 3);
            p.Text(x, y, cw, kRowH, buf, Font::Text, r.valid ? (r.best ? Col::Purple : Col::Text) : Col::Dim, Align::Right);
            if (!r.valid && r.timeMs > 0) { // struck through
              const float tw = p.Measure(buf, Font::Text);
              p.Fill(x + cw - tw, y + kRowH / 2, tw, 1.f, Col::Bad);
            }
            break;
          case 2:
            if (!r.valid) p.Text(x, y, cw, kRowH, L"invalid", Font::Small, Col::Bad, Align::Right);
            else if (r.best) p.Text(x, y, cw, kRowH, L"best", Font::Small, Col::Purple, Align::Right);
            else if (r.deltaMs != kNone) {
              FormatGap(buf, 24, r.deltaMs, 3, L"+");
              p.Text(x, y, cw, kRowH, buf, Font::Text, Col::Dim, Align::Right);
            } else p.Text(x, y, cw, kRowH, L"—", Font::Text, Col::Dim, Align::Right);
            break;
          case 3:
            if (r.fuelCl == kNone) Dash(p, x, y, cw);
            else if (r.fuelCl < 0) p.Text(x, y, cw, kRowH, L"refuel", Font::Small, Col::Warn, Align::Right);
            else p.Textf(x, y, cw, kRowH, Font::Text, Col::Text, Align::Right, L"%.2f", r.fuelCl / 100.0);
            break;
          case 4:
            if (r.energyCp == kNone || !v.energyCar) Dash(p, x, y, cw);
            else if (r.energyCp < 0) p.Text(x, y, cw, kRowH, L"refill", Font::Small, Col::Warn, Align::Right);
            else p.Textf(x, y, cw, kRowH, Font::Text, Col::Info, Align::Right, L"%.1f%%", r.energyCp / 100.0);
            break;
          case 5:
            if (r.wearCp == kNone) Dash(p, x, y, cw);
            else if (r.wearCp < 0) p.Text(x, y, cw, kRowH, L"new", Font::Small, Col::Dim, Align::Right);
            else p.Textf(x, y, cw, kRowH, Font::Text, Col::Dim, Align::Right, r.wearCp < 100 ? L"%.2f%%" : L"%.1f%%",
                         r.wearCp / 100.0);
            break;
        }
      });
    }
  }

private:
  // Calls f(column, x, width) for every column shown.
  template <typename F>
  void Columns(F&& f) const {
    float x = kPad;
    const float widths[] = {kLapW, kTimeW, kDeltaW, kFuelW, kEnergyW, kWearW};
    const bool shown[] = {true, true, delta_, fuel_, energy_, wear_};
    for (int c = 0; c < 6; ++c) {
      if (!shown[c]) continue;
      f(c, x, widths[c] - 2.f);
      x += widths[c];
    }
  }

  static void Dash(Painter& p, float x, float y, float w) { p.Text(x, y, w, kRowH, L"—", Font::Text, Col::Dim, Align::Right); }

  // Signed difference to the last lap (hundredths): less is green, more is amber, ~0 neutral.
  void Versus(Painter& p, float x, float y, float w, int v, const wchar_t* fmt) const {
    if (v == kNone) { Dash(p, x, y, w); return; }
    const D2D1_COLOR_F c = v < -1 ? less_ : v > 1 ? more_ : Col::Text;
    p.Textf(x, y, w, kRowH, Font::TextBold, c, Align::Right, fmt, v / 100.0);
  }

  void Build(const Model& m, View& v) const {
    v.valid = true;
    v.energyCar = m.race.usesEnergy;
    v.liveLap = m.player->mTotalLaps + 1;
    double d;
    if (live_ && m.fuel.VsLastLap(m.lapFrac, d)) v.fuelVsCl = Quantize(d, 0.01);
    if (live_ && v.energyCar && m.energy.VsLastLap(m.lapFrac, d)) v.energyVsCp = Quantize(d * 100.0, 0.01);
    const double best = m.timing->PlayerSession().lap;
    v.count = std::min(m.lapCount, rows_);
    for (int i = 0; i < v.count; ++i) {
      const LapRecord& l = m.laps[i];
      Row& r = v.rows[i];
      r.lap = l.lap;
      r.timeMs = l.time > 0 ? static_cast<int>(std::lround(l.time * 1000.0)) : 0;
      r.valid = l.valid && l.time > 0;
      r.pit = l.pit;
      if (r.valid && best > 0) {
        r.best = std::fabs(l.time - best) < 5e-4;
        r.deltaMs = std::max(0, static_cast<int>(std::lround((l.time - best) * 1000.0)));
      }
      // Negative use = refuelled / new tyres during the lap (shown as such, -1).
      if (l.hasUse) {
        r.fuelCl = l.fuel < 0 ? -1 : Quantize(l.fuel, 0.01);
        r.wearCp = l.wear < 0 ? -1 : Quantize(l.wear * 100.0, 0.01);
      }
      if (l.hasEnergy) r.energyCp = l.energy < 0 ? -1 : Quantize(l.energy * 100.0, 0.01);
    }
  }

  int rows_;
  bool live_, delta_, fuel_, energy_, wear_;
  D2D1_COLOR_F less_, more_;
  View view_;
};

} // namespace

const WidgetType kLapHistoryWidget{
  "laps", "Lap history", "Your last laps: time, gap to your best, fuel / energy / tyre use per lap, and the lap in "
  "progress compared with the last one.",
  true, 1576, 520, 250, kOptions, CreateWidget<LapHistoryWidget>};
