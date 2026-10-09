// Strategy. In races: when a pit stop is required (a full tank / 100 % energy can't reach the
// flag) it shows fuel / energy, laps to the end, the pit window, what to add and how long the
// stop takes; otherwise it shrinks to a one-line fuel check. In practice / qualifying: the plan
// for the race set in Settings > General > Race plan.
#include "widgets/Widget.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr float kW = 360.f, kH = 160.f, kPad = 10.f, kPillH = 30.f;

enum class Mode : uint8_t { Hidden, Waiting, Check, Plan, RacePlan, RacePlanWaiting };

struct View {
  Mode mode = Mode::Hidden;
  // Race plan (practice / qualifying)
  bool planByLaps = false;
  int planLength = 0;           // minutes or laps
  int planLaps = 0, pace = 0;   // laps, ms
  int fuelPerLapCl = 0, energyPerLapPm = 0; // 0.01 L, 0.1 %
  int fillFuelDl = 0, fillEnergyPm = 0;
  bool fillFull = false;
  int stopLap = 0;

  bool started = false;
  bool energy = false;          // car uses virtual energy
  bool energyLimited = false;
  int fuelDl = 0, fuelLaps = -1;     // 0.1 L, 0.1 laps
  int energyPm = 0, energyLaps = -1; // 0.1 %, 0.1 laps
  int toEnd = -1;               // 0.1 laps
  int totalLaps = 0;
  int onFull = -1;              // 0.1 laps
  int stops = 0;
  int windowOpen = 0, windowClose = 0, lap = 0;
  int addFuelDl = 0, addEnergyPm = 0;
  int refuelS = -1, tyresS = -1;
  int spare = 0;                // 0.1 laps (check mode)
  bool operator==(const View&) const = default;
};

const OptionDef kOptions[] = {
  OptBool("fuel_check", "One-line fuel check when no stop is needed", true),
  OptColor("ok", "Enough", 0x3DDC84FF),
  OptColor("warn", "Stop required / tight", 0xFFC23DFF),
  OptColor("bad", "Not enough", 0xFF5A5AFF),
};

class StrategyWidget final : public Widget {
public:
  StrategyWidget(const WidgetType& t, const OptionSet& o)
      : Widget(t, o), check_(o.Bool("fuel_check")), ok_(o.Color("ok")), warn_(o.Color("warn")), bad_(o.Color("bad")) {}

  float Width() const override { return kW; }
  float Height() const override { return kH; }

  bool Prepare(const Model& m) override {
    View v;
    if (m.onTrack && m.race.valid && m.race.race) Build(m, v);
    else if (m.onTrack && m.plan.valid) BuildPlan(m, v);
    if (v == view_) return false;
    view_ = v;
    return true;
  }

  void Draw(Painter& p) override {
    const View& v = view_;
    switch (v.mode) {
      case Mode::Hidden: if (p.EditMode()) DrawPlaceholder(p, kW, kH, L"STRATEGY"); return;
      case Mode::Waiting: DrawWaiting(p); return;
      case Mode::Check: DrawCheck(p); return;
      case Mode::Plan: DrawPlan(p); return;
      case Mode::RacePlan: DrawRacePlan(p); return;
      case Mode::RacePlanWaiting: {
        const float w = 330.f, y = kH - kPillH;
        PillBox(p, w);
        wchar_t len[24];
        PlanLength(len);
        p.Textf(kW - w + 10, y, w - 20, kPillH, Font::Small, Col::Dim, Align::Left,
                L"RACE PLAN %ls  ·  drive a clean lap to measure", len);
        return;
      }
    }
  }

private:
  // The pill sits at the bottom of the widget so it lines up with the bottom of the screen.
  void PillBox(Painter& p, float w) const {
    p.FillRounded(kW - w, kH - kPillH, w, kPillH, 6.f, Background());
    if (p.EditMode()) p.Outline(0, 0, kW, kH, 6.f, Col::EditOutline, 2.f);
  }

  void DrawWaiting(Painter& p) const {
    const float w = 230.f, y = kH - kPillH;
    PillBox(p, w);
    p.Text(kW - w + 10, y, w - 20, kPillH, L"FUEL  ·  measuring use per lap…", Font::Small, Col::Dim);
  }

  void DrawCheck(Painter& p) const {
    const View& v = view_;
    const float w = 300.f, x = kW - w + 10, y = kH - kPillH;
    PillBox(p, w);
    const bool ok = v.spare >= 0;
    const D2D1_COLOR_F c = !ok ? bad_ : v.spare < 5 ? warn_ : ok_;
    p.Text(x, y, 16, kPillH, ok ? L"✓" : L"!", Font::TextBold, c);
    float cx = x + 18;
    p.Text(cx, y, 50, kPillH, v.energy ? L"ENERGY" : L"FUEL", Font::Small, Col::Dim);
    cx += v.energy ? 46.f : 32.f;
    if (v.energy) p.Textf(cx, y, 60, kPillH, Font::TextBold, Col::Text, Align::Left, L"%.0f %%", v.energyPm / 10.0);
    else p.Textf(cx, y, 60, kPillH, Font::TextBold, Col::Text, Align::Left, L"%.0f L", v.fuelDl / 10.0);
    cx += 52;
    const int laps = v.energyLimited ? v.energyLaps : v.fuelLaps;
    p.Textf(cx, y, 80, kPillH, Font::Text, Col::Text, Align::Left, L"%.1f laps", laps / 10.0);
    if (ok) p.Textf(kW - 150, y, 140, kPillH, Font::Small, c, Align::Right, L"+%.1f lap spare", v.spare / 10.0);
    else p.Textf(kW - 150, y, 140, kPillH, Font::Small, c, Align::Right, L"short %.1f laps", -v.spare / 10.0);
  }

  void DrawPlan(Painter& p) const {
    const View& v = view_;
    p.Panel(kW, kH);
    p.Text(kPad, 4, 120, 20, L"STRATEGY", Font::Small, Col::Dim);
    wchar_t badge[32];
    if (v.stops == 1) wcscpy_s(badge, v.started ? L"1 STOP LEFT" : L"1 STOP REQUIRED");
    else swprintf_s(badge, v.started ? L"%d STOPS LEFT" : L"%d STOPS REQUIRED", v.stops);
    const float bw = p.Measure(badge, Font::Small) + 14;
    p.FillRounded(kW - kPad - bw, 6, bw, 16, 3, warn_);
    p.Text(kW - kPad - bw, 6, bw, 16, badge, Font::Small, Col::Rgb(0x1A1405), Align::Center);

    // Cells: energy (if used), fuel, laps to the end.
    const int cells = v.energy ? 3 : 2;
    const float gap = 5.f, cw = (kW - 2 * kPad - gap * (cells - 1)) / cells, cy = 26.f, ch = 46.f;
    float x = kPad;
    auto cell = [&](const wchar_t* label, const wchar_t* val, const wchar_t* sub, D2D1_COLOR_F smallCol) {
      p.FillRounded(x, cy, cw, ch, 4, Col::Row);
      p.Text(x + 7, cy + 1, cw - 10, 14, label, Font::Small, Col::Dim);
      p.Text(x + 7, cy + 13, cw - 10, 20, val, Font::Mid, Col::Text);
      p.Text(x + 7, cy + 31, cw - 10, 14, sub, Font::Small, smallCol);
      x += cw + gap;
    };
    wchar_t val[24], sub[32];
    if (v.energy) {
      swprintf_s(val, L"%.0f %%", v.energyPm / 10.0);
      swprintf_s(sub, L"%.1f laps", v.energyLaps / 10.0);
      cell(L"ENERGY", val, sub, v.energyLimited ? warn_ : Col::Dim);
    }
    swprintf_s(val, L"%.1f L", v.fuelDl / 10.0);
    if (v.fuelLaps >= 0) swprintf_s(sub, L"%.1f laps", v.fuelLaps / 10.0);
    else wcscpy_s(sub, L"—");
    cell(L"FUEL", val, sub, v.energyLimited ? Col::Dim : warn_);
    swprintf_s(val, L"%.1f", v.toEnd / 10.0);
    swprintf_s(sub, L"of ≈ %d laps", v.totalLaps);
    cell(L"TO THE END", val, sub, Col::Dim);

    float y = cy + ch + 6;
    constexpr float kRow = 19.f;
    auto row = [&](const wchar_t* label) { p.Text(kPad, y, 120, kRow, label, Font::Small, Col::Dim); };
    if (!v.started) {
      row(L"Full tank lasts");
      p.Textf(kPad, y, kW - 2 * kPad, kRow, Font::TextBold, Col::Text, Align::Right, L"%.1f laps", v.onFull / 10.0);
      y += kRow;
      row(L"Start with");
      p.Text(kPad, y, kW - 2 * kPad, kRow, L"full", Font::TextBold, Col::Text, Align::Right);
      y += kRow;
      row(L"Then stop around");
      p.Textf(kPad, y, kW - 2 * kPad, kRow, Font::TextBold, Col::Text, Align::Right, L"lap %d", v.windowClose);
      return;
    }
    row(L"Pit window");
    if (v.lap >= v.windowOpen) {
      p.Textf(kPad, y, kW - 2 * kPad, kRow, Font::TextBold, v.lap >= v.windowClose ? bad_ : ok_, Align::Right,
              v.lap >= v.windowClose ? L"BOX THIS LAP" : L"OPEN · latest lap %d", v.windowClose);
    } else {
      p.Textf(kPad, y, kW - 2 * kPad, kRow, Font::TextBold, Col::Text, Align::Right, L"laps %d – %d", v.windowOpen, v.windowClose);
    }
    y += kRow;
    row(L"At the stop add");
    if (v.energy) p.Textf(kPad, y, kW - 2 * kPad, kRow, Font::TextBold, Col::Text, Align::Right, L"%.0f %% energy  ·  %.0f L",
                          v.addEnergyPm / 10.0, v.addFuelDl / 10.0);
    else p.Textf(kPad, y, kW - 2 * kPad, kRow, Font::TextBold, Col::Text, Align::Right, L"%.1f L", v.addFuelDl / 10.0);
    y += kRow;
    row(L"Stop time");
    if (v.refuelS >= 0) {
      if (v.tyresS > 0)
        p.Textf(kPad, y, kW - 2 * kPad, kRow, Font::Text, Col::Text, Align::Right, L"refuel ≈ %d s  ·  tyres %d s", v.refuelS, v.tyresS);
      else p.Textf(kPad, y, kW - 2 * kPad, kRow, Font::Text, Col::Text, Align::Right, L"refuel ≈ %d s", v.refuelS);
    } else {
      p.Text(kPad, y, kW - 2 * kPad, kRow, L"needs the LMU REST API", Font::Small, Col::Dim, Align::Right);
    }
  }

  void PlanLength(wchar_t (&out)[24]) const {
    if (view_.planByLaps) swprintf_s(out, L"%d laps", view_.planLength);
    else if (view_.planLength % 60 == 0) swprintf_s(out, L"%d h", view_.planLength / 60);
    else swprintf_s(out, L"%d min", view_.planLength);
  }

  // Practice / qualifying: the race set in Settings > General, from this session's pace and use.
  void DrawRacePlan(Painter& p) const {
    const View& v = view_;
    p.Panel(kW, kH);
    wchar_t len[24], title[48];
    PlanLength(len);
    swprintf_s(title, L"RACE PLAN  ·  %ls", len);
    p.Text(kPad, 4, 220, 20, title, Font::Small, Col::Dim);
    wchar_t badge[24];
    if (v.stops == 0) wcscpy_s(badge, L"NO STOP");
    else if (v.stops == 1) wcscpy_s(badge, L"1 STOP");
    else swprintf_s(badge, L"%d STOPS", v.stops);
    const float bw = p.Measure(badge, Font::Small) + 14;
    p.FillRounded(kW - kPad - bw, 6, bw, 16, 3, v.stops == 0 ? ok_ : warn_);
    p.Text(kW - kPad - bw, 6, bw, 16, badge, Font::Small, Col::Rgb(0x1A1405), Align::Center);

    const float gap = 5.f, cw = (kW - 2 * kPad - gap * 2) / 3, cy = 26.f, ch = 46.f;
    float x = kPad;
    auto cell = [&](const wchar_t* label, const wchar_t* val, const wchar_t* sub) {
      p.FillRounded(x, cy, cw, ch, 4, Col::Row);
      p.Text(x + 7, cy + 1, cw - 10, 14, label, Font::Small, Col::Dim);
      p.Text(x + 7, cy + 13, cw - 10, 20, val, Font::Mid, Col::Text);
      p.Text(x + 7, cy + 31, cw - 10, 14, sub, Font::Small, Col::Dim);
      x += cw + gap;
    };
    wchar_t val[24], sub[32], t[16];
    swprintf_s(val, L"%d laps", v.planLaps);
    FormatTime(t, 16, v.pace, 1);
    swprintf_s(sub, L"at %ls", t);
    cell(L"RACE", val, sub);
    swprintf_s(val, L"%.2f L", v.fuelPerLapCl / 100.0);
    if (v.energy) swprintf_s(sub, L"%.1f %% energy", v.energyPerLapPm / 10.0);
    else wcscpy_s(sub, L"fuel");
    cell(L"PER LAP", val, sub);
    swprintf_s(val, L"%.1f", v.onFull / 10.0);
    wcscpy_s(sub, v.energyLimited ? L"laps · energy" : L"laps");
    cell(L"ONE TANK", val, sub);

    float y = cy + ch + 6;
    constexpr float kRow = 19.f;
    const float rw = kW - 2 * kPad;
    auto row = [&](const wchar_t* label) { p.Text(kPad, y, 140, kRow, label, Font::Small, Col::Dim); };
    row(L"Start with");
    if (v.fillFull) wcscpy_s(val, v.energy ? L"full  ·  100 %" : L"full");
    else if (v.energy) swprintf_s(val, L"%.1f L  ·  %.0f %%", v.fillFuelDl / 10.0, v.fillEnergyPm / 10.0);
    else swprintf_s(val, L"%.1f L", v.fillFuelDl / 10.0);
    p.Text(kPad, y, rw, kRow, val, Font::TextBold, Col::Text, Align::Right);
    y += kRow;
    if (v.stops == 0) {
      row(L"Spare at the flag");
      p.Textf(kPad, y, rw, kRow, Font::TextBold, v.spare < 5 ? warn_ : ok_, Align::Right, L"+%.1f laps", v.spare / 10.0);
      return;
    }
    row(L"First stop by");
    p.Textf(kPad, y, rw, kRow, Font::TextBold, Col::Text, Align::Right, L"lap %d", v.stopLap);
    y += kRow;
    row(v.stops == 1 ? L"At the stop add" : L"At each stop add");
    if (v.energy) p.Textf(kPad, y, rw, kRow, Font::TextBold, Col::Text, Align::Right, L"%.0f %% energy  ·  %.0f L",
                          v.addEnergyPm / 10.0, v.addFuelDl / 10.0);
    else p.Textf(kPad, y, rw, kRow, Font::TextBold, Col::Text, Align::Right, L"%.1f L", v.addFuelDl / 10.0);
    y += kRow;
    if (v.refuelS >= 0) {
      row(L"Stop time");
      if (v.tyresS > 0)
        p.Textf(kPad, y, rw, kRow, Font::Text, Col::Text, Align::Right, L"refuel ≈ %d s  ·  tyres %d s", v.refuelS, v.tyresS);
      else p.Textf(kPad, y, rw, kRow, Font::Text, Col::Text, Align::Right, L"refuel ≈ %d s", v.refuelS);
    }
  }

  void BuildPlan(const Model& m, View& v) const {
    const RacePlan& r = m.plan;
    v.planByLaps = r.byLaps;
    v.planLength = r.byLaps ? static_cast<int>(r.totalLaps) : r.minutes;
    if (!r.ready) { v.mode = Mode::RacePlanWaiting; return; }
    v.mode = Mode::RacePlan;
    v.energy = r.usesEnergy && r.energyPerLap > 0;
    v.energyLimited = r.energyLimited;
    v.planLaps = static_cast<int>(std::lround(r.totalLaps));
    v.pace = static_cast<int>(std::lround(r.pace * 1000.0));
    v.fuelPerLapCl = Quantize(r.fuelPerLap, 0.01);
    v.energyPerLapPm = Quantize(r.energyPerLap * 100.0, 0.1);
    v.onFull = Quantize(r.lapsOnFull, 0.1);
    v.stops = r.stops;
    v.fillFull = r.stops > 0;
    v.fillFuelDl = Quantize(r.fillFuel, 0.1);
    v.fillEnergyPm = Quantize(r.fillEnergy * 100.0, 0.1);
    v.spare = Quantize(r.spareLaps, 0.1);
    v.stopLap = r.stopLap;
    v.addFuelDl = Quantize(r.addFuel, 0.1);
    v.addEnergyPm = Quantize(r.addEnergy * 100.0, 0.1);
    if (r.stops > 0 && m.rest && m.rest->valid && (m.rest->fuelFillRate > 0 || m.rest->energyFillRate > 0)) {
      double t = 0;
      if (r.addFuel > 0 && m.rest->fuelFillRate > 0) t = m.rest->fuelInsert + r.addFuel / m.rest->fuelFillRate;
      if (r.addEnergy > 0 && m.rest->energyFillRate > 0) t = std::max(t, r.addEnergy / m.rest->energyFillRate);
      v.refuelS = static_cast<int>(std::lround(t));
      if (m.rest->tyreChange > 0) v.tyresS = static_cast<int>(std::lround(m.rest->tyreChange));
    }
  }

  void Build(const Model& m, View& v) const {
    const RaceInfo& r = m.race;
    v.started = r.started;
    v.energy = r.usesEnergy;
    v.energyLimited = r.energyLimited;
    v.lap = r.lap;
    v.totalLaps = static_cast<int>(std::lround(std::max(0.0, r.totalLaps)));
    v.fuelDl = Quantize(r.fuel, 0.1);
    if (r.fuelPerLap > 0) v.fuelLaps = Quantize(r.fuel / r.fuelPerLap, 0.1);
    v.energyPm = Quantize(r.energy * 100.0, 0.1);
    if (r.energyPerLap > 0) v.energyLaps = Quantize(r.energy / r.energyPerLap, 0.1);
    if (!r.stopKnown) { v.mode = Mode::Waiting; return; }
    v.toEnd = Quantize(r.lapsToGo, 0.1);
    v.onFull = Quantize(r.lapsOnFull, 0.1);
    if (!r.stopRequired) {
      if (!check_) return;
      v.mode = Mode::Check;
      v.spare = Quantize(r.started ? r.spareLaps : (r.lapsInTank - r.totalLaps), 0.1);
      return;
    }
    v.mode = Mode::Plan;
    v.stops = std::max(1, r.stopsLeft);
    v.windowOpen = r.windowOpen;
    v.windowClose = r.windowClose;
    if (!r.started) v.windowClose = static_cast<int>(std::floor(r.lapsOnFull));
    v.addFuelDl = Quantize(r.addFuel, 0.1);
    v.addEnergyPm = Quantize(r.addEnergy * 100.0, 0.1);
    if (m.rest && m.rest->valid && (m.rest->fuelFillRate > 0 || m.rest->energyFillRate > 0)) {
      double t = 0;
      if (r.addFuel > 0 && m.rest->fuelFillRate > 0) t = m.rest->fuelInsert + r.addFuel / m.rest->fuelFillRate;
      if (r.addEnergy > 0 && m.rest->energyFillRate > 0) t = std::max(t, r.addEnergy / m.rest->energyFillRate);
      v.refuelS = static_cast<int>(std::lround(t));
      if (m.rest->tyreChange > 0) v.tyresS = static_cast<int>(std::lround(m.rest->tyreChange));
    }
  }

  bool check_;
  D2D1_COLOR_F ok_, warn_, bad_;
  View view_;
};

} // namespace

const WidgetType kStrategyWidget{
  "strategy", "Strategy",
  "Races: pit plan when a stop is required, otherwise a one-line fuel check. Practice / qualifying: the plan for "
  "the race set in Settings > General > Race plan.",
  true, 1544, 846, 250, kOptions, CreateWidget<StrategyWidget>};
