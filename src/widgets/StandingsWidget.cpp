// Standings for your class with the session info on top: session, time left, race length in
// laps (estimated for timed races, also before the start), what to fill up with, whether a pit
// stop is required, and the conditions. Then the leaders plus the cars around you.
#include "widgets/Widget.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr int kMaxRows = 20;
constexpr float kRowH = 20.f, kPad = 6.f, kHeaderH = 18.f;
constexpr float kLine1H = 22.f, kLine2H = 16.f, kCardH = 50.f, kNoteH = 16.f;
constexpr float kSessionMaxH = kLine1H + kLine2H + 4 + kCardH + 4 + kNoteH + 6;
constexpr float kPosW = 28.f, kNumW = 40.f, kBrandW = 40.f, kNameW = 150.f, kTagW = 32.f, kLapW = 72.f, kGapW = 70.f;

struct Row {
  int classPlace = 0;
  wchar_t number[8]{};
  Brand brand;
  wchar_t name[33]{};
  int lastMs = 0, bestMs = 0;
  TimeTint lastTint = kTintNormal, bestTint = kTintNormal;
  int gapMs = 0;            // -1 = no time to compare (practice / qualifying without a lap)
  int lapsDown = 0;
  int pitstops = 0;
  CarState car;             // fuel / energy left, tyres
  bool pit = false;
  bool player = false;
  bool gapBefore = false;   // draw a separator above (rows skipped)
  bool operator==(const Row&) const = default;
};

// Session info (see RaceInfo in Model.h).
struct Session {
  long kind = 0;            // LMU session number
  bool race = false, started = true, timed = true;
  int phase = 5;
  int timeLeft = -1;        // seconds
  int lengthMin = 0;        // session length
  int lap = 0, totalLaps = -1, maxLaps = 0;
  int lapsLeft = -1;        // practice / qualifying: laps that still fit in the time
  int fuelLaps = -1;        // laps the tank lasts, 0.1
  int stop = 0;             // 0 unknown, 1 not needed, 2 required
  int stops = 0;
  int fillFuelDl = 0, fillEnergyPm = 0;
  int paceMs = 0;
  uint8_t paceSource = 0;
  int fuelPerLapCl = 0, energyPerLapCpm = 0; // 0.01 L, 0.01 %
  int onFullDl = 0;                          // 0.1 laps
  int trackC = 0, airC = 0, rainPct = 0;
  bool wet = false;
  bool operator==(const Session&) const = default;
};

struct View {
  bool valid = false;
  Session ses;
  wchar_t cls[33]{};
  int classCount = 0;
  int count = 0;
  Row rows[kMaxRows];
  bool operator==(const View&) const = default;
};

const OptionDef kOptions[] = {
  OptInt("max_rows", "Rows", 10, 4, kMaxRows),
  OptInt("top_rows", "Always show top", 3, 1, 5, "Leaders always listed before the cars around you"),
  OptBool("show_session", "Session info", true,
          "Session, time left, race length in laps, fuel to fill up with, pit stop required or not, conditions"),
  OptBool("show_conditions", "Track / air temperature and rain", true),
  OptBool("show_number", "Column: car number", true),
  OptBool("show_brand", "Column: car make", true),
  OptBool("show_best_lap", "Column: best lap", true),
  OptBool("show_last_lap", "Column: last lap", true),
  OptBool("show_fuel", "Column: fuel / virtual energy %", true, "Fuel left, then virtual energy (blue) for cars that have it"),
  OptBool("show_tyres", "Column: tyres", true, "Compound (front / rear if different) and tread left"),
  OptBool("show_pitstops", "Show pit stop count", true),
  OptChoice("gap_to", "Gap to", "leader|car ahead", 0,
            "Race: time behind on track. Practice / qualifying: difference between best laps."),
  OptInt("gap_decimals", "Gap decimals", 3, 1, 3),
  OptInt("time_decimals", "Lap time decimals", 3, 1, 3),
  OptColor("player_row", "Your row", 0x3A4458FF),
  OptColor("text", "Text", 0xF2F4F7FF),
  OptColor("class_best", "Fastest in class", 0xB87CFFFF),
  OptColor("personal_best", "Driver's own best", 0x3DDC84FF),
};


class StandingsWidget final : public Widget {
public:
  StandingsWidget(const WidgetType& t, const OptionSet& o)
      : Widget(t, o),
        maxRows_(o.Int("max_rows")),
        topRows_(o.Int("top_rows")),
        session_(o.Bool("show_session")),
        conditions_(o.Bool("show_conditions")),
        number_(o.Bool("show_number")),
        brand_(o.Bool("show_brand")),
        best_(o.Bool("show_best_lap")),
        last_(o.Bool("show_last_lap")),
        fuel_(o.Bool("show_fuel")),
        tyres_(o.Bool("show_tyres")),
        showStops_(o.Bool("show_pitstops")),
        toAhead_(o.Choice("gap_to") == 1),
        gapDecimals_(o.Int("gap_decimals")),
        timeDecimals_(o.Int("time_decimals")),
        playerRow_(o.Color("player_row")),
        text_(o.Color("text")),
        classBest_(o.Color("class_best")),
        personalBest_(o.Color("personal_best")) {}

  float Width() const override {
    return kPad * 2 + kPosW + (number_ ? kNumW : 0) + (brand_ ? kBrandW : 0) + kNameW + kTagW + (best_ ? kLapW : 0) + (last_ ? kLapW : 0) +
           (fuel_ ? kCarFuelW : 0) + (tyres_ ? kCarTyreW : 0) + kGapW;
  }
  float Height() const override { return kPad * 2 + (session_ ? kSessionMaxH : 0) + kHeaderH + kRowH * maxRows_; }

  bool Prepare(const Model& m) override {
    View v;
    if (m.onTrack) Build(m, v);
    if (v == view_) return false;
    view_ = v;
    return true;
  }

  void Draw(Painter& p) override {
    const float w = Width();
    if (!view_.valid) {
      p.Panel(w, Height());
      p.Text(0, 0, w, Height(), L"STANDINGS", Font::TextBold, Col::Dim, Align::Center);
      return;
    }
    // Only as tall as the content: the session block changes size before / after the start.
    const float sesH = session_ ? SessionHeight(view_.ses) : 0.f;
    p.Panel(w, kPad * 2 + sesH + kHeaderH + kRowH * view_.count);
    float y = kPad;
    if (session_) {
      DrawSession(p, w, y, view_.ses);
      y += sesH;
      p.Fill(kPad, y - 3, w - 2 * kPad, 1, Col::Rgb(0x8A93A3, 0.35f));
    }

    // Column headers.
    float x = kPad + kPosW;
    if (number_) { p.Text(x, y, kNumW, kHeaderH, L"#", Font::Small, Col::Dim, Align::Center); x += kNumW; }
    if (brand_) x += kBrandW;
    p.Textf(x + 6, y, kNameW + kTagW, kHeaderH, Font::Small, Col::Dim, Align::Left, L"%ls  ·  %d cars", view_.cls, view_.classCount);
    x += kNameW + kTagW;
    if (best_) { p.Text(x, y, kLapW, kHeaderH, L"BEST", Font::Small, Col::Dim, Align::Right); x += kLapW; }
    if (last_) { p.Text(x, y, kLapW, kHeaderH, L"LAST", Font::Small, Col::Dim, Align::Right); x += kLapW; }
    if (fuel_) { p.Text(x, y, kCarFuelW - 2, kHeaderH, L"FUEL · VE", Font::Small, Col::Dim, Align::Right); x += kCarFuelW; }
    if (tyres_) { p.Text(x, y, kCarTyreW - 2, kHeaderH, L"TYRES", Font::Small, Col::Dim, Align::Right); x += kCarTyreW; }
    p.Text(x, y, kGapW - 4, kHeaderH, toAhead_ ? L"INT" : L"GAP", Font::Small, Col::Dim, Align::Right);
    y += kHeaderH;

    for (int i = 0; i < view_.count; ++i, y += kRowH) {
      const Row& r = view_.rows[i];
      if (r.gapBefore) p.Fill(kPad, y, w - 2 * kPad, 1, Col::Rgb(0x8A93A3, 0.5f));
      if (r.player) p.FillRounded(3, y, w - 6, kRowH, 3, playerRow_);
      D2D1_COLOR_F text = text_;
      if (r.pit) text.a *= 0.5f;
      auto timeColor = [&](TimeTint t) { return t == kTintClass ? classBest_ : t == kTintPersonal ? personalBest_ : text; };

      x = kPad;
      p.Textf(x, y, kPosW - 6, kRowH, Font::TextBold, text, Align::Right, L"%d", r.classPlace);
      x += kPosW;
      if (number_) {
        if (r.number[0]) {
          p.FillRounded(x + 4, y + 3, kNumW - 8, kRowH - 6, 3, Col::Rgb(0x2B313C, 0.95f));
          p.Text(x + 4, y, kNumW - 8, kRowH, r.number, Font::Small, text, Align::Center);
        }
        x += kNumW;
      }
      if (brand_) {
        DrawBrand(p, x + 3, y, kBrandW - 6, kRowH, r.brand);
        x += kBrandW;
      }
      p.Text(x + 6, y, kNameW - 6, kRowH, r.name, r.player ? Font::TextBold : Font::Text, text);
      x += kNameW;
      if (r.pit) p.Text(x, y, kTagW, kRowH, L"PIT", Font::Small, Col::Warn, Align::Center);
      else if (showStops_ && r.pitstops > 0) p.Textf(x, y, kTagW, kRowH, Font::Small, Col::Dim, Align::Center, L"%dx", r.pitstops);
      x += kTagW;

      wchar_t buf[24];
      if (best_) {
        FormatTime(buf, 24, r.bestMs, timeDecimals_);
        p.Text(x, y, kLapW, kRowH, buf, Font::Text, r.bestMs ? timeColor(r.bestTint) : Col::Dim, Align::Right);
        x += kLapW;
      }
      if (last_) {
        FormatTime(buf, 24, r.lastMs, timeDecimals_);
        p.Text(x, y, kLapW, kRowH, buf, Font::Text, r.lastMs ? timeColor(r.lastTint) : Col::Dim, Align::Right);
        x += kLapW;
      }
      if (fuel_) { DrawCarFuel(p, x, y, kCarFuelW, kRowH, r.car, r.pit ? 0.5f : 1.f); x += kCarFuelW; }
      if (tyres_) { DrawCarTyres(p, x, y, kCarTyreW, kRowH, r.car, r.pit ? 0.5f : 1.f); x += kCarTyreW; }
      if (r.classPlace == 1 && !toAhead_) p.Text(x, y, kGapW - 4, kRowH, L"LEADER", Font::Small, Col::Dim, Align::Right);
      else if (r.classPlace == 1) p.Text(x, y, kGapW - 4, kRowH, L"—", Font::Small, Col::Dim, Align::Right);
      else if (r.gapMs < 0) p.Text(x, y, kGapW - 4, kRowH, L"—", Font::Small, Col::Dim, Align::Right);
      else if (r.lapsDown > 0) p.Textf(x, y, kGapW - 4, kRowH, Font::Text, text, Align::Right, L"+%dL", r.lapsDown);
      else {
        FormatGap(buf, 24, r.gapMs, gapDecimals_, L"+");
        p.Text(x, y, kGapW - 4, kRowH, buf, Font::Text, text, Align::Right);
      }
    }
  }

private:
  static bool PreStart(const Session& s) { return s.race && !s.started; }
  static float SessionHeight(const Session& s) {
    return kLine1H + kLine2H + (PreStart(s) ? 4 + kCardH + 4 + kNoteH : 0.f) + 6;
  }

  static void Clock(wchar_t* buf, size_t n, int secs) {
    if (secs >= 3600) swprintf_s(buf, n, L"%d:%02d:%02d", secs / 3600, (secs / 60) % 60, secs % 60);
    else swprintf_s(buf, n, L"%d:%02d", secs / 60, secs % 60);
  }

  void DrawSession(Painter& p, float w, float y, const Session& s) const {
    const float x = kPad + 4, iw = w - 2 * (kPad + 4);
    // Line 1: session · lap · time left.
    const wchar_t* name = s.kind == 0 ? L"TEST DAY" : s.kind <= 4 ? L"PRACTICE" : s.kind <= 8 ? L"QUALIFYING"
                        : s.kind == 9 ? L"WARM-UP" : L"RACE";
    p.Text(x, y, 140, kLine1H, name, Font::TextBold, Col::Text);
    wchar_t buf[64], clock[24];
    if (s.race && s.started) {
      if (!s.timed) swprintf_s(buf, L"Lap %d / %d", s.lap, s.maxLaps);
      else if (s.totalLaps > 0) swprintf_s(buf, L"Lap %d / ≈ %d", s.lap, s.totalLaps);
      else swprintf_s(buf, L"Lap %d", s.lap);
      p.Text(x, y, iw, kLine1H, buf, Font::Text, Col::Text, Align::Center);
    } else if (!s.race) {
      swprintf_s(buf, L"Lap %d", s.lap);
      p.Text(x, y, iw, kLine1H, buf, Font::Text, Col::Dim, Align::Center);
    }
    if (s.timeLeft >= 0) {
      Clock(clock, 24, s.timeLeft);
      if (PreStart(s)) {
        const wchar_t* phase = s.phase == 3 ? L"formation lap" : s.phase == 4 ? L"starting" : L"before the start";
        p.Text(x, y, iw, kLine1H, phase, Font::TextBold, Col::Warn, Align::Right);
      } else {
        const float lw = p.Measure(L" left", Font::Small);
        p.Text(x, y, iw - lw, kLine1H, clock, Font::TextBold, Col::Text, Align::Right);
        p.Text(x, y + 1, iw, kLine1H, L" left", Font::Small, Col::Dim, Align::Right);
      }
    }
    y += kLine1H;

    // Line 2: what matters for this session (left) + conditions (right).
    wchar_t left[96] = L"";
    D2D1_COLOR_F leftCol = Col::Dim;
    wchar_t len[16] = L"";
    if (s.lengthMin > 0) {
      if (s.lengthMin % 60 == 0) swprintf_s(len, L"%d h", s.lengthMin / 60);
      else swprintf_s(len, L"%d min", s.lengthMin);
    }
    const wchar_t* sep = len[0] ? L" · " : L"";
    if (s.race) {
      if (s.stop == 2) {
        swprintf_s(left, L"%ls%ls%ls", len, sep, s.stops > 1 ? L"pit stops required" : L"pit stop required");
        leftCol = Col::Warn;
      } else if (s.stop == 1) {
        swprintf_s(left, L"%ls%lsno pit stop needed", len, sep);
      } else {
        swprintf_s(left, L"%ls%lsfuel use: after your first lap", len, sep);
      }
    } else if (s.lapsLeft >= 0) {
      if (s.fuelLaps >= 0) swprintf_s(left, L"time for %d laps · fuel for %d", s.lapsLeft, s.fuelLaps / 10);
      else swprintf_s(left, L"time for %d laps", s.lapsLeft);
    }
    p.Text(x, y, iw, kLine2H, left, Font::Small, leftCol);
    if (conditions_) {
      if (s.rainPct > 0) swprintf_s(buf, L"track %d° · air %d° · rain %d%%", s.trackC, s.airC, s.rainPct);
      else swprintf_s(buf, L"track %d° · air %d° · %ls", s.trackC, s.airC, s.wet ? L"wet" : L"dry");
      p.Text(x, y, iw, kLine2H, buf, Font::Small, Col::Dim, Align::Right);
    }
    y += kLine2H;
    if (!PreStart(s)) return;

    // Before the start: race length and what to fill up with.
    y += 4;
    const float cw = (iw - 6) / 2;
    p.FillRounded(x, y, cw, kCardH, 4, Col::Row);
    p.Text(x + 8, y + 2, cw - 16, 14, L"RACE LENGTH", Font::Small, Col::Dim);
    if (!s.timed) swprintf_s(buf, L"%d laps", s.maxLaps);
    else if (s.totalLaps > 0) swprintf_s(buf, L"≈ %d laps", s.totalLaps);
    else wcscpy_s(buf, L"—");
    p.Text(x + 8, y + 14, cw - 16, 22, buf, Font::Mid, Col::Text);
    if (s.timed && s.totalLaps > 0 && s.paceMs > 0) {
      FormatTime(clock, 24, s.paceMs, 3);
      static const wchar_t* const kSrc[] = {L"", L"race", L"session best", L"your best here", L"LMU estimate"};
      swprintf_s(buf, L"at %ls (%ls)", clock, kSrc[std::min<int>(s.paceSource, 4)]);
      p.Text(x + 8, y + 34, cw - 16, 14, buf, Font::Small, Col::Dim);
    }
    const float x2 = x + cw + 6;
    p.FillRounded(x2, y, cw, kCardH, 4, Col::Row);
    p.Text(x2 + 8, y + 2, cw - 16, 14, L"FILL UP WITH", Font::Small, Col::Dim);
    if (s.fillFuelDl > 0 || s.fillEnergyPm > 0) {
      if (s.stop == 2) wcscpy_s(buf, L"full");
      else if (s.fillEnergyPm > 0) swprintf_s(buf, L"%.0f L · %.0f %%", s.fillFuelDl / 10.0, s.fillEnergyPm / 10.0);
      else swprintf_s(buf, L"%.1f L", s.fillFuelDl / 10.0);
      p.Text(x2 + 8, y + 14, cw - 16, 22, buf, Font::Mid, Col::Text);
      if (s.stop == 2) swprintf_s(buf, L"a full tank lasts %.1f laps", s.onFullDl / 10.0);
      else if (s.energyPerLapCpm > 0) swprintf_s(buf, L"race + 1 lap · %.1f %% / lap", s.energyPerLapCpm / 100.0);
      else swprintf_s(buf, L"race + 1 lap · %.2f L / lap", s.fuelPerLapCl / 100.0);
      p.Text(x2 + 8, y + 34, cw - 16, 14, buf, Font::Small, Col::Dim);
    } else {
      p.Text(x2 + 8, y + 14, cw - 16, 22, L"—", Font::Mid, Col::Dim);
      p.Text(x2 + 8, y + 34, cw - 16, 14, L"drive a lap here first", Font::Small, Col::Dim);
    }
    y += kCardH + 4;
    if (s.stop == 1) {
      p.Text(x, y, iw, kNoteH, L"✓ a full tank covers the race: no pit stop needed", Font::Small, Col::Good);
    } else if (s.stop == 2) {
      swprintf_s(buf, L"! pit stop required: %d stop%ls", s.stops, s.stops > 1 ? L"s" : L"");
      p.Text(x, y, iw, kNoteH, buf, Font::Small, Col::Warn);
    }
  }

  void BuildSession(const Model& m, Session& se) const {
    const Snapshot& s = *m.snap;
    const ScoringInfoV01& sc = s.scoring;
    const RaceInfo& r = m.race;
    se.kind = sc.mSession;
    se.race = r.race;
    se.started = r.started;
    se.timed = r.timed;
    se.phase = sc.mGamePhase;
    if (r.timeLeft >= 0) se.timeLeft = static_cast<int>(r.timeLeft);
    if (r.sessionLength > 0) se.lengthMin = static_cast<int>((r.sessionLength + 30.0) / 60.0);
    else if (sc.mEndET > 0 && sc.mEndET < 200000) se.lengthMin = static_cast<int>(sc.mEndET / 60.0);
    se.lap = r.lap;
    if (!r.timed) se.maxLaps = sc.mMaxLaps;
    if (r.totalLaps > 0) se.totalLaps = static_cast<int>(std::lround(r.totalLaps));
    if (!r.race && r.lapsToGo >= 0) se.lapsLeft = static_cast<int>(std::floor(r.lapsToGo));
    if (r.lapsInTank >= 0) se.fuelLaps = Quantize(r.lapsInTank, 0.1);
    if (r.stopKnown) se.stop = r.stopRequired ? 2 : 1;
    se.stops = r.stopsLeft;
    se.fillFuelDl = Quantize(r.fillFuel, 0.1);
    se.fillEnergyPm = Quantize(r.fillEnergy * 100.0, 0.1);
    if (r.pace > 0) se.paceMs = static_cast<int>(std::lround(r.pace * 1000.0));
    se.paceSource = static_cast<uint8_t>(r.paceSource);
    se.fuelPerLapCl = Quantize(r.fuelPerLap, 0.01);
    se.energyPerLapCpm = Quantize(r.energyPerLap * 100.0, 0.01);
    se.onFullDl = r.lapsOnFull > 0 ? Quantize(r.lapsOnFull, 0.1) : 0;
    se.trackC = static_cast<int>(std::lround(sc.mTrackTemp));
    se.airC = static_cast<int>(std::lround(sc.mAmbientTemp));
    se.rainPct = static_cast<int>(std::lround(std::clamp(sc.mRaining, 0.0, 1.0) * 100.0));
    se.wet = sc.mAvgPathWetness > 0.1;
  }

  void Build(const Model& m, View& v) const {
    const Snapshot& s = *m.snap;
    const char* myClass = m.player->mVehicleClass;

    int idx[kMaxVehicles];
    int n = 0;
    for (int i = 0; i < s.numVehicles; ++i)
      if (strncmp(s.vehicles[i].mVehicleClass, myClass, sizeof(m.player->mVehicleClass)) == 0) idx[n++] = i;
    std::sort(idx, idx + n, [&](int a, int b) { return s.vehicles[a].mPlace < s.vehicles[b].mPlace; });
    if (n == 0) return;

    int me = 0;
    for (int k = 0; k < n; ++k) if (idx[k] == m.playerIdx) me = k;

    // Rows: all if they fit, otherwise the top N + a window centred on the player.
    int pick[kMaxRows];
    int count = 0;
    if (n <= maxRows_) {
      for (int k = 0; k < n; ++k) pick[count++] = k;
    } else {
      const int top = std::min(topRows_, maxRows_ - 1), window = maxRows_ - top;
      int start = std::clamp(me - window / 2, top, n - window);
      for (int k = 0; k < top; ++k) pick[count++] = k;
      for (int k = start; k < start + window; ++k) pick[count++] = k;
    }

    v.valid = true;
    if (session_) BuildSession(m, v.ses);
    CopyName(v.cls, m.player->mVehicleClass);
    v.classCount = n;

    const VehicleScoringInfoV01& leader = s.vehicles[idx[0]];
    const bool race = s.scoring.mSession >= 10;
    for (int c = 0; c < count; ++c) {
      const VehicleScoringInfoV01& o = s.vehicles[idx[pick[c]]];
      Row& r = v.rows[v.count++];
      r.classPlace = pick[c] + 1;
      CarNumber(r.number, o);
      r.brand = BrandFor(s, o);
      CopyName(r.name, o.mDriverName);
      if (o.mLastLapTime > 0) r.lastMs = static_cast<int>(std::lround(o.mLastLapTime * 1000.0));
      if (o.mBestLapTime > 0) r.bestMs = static_cast<int>(std::lround(o.mBestLapTime * 1000.0));
      r.lastTint = TintFor(m.timing, o, 3, o.mLastLapTime);
      r.bestTint = TintFor(m.timing, o, 3, o.mBestLapTime) == kTintClass ? kTintClass : kTintNormal;
      // Reference car: class leader, or the car one place ahead in class.
      const VehicleScoringInfoV01& ref = toAhead_ && pick[c] > 0 ? s.vehicles[idx[pick[c] - 1]] : leader;
      if (race) {
        r.lapsDown = o.mLapsBehindLeader - ref.mLapsBehindLeader;
        r.gapMs = Quantize(std::fmax(0.0, o.mTimeBehindLeader - ref.mTimeBehindLeader), 0.001);
      } else if (o.mBestLapTime > 0 && ref.mBestLapTime > 0) {
        // Practice / qualifying: positions are by best lap, so is the gap.
        r.gapMs = Quantize(std::fmax(0.0, o.mBestLapTime - ref.mBestLapTime), 0.001);
      } else {
        r.gapMs = -1;
      }
      r.pitstops = o.mNumPitstops;
      if (fuel_ || tyres_) r.car = CarStateFor(s, o);
      r.pit = o.mInPits;
      r.player = idx[pick[c]] == m.playerIdx;
      r.gapBefore = c > 0 && pick[c] != pick[c - 1] + 1;
    }
  }

  int maxRows_, topRows_;
  bool session_, conditions_, number_, brand_, best_, last_, fuel_, tyres_, showStops_, toAhead_;
  int gapDecimals_, timeDecimals_;
  D2D1_COLOR_F playerRow_, text_, classBest_, personalBest_;
  View view_;
};

} // namespace

const WidgetType kStandingsWidget{
  "standings", "Standings", "Session header and your class: the leaders plus the cars around you.",
  true, 16, 16, 250, kOptions, CreateWidget<StandingsWidget>};
