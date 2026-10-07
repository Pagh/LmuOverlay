// Sector times. Two layouts:
//  - compact (default): three boxes with the gap of each sector of this lap to a reference
//    (by default the same reference as the Delta widget), plus last / best / ideal lap;
//  - table: this lap, last lap, session best, all-time best and lobby best, row by row.
// Purple = fastest in your class, green = your best this session, yellow = slower.
#include "widgets/Widget.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

constexpr float kPad = 8.f;
// Table layout
constexpr float kLabelW = 74.f, kSecW = 72.f, kLapW = 90.f, kRowH = 21.f;
constexpr float kTableW = kPad * 2 + kLabelW + kSecW * 3 + kLapW;
// Compact layout
constexpr float kCompactW = 336.f, kBoxGap = 6.f, kBoxH = 52.f;

enum Tint : uint8_t { kText, kGreen, kYellow, kPurple, kRed, kDim };
enum Kind : uint8_t { kNone, kTime, kDelta, kRunning };
enum RowId { kNow, kLast, kSession, kAllTime, kLobby, kRows };

struct Cell {
  int ms = 0;
  Kind kind = kNone;
  Tint tint = kText;
  bool fromLastLap = false;  // compact: not driven yet this lap, showing last lap's result
  bool operator==(const Cell&) const = default;
};

struct View {
  bool valid = false;
  int lap = 0;
  int current = 0;           // sector in progress (0..2)
  int refKind = 0;           // 0 session, 1 all-time, 2 lobby
  Cell cells[kRows][4];      // S1 S2 S3 LAP
  Cell compact[3];
  int refMs[3]{};            // compact: reference sector times
  int idealSession = 0, idealAllTime = 0;
  bool outLap = false;       // S1 of an out lap isn't compared
  bool operator==(const View&) const = default;
};

const OptionDef kOptions[] = {
  OptChoice("layout", "Layout", "compact|table", 0,
            "Compact: this lap's sectors against one reference.\nTable: every reference at once."),
  OptChoice("compare_to", "Compare this lap to", "same as Delta|session best|all-time best|lobby best", 0,
            "'Same as Delta' follows the Delta widget's reference (Ctrl+Alt+D)."),
  OptBool("show_last", "Table row: last lap", true),
  OptBool("show_session", "Table row: session best", true),
  OptBool("show_alltime", "Table row: all-time best", true,
          "Your best ever on this track with this car (valid laps, saved between sessions)"),
  OptBool("show_lobby", "Table row: lobby best", true, "Fastest sectors of anyone in your class this session"),
  OptBool("show_ideal", "Show ideal lap", true, "Your all-time best sectors added up (saved between sessions)"),
  OptInt("decimals", "Decimals", 3, 1, 3),
  OptColor("purple", "Class best", 0xB87CFFFF),
  OptColor("green", "Personal best", 0x3DDC84FF),
  OptColor("yellow", "Slower", 0xFFC23DFF),
};

int Ms(double s) { return s > 0 ? static_cast<int>(std::lround(s * 1000.0)) : 0; }   // a time (0 = none)
int DeltaMs(double s) { return static_cast<int>(std::lround(s * 1000.0)); }          // a gap: keeps its sign

class SectorsWidget final : public Widget {
public:
  SectorsWidget(const WidgetType& t, const OptionSet& o)
      : Widget(t, o),
        table_(o.Choice("layout") == 1),
        compareTo_(o.Choice("compare_to")),
        ideal_(o.Bool("show_ideal")),
        decimals_(o.Int("decimals")) {
    show_[kNow] = true;
    show_[kLast] = o.Bool("show_last");
    show_[kSession] = o.Bool("show_session");
    show_[kAllTime] = o.Bool("show_alltime");
    show_[kLobby] = o.Bool("show_lobby");
    colors_[kText] = Col::Text;
    colors_[kGreen] = o.Color("green");
    colors_[kYellow] = o.Color("yellow");
    colors_[kPurple] = o.Color("purple");
    colors_[kRed] = Col::Bad;
    colors_[kDim] = Col::Dim;
  }

  float Width() const override { return table_ ? kTableW : kCompactW; }
  float Height() const override {
    if (!table_) return kPad + 18.f + kBoxH + 6.f + 20.f + kPad;
    int rows = 0;
    for (bool b : show_) rows += b;
    return kPad * 2 + kRowH * (rows + 1) + (ideal_ ? 18.f : 0.f);
  }

  bool Prepare(const Model& m) override {
    View v;
    if (m.onTrack && m.player && m.telem && m.timing) Build(m, v);
    if (v == view_) return false;
    view_ = v;
    return true;
  }

  void Draw(Painter& p) override {
    p.Panel(Width(), Height());
    if (!view_.valid) { p.Text(0, 0, Width(), Height(), L"SECTORS", Font::TextBold, Col::Dim, Align::Center); return; }
    if (table_) DrawTable(p);
    else DrawCompact(p);
  }

private:
  // ---- formatting ---------------------------------------------------------------------------

  void FormatSector(wchar_t* buf, size_t n, int ms) const {
    const int div = decimals_ >= 3 ? 1 : decimals_ == 2 ? 10 : 100;
    if (ms >= 60000) swprintf_s(buf, n, L"%d:%02d.%0*d", ms / 60000, (ms / 1000) % 60, decimals_, (ms % 1000) / div);
    else swprintf_s(buf, n, L"%d.%0*d", ms / 1000, decimals_, (ms % 1000) / div);
  }

  void FormatCell(wchar_t* buf, size_t n, const Cell& c, bool lapColumn) const {
    if (c.kind == kDelta) {
      wchar_t t[24];
      FormatSector(t, 24, std::abs(c.ms));
      swprintf_s(buf, n, L"%lc%ls", c.ms <= 0 ? L'-' : L'+', t);
    } else if (c.kind == kRunning) {
      if (lapColumn || c.ms >= 60000) swprintf_s(buf, n, L"%d:%04.1f", c.ms / 60000, (c.ms % 60000) / 1000.0);
      else swprintf_s(buf, n, L"%.1f", c.ms / 1000.0);
    } else if (lapColumn) {
      FormatLapTime(buf, n, c.ms / 1000.0);
    } else {
      FormatSector(buf, n, c.ms);
    }
  }

  // ---- compact ------------------------------------------------------------------------------

  void DrawCompact(Painter& p) const {
    const float w = kCompactW;
    static const wchar_t* const kRef[] = {L"SESSION BEST", L"ALL-TIME BEST", L"LOBBY BEST"};
    p.Textf(kPad, kPad - 2, 200, 18, Font::Small, Col::Dim, Align::Left, L"SECTORS  vs %ls", kRef[view_.refKind]);
    p.Textf(w - 108, kPad - 2, 100, 18, Font::Small, Col::Dim, Align::Right, L"LAP %d", view_.lap);

    const float y = kPad + 18.f, bw = (w - 2 * kPad - 2 * kBoxGap) / 3.f;
    for (int k = 0; k < 3; ++k) {
      const float x = kPad + k * (bw + kBoxGap);
      const Cell& c = view_.compact[k];
      D2D1_COLOR_F tint = colors_[c.tint];
      // Tinted box for a finished sector, outlined box for the one you're driving.
      if (c.kind == kDelta || c.kind == kTime) {
        D2D1_COLOR_F bg = tint;
        bg.a = c.fromLastLap ? 0.10f : 0.22f;
        p.FillRounded(x, y, bw, kBoxH, 5, bg);
      } else {
        p.FillRounded(x, y, bw, kBoxH, 5, Col::Rgb(0x222731, 0.9f));
      }
      if (k == view_.current) p.Outline(x, y, bw, kBoxH, 5, Col::Text, 1.5f);
      if (k == 0 && view_.outLap) {
        p.Textf(x + 8, y + 3, 30, 16, Font::Small, Col::Dim, Align::Left, L"S1");
        p.Text(x, y + 18, bw, kBoxH - 20, L"OUT LAP", Font::TextBold, Col::Dim, Align::Center);
        continue;
      }

      p.Textf(x + 8, y + 3, 30, 16, Font::Small, Col::Dim, Align::Left, L"S%d", k + 1);
      if (view_.refMs[k] > 0) {
        wchar_t ref[24];
        FormatSector(ref, 24, view_.refMs[k]);
        p.Text(x + 30, y + 3, bw - 38, 16, ref, Font::Small, Col::Dim, Align::Right);
      }
      if (c.kind == kNone) continue;
      wchar_t buf[32];
      FormatCell(buf, 32, c, false);
      if (c.kind == kRunning) tint = Col::Text;
      if (c.fromLastLap) tint.a = 0.55f;
      p.Text(x, y + 18, bw, kBoxH - 20, buf, Font::Big, tint, Align::Center);
    }

    // One line of lap times.
    const float ly = y + kBoxH + 6.f, cw = (w - 2 * kPad) / 3.f;
    auto lapItem = [&](float x, const wchar_t* label, const Cell& c) {
      p.Text(x, ly, 40, 20, label, Font::Small, Col::Dim);
      wchar_t buf[24] = L"—";
      if (c.kind != kNone) FormatCell(buf, 24, c, true);
      p.Text(x + 38, ly, cw - 42, 20, buf, Font::TextBold, c.kind != kNone ? colors_[c.tint] : Col::Dim);
    };
    lapItem(kPad, L"LAST", view_.cells[kLast][3]);
    lapItem(kPad + cw, L"BEST", view_.cells[kSession][3]);
    // Ideal: your all-time best sectors added up (your theoretical best lap here with this car).
    if (ideal_) lapItem(kPad + 2 * cw, L"IDEAL", Cell{view_.idealAllTime, view_.idealAllTime ? kTime : kNone, kPurple});
  }

  // ---- table --------------------------------------------------------------------------------

  void DrawTable(Painter& p) const {
    float y = kPad;
    p.Textf(kPad, y, kLabelW, kRowH, Font::Small, Col::Dim, Align::Left, L"LAP %d", view_.lap);
    const wchar_t* heads[] = {L"S1", L"S2", L"S3", L"LAP"};
    for (int c = 0; c < 4; ++c) {
      const float x = kPad + kLabelW + c * kSecW;
      p.Text(x, y, c < 3 ? kSecW : kLapW, kRowH, heads[c], Font::Small, c == view_.current ? Col::Text : Col::Dim, Align::Right);
    }
    y += kRowH;

    const wchar_t* labels[kRows] = {L"Now", L"Last", L"Best", L"All-time", L"Lobby"};
    for (int r = 0; r < kRows; ++r) {
      if (!show_[r]) continue;
      if (r == kNow) p.FillRounded(3, y, kTableW - 6, kRowH, 3, Col::Rgb(0x2A303B, 0.9f));
      p.Text(kPad, y, kLabelW, kRowH, labels[r], Font::Small, Col::Dim);
      for (int c = 0; c < 4; ++c) {
        const float x = kPad + kLabelW + c * kSecW, cw = c < 3 ? kSecW : kLapW;
        const Cell& cell = view_.cells[r][c];
        if (cell.kind == kNone) { p.Text(x, y, cw, kRowH, L"—", Font::Text, Col::Dim, Align::Right); continue; }
        wchar_t buf[32];
        FormatCell(buf, 32, cell, c == 3);
        p.Text(x, y, cw, kRowH, buf, cell.tint == kText ? Font::Text : Font::TextBold, colors_[cell.tint], Align::Right);
      }
      y += kRowH;
    }
    if (ideal_) {
      wchar_t a[24], b[24];
      FormatLapTime(a, 24, view_.idealSession / 1000.0);
      FormatLapTime(b, 24, view_.idealAllTime / 1000.0);
      p.Textf(kPad, y + 1, kTableW - 2 * kPad, 16, Font::Small, Col::Dim, Align::Right, L"Ideal  session %ls   ·   all-time %ls", a, b);
    }
  }

  // ---- data ---------------------------------------------------------------------------------

  // References are taken when a lap starts and kept for that lap (and for showing it as "last lap"
  // afterwards): otherwise a sector you just set would already be the reference it's compared to.
  struct Refs { SectorSet session, allTime, lobby; };

  void FreezeRefs(const Model& m) {
    const Timing& tm = *m.timing;
    const VehicleScoringInfoV01& me = *m.player;
    const Timing::ClassBest* cls = tm.Class(me.mVehicleClass);
    Refs now{tm.PlayerSession(), tm.PlayerAllTime(), cls ? cls->best : SectorSet{}};
    if (me.mTotalLaps != lapKey_) {
      prev_ = haveRefs_ ? cur_ : now;
      cur_ = now;
      lapKey_ = me.mTotalLaps;
      haveRefs_ = true;
    } else if (me.mInPits || !haveRefs_) {
      cur_ = prev_ = now;
      haveRefs_ = true;
    }
  }

  void Build(const Model& m, View& v) {
    const Timing& tm = *m.timing;
    const VehicleScoringInfoV01& me = *m.player;
    const TelemInfoV01& t = *m.telem;
    FreezeRefs(m);
    const SectorSet& lobby = cur_.lobby;
    const SectorSet& session = cur_.session;
    const SectorSet& allTime = cur_.allTime;

    v.valid = true;
    v.lap = t.mLapNumber;
    v.current = me.mSector == 1 ? 0 : me.mSector == 2 ? 1 : 2; // LMU: 1=S1, 2=S2, 0=S3

    // Reference: follows the Delta widget unless set explicitly.
    if (compareTo_ == 0) v.refKind = m.deltaRef == DeltaRef::AllTime ? 1 : m.deltaRef == DeltaRef::Lobby ? 2 : 0;
    else v.refKind = compareTo_ - 1;
    const SectorSet& ref = v.refKind == 1 ? allTime : v.refKind == 2 ? lobby : session;
    for (int k = 0; k < 3; ++k) v.refMs[k] = Ms(ref.s[k]);

    auto tintVs = [&](const Refs& r, int k, double time) -> Tint { // k = 0..2 sector, 3 = lap
      const double lb = k < 3 ? r.lobby.s[k] : r.lobby.lap, pb = k < 3 ? r.session.s[k] : r.session.lap;
      if (lb > 0 && time <= lb + 1e-4) return kPurple;
      if (pb > 0 && time <= pb + 1e-4) return kGreen;
      return pb > 0 ? kYellow : kText;
    };
    auto tint = [&](int k, double time) { return tintVs(cur_, k, time); };
    auto timeRow = [&](Cell* row, const SectorSet& s, Tint fixed, bool colour) {
      for (int k = 0; k < 4; ++k) {
        const double time = k < 3 ? s.s[k] : s.lap;
        if (time > 0) row[k] = {Ms(time), kTime, colour ? tint(k, time) : fixed};
      }
    };

    // This lap: completed sectors as gaps to the reference, the running sector as elapsed time.
    double done[3] = {0, 0, 0};
    if (v.current >= 1 && me.mCurSector1 > 0) done[0] = me.mCurSector1;
    if (v.current >= 2 && me.mCurSector2 > me.mCurSector1 && me.mCurSector1 > 0) done[1] = me.mCurSector2 - me.mCurSector1;
    const bool lapRunning = t.mLapStartET > 0 && !(me.mInPits && me.mTotalLaps == 0);
    const double lapTime = lapRunning ? t.mElapsedTime - t.mLapStartET : 0;
    auto gapCell = [&](int k, double time) {
      return ref.s[k] > 0 ? Cell{DeltaMs(time - ref.s[k]), kDelta, tint(k, time)} : Cell{Ms(time), kTime, tint(k, time)};
    };
    // Last lap's results against the references it was driven against.
    const SectorSet& prevRef = v.refKind == 1 ? prev_.allTime : v.refKind == 2 ? prev_.lobby : prev_.session;
    auto lastCell = [&](int k, double time) {
      const Tint tt = tintVs(prev_, k, time);
      return prevRef.s[k] > 0 ? Cell{DeltaMs(time - prevRef.s[k]), kDelta, tt} : Cell{Ms(time), kTime, tt};
    };
    const SectorSet& last = tm.PlayerLast();
    v.outLap = tm.OutLap();
    if (v.outLap) done[0] = 0; // S1 of an out lap includes the pit exit
    for (int k = 0; k < 3; ++k) {
      if (v.outLap && k == 0 && v.current > 0) continue;
      if (done[k] > 0) {
        v.cells[kNow][k] = v.compact[k] = gapCell(k, done[k]);
      } else if (k == v.current && lapRunning) {
        const double running = lapTime - done[0] - done[1];
        if (running > 0) v.cells[kNow][k] = v.compact[k] = {Quantize(running, 0.1) * 100, kRunning, kText};
      } else if (last.s[k] > 0) {
        v.compact[k] = lastCell(k, last.s[k]); // not driven yet this lap: last lap's result, dimmed
        v.compact[k].fromLastLap = true;
      }
    }
    if (lapRunning && lapTime > 0)
      v.cells[kNow][3] = {Quantize(lapTime, 0.1) * 100, kRunning, tm.CurrentLapInvalid() ? kRed : kText};

    timeRow(v.cells[kLast], last, kText, true);
    if (!tm.PlayerLastValid() && v.cells[kLast][3].kind != kNone) v.cells[kLast][3].tint = kRed;
    timeRow(v.cells[kSession], session, kText, true);
    timeRow(v.cells[kAllTime], allTime, kText, false);
    timeRow(v.cells[kLobby], lobby, kPurple, false);

    // Ideal laps from your best sectors right now (not the per-lap frozen references).
    v.idealSession = Ms(tm.PlayerSession().Ideal());
    v.idealAllTime = Ms(tm.PlayerAllTime().Ideal());
  }

  bool table_;
  int compareTo_;
  bool show_[kRows];
  bool ideal_;
  int decimals_;
  D2D1_COLOR_F colors_[6];
  View view_;
  Refs cur_, prev_;
  long lapKey_ = -100;
  bool haveRefs_ = false;
};

} // namespace

const WidgetType kSectorsWidget{
  "sectors", "Sectors", "This lap's sectors against a reference (compact), or every reference in a table.",
  true, 1568, 16, 100, kOptions, CreateWidget<SectorsWidget>};
