// Track info: a small bar under the delta that only speaks when something matters.
// Danger ahead (stopped / slow car with the distance in metres, local yellow, full-course
// yellow), blue flag, session phase, track-limit points, penalties and lap validity.
#include "widgets/Widget.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr float kW = 420.f, kH = 26.f;

enum class Danger : uint8_t { None, Slow, Stopped, Yellow, YellowAhead, FullCourse, Blue, Red, Phase, Notice };

struct View {
  bool valid = false;
  Danger danger = Danger::None;
  int distM = -1;             // metres to the danger, quantised
  int fcy = 0;                // LMU yellow flag state (full course)
  int phase = 0;              // game phase for pre-race / finished text
  int tlSteps = 0, tlPerPoint = 0, tlPerPenalty = 0;
  int penalties = 0;
  int lapState = 0;           // 0 hidden, 1 valid, 2 invalid
  wchar_t notice[48]{};       // "PROFILE  Race" after a button press
  bool operator==(const View&) const = default;
};

const OptionDef kOptions[] = {
  OptInt("range_m", "Warn about dangers ahead within (m)", 250, 50, 2000, "Nothing behind you is ever reported"),
  OptInt("slow_kmh", "Slow car below (km/h)", 60, 10, 150, "Cars on track slower than this are reported as a danger"),
  OptBool("show_track_limits", "Track-limit points", true, "Shown once you have at least one point"),
  OptBool("always_show_limits", "Track limits even at 0", false),
  OptBool("show_penalties", "Penalties", true),
  OptBool("show_lap_valid", "Lap valid / invalid (practice, qualifying)", true),
  OptColor("yellow", "Yellow / danger", 0xFFC23DFF),
  OptColor("blue", "Blue flag", 0x4DA3FFFF),
  OptColor("red", "Red flag / invalid", 0xFF5A5AFF),
  OptColor("ok", "Valid lap", 0x3DDC84FF),
};

class TrackInfoWidget final : public Widget {
public:
  TrackInfoWidget(const WidgetType& t, const OptionSet& o)
      : Widget(t, o),
        range_(o.Int("range_m")),
        slow_(o.Int("slow_kmh") / 3.6),
        limits_(o.Bool("show_track_limits")),
        limitsAlways_(o.Bool("always_show_limits")),
        penalties_(o.Bool("show_penalties")),
        lapValid_(o.Bool("show_lap_valid")),
        yellow_(o.Color("yellow")),
        blue_(o.Color("blue")),
        red_(o.Color("red")),
        ok_(o.Color("ok")) {}

  float Width() const override { return kW; }
  float Height() const override { return kH; }

  bool Prepare(const Model& m) override {
    View v;
    if (m.onTrack && m.player) Build(m, v);
    if (v == view_) return false;
    view_ = v;
    return true;
  }

  void Draw(Painter& p) override {
    const View& v = view_;
    if (!v.valid) { if (p.EditMode()) DrawPlaceholder(p, kW, kH, L"TRACK INFO"); return; }

    // Segments, measured and centred: [coloured danger block][detail][TL][PEN][LAP].
    wchar_t head[48] = L"", detail[64] = L"";
    D2D1_COLOR_F headCol = yellow_;
    bool filled = true; // danger block drawn solid
    switch (v.danger) {
      case Danger::Stopped:
      case Danger::Slow:
        swprintf_s(head, L"%ls  %d m", v.danger == Danger::Stopped ? L"STOPPED CAR" : L"SLOW CAR", v.distM);
        break;
      case Danger::Yellow: swprintf_s(head, L"YELLOW  %d m", v.distM); wcscpy_s(detail, L"slow car ahead"); break;
      case Danger::YellowAhead: swprintf_s(head, L"YELLOW  in %d m", v.distM); break;
      case Danger::FullCourse:
        wcscpy_s(head, L"FULL COURSE YELLOW");
        wcscpy_s(detail, v.fcy == 2 ? L"pits closed" : v.fcy == 3 ? L"pit: lead lap" : v.fcy == 4 ? L"pits open"
                         : v.fcy == 5 ? L"last lap" : v.fcy == 6 ? L"resume" : L"");
        break;
      case Danger::Blue: wcscpy_s(head, L"BLUE FLAG"); headCol = blue_; wcscpy_s(detail, L"let the faster car by"); break;
      case Danger::Red: wcscpy_s(head, L"SESSION STOPPED"); headCol = red_; break;
      case Danger::Phase:
        filled = false;
        headCol = Col::Text;
        wcscpy_s(head, v.phase == 1 ? L"RECON LAP" : v.phase == 2 ? L"GRID" : v.phase == 3 ? L"FORMATION LAP"
                       : v.phase == 4 ? L"STARTING" : v.phase == 8 ? L"CHEQUERED FLAG" : L"");
        break;
      case Danger::Notice: wcscpy_s(head, v.notice); headCol = blue_; break;
      case Danger::None: break;
    }
    const bool showTl = limits_ && v.tlPerPoint > 0 && (v.tlSteps > 0 || limitsAlways_);
    const bool showPen = penalties_ && v.penalties > 0;
    wchar_t tl[32] = L"", pen[16] = L"";
    if (showTl) {
      const double pts = static_cast<double>(v.tlSteps) / v.tlPerPoint, max = static_cast<double>(v.tlPerPenalty) / v.tlPerPoint;
      swprintf_s(tl, L"%g/%g", pts, max);
    }
    if (showPen) swprintf_s(pen, L"%d", v.penalties);
    if (!head[0] && !showTl && !showPen && !v.lapState) { if (p.EditMode()) DrawPlaceholder(p, kW, kH, L"TRACK INFO"); return; }

    constexpr float kGap = 10.f, kIn = 10.f;
    const float headW = head[0] ? p.Measure(head, Font::TextBold) + (filled ? 2 * kIn + 16 : 2 * kIn) : 0.f;
    const float detailW = detail[0] ? p.Measure(detail, Font::Small) + kGap : 0.f;
    const float tlW = showTl ? p.Measure(tl, Font::TextBold) + p.Measure(L"TL ", Font::Small) + kGap : 0.f;
    const float penW = showPen ? p.Measure(pen, Font::TextBold) + p.Measure(L"PEN ", Font::Small) + kGap : 0.f;
    const wchar_t* lapTxt = v.lapState == 1 ? L"✓ LAP VALID" : L"✕ LAP INVALID";
    const float lapW = v.lapState ? p.Measure(lapTxt, Font::TextBold) + kGap : 0.f;
    const float rest = detailW + tlW + penW + lapW;
    const float total = std::min(kW, headW + (rest > 0 ? rest + kGap : 0.f));
    const float x0 = (kW - total) / 2.f;

    p.FillRounded(x0, 0, total, kH, 6.f, Background());
    if (p.EditMode()) p.Outline(x0, 0, total, kH, 6.f, Col::EditOutline, 2.f);
    float x = x0;
    if (head[0]) {
      if (filled) {
        D2D1_COLOR_F bg = headCol;
        bg.a = 0.92f;
        p.FillRounded(x, 0, headW, kH, 6.f, bg);
        if (rest > 0) p.Fill(x + headW - 6, 0, 6, kH, bg); // square join with the rest of the bar
        if (v.danger != Danger::Notice) DrawTriangle(p, x + kIn, kH / 2.f);
        p.Text(x + kIn + 16, 0, headW - kIn - 16, kH, head, Font::TextBold, Col::Rgb(0x1A1405), Align::Left);
      } else {
        p.Text(x + kIn, 0, headW - kIn, kH, head, Font::TextBold, headCol, Align::Left);
      }
      x += headW + kGap;
    } else {
      x += kGap;
    }
    if (detail[0]) { p.Text(x, 0, detailW, kH, detail, Font::Small, headCol, Align::Left); x += detailW; }
    if (showTl) {
      const float lw = p.Measure(L"TL ", Font::Small);
      p.Text(x, 0, lw, kH, L"TL ", Font::Small, Col::Dim, Align::Left);
      const bool close = v.tlSteps * 3 >= v.tlPerPenalty * 2; // 2/3 of the way to a penalty
      p.Text(x + lw, 0, tlW - lw, kH, tl, Font::TextBold, close ? red_ : yellow_, Align::Left);
      x += tlW;
    }
    if (showPen) {
      const float lw = p.Measure(L"PEN ", Font::Small);
      p.Text(x, 0, lw, kH, L"PEN ", Font::Small, Col::Dim, Align::Left);
      p.Text(x + lw, 0, penW - lw, kH, pen, Font::TextBold, red_, Align::Left);
      x += penW;
    }
    if (v.lapState) p.Text(x, 0, lapW, kH, lapTxt, Font::TextBold, v.lapState == 1 ? ok_ : red_, Align::Left);
  }

private:
  static void DrawTriangle(Painter& p, float x, float cy) {
    const D2D1_POINT_2F pts[] = {{x + 6, cy - 6}, {x + 12, cy + 5}, {x, cy + 5}, {x + 6, cy - 6}};
    p.Polyline(pts, 4, Col::Rgb(0x1A1405), 2.f);
  }

  void Build(const Model& m, View& v) const {
    const Snapshot& s = *m.snap;
    const ScoringInfoV01& sc = s.scoring;
    const VehicleScoringInfoV01& me = *m.player;
    const bool race = sc.mSession >= 10 && sc.mSession <= 13;
    v.valid = true;
    v.penalties = me.mNumPenalties;
    if (m.telem) {
      v.tlSteps = m.telem->mTrackLimitsSteps;
      v.tlPerPoint = sc.mTrackLimitsStepsPerPoint;
      v.tlPerPenalty = sc.mTrackLimitsStepsPerPenalty;
      if (lapValid_ && !race && !me.mInPits && m.timing)
        v.lapState = (m.timing->CurrentLapInvalid() || m.telem->mLapInvalidated) ? 2 : 1;
    }

    if (m.now - m.noticeAt < 2.5 && m.notice[0]) { // you just switched something
      v.danger = Danger::Notice;
      wcscpy_s(v.notice, m.notice);
      return;
    }
    if (sc.mGamePhase == 7) { v.danger = Danger::Red; return; }
    if (sc.mGamePhase == 6 || (sc.mYellowFlagState >= 1 && sc.mYellowFlagState <= 6)) {
      v.danger = Danger::FullCourse;
      v.fcy = sc.mYellowFlagState;
      return;
    }

    // Dangers ahead only (never behind), within range_ metres.
    const double L = m.trackLength;
    if (L <= 0 || me.mInPits) return;
    const double mySpeed = std::sqrt(me.mLocalVel.x * me.mLocalVel.x + me.mLocalVel.z * me.mLocalVel.z);
    // LMU's local yellows (index i = sector i+1; 1 = yellow, 11 = green).
    const TrackMap* map = m.timing ? &m.timing->Map() : nullptr;
    auto yellowAt = [&](double d) {
      const int sec = map ? map->SectorOf(std::fmod(d + L, L)) : -1;
      return sec >= 0 && sc.mSectorFlag[sec] == 1;
    };
    double best = 1e9, bestSpeed = 0;
    bool bestFlagged = false;
    for (int i = 0; i < s.numVehicles; ++i) {
      const VehicleScoringInfoV01& o = s.vehicles[i];
      if (i == m.playerIdx || o.mInPits || o.mInGarageStall || o.mFinishStatus != 0) continue;
      if (o.mPitState >= 2 && o.mPitState <= 4) continue; // entering / stopped / leaving the pits
      double d = o.mLapDist - me.mLapDist;
      if (d < 0) d += L;
      if (d > range_ || d >= best) continue;
      const double speed = std::sqrt(o.mLocalVel.x * o.mLocalVel.x + o.mLocalVel.y * o.mLocalVel.y + o.mLocalVel.z * o.mLocalVel.z);
      // Slow outright, or (in a yellow sector) much slower than you: that's what the flag is for.
      const bool flagged = yellowAt(o.mLapDist);
      if (speed < slow_ || (flagged && speed < mySpeed * 0.6)) {
        best = d;
        bestSpeed = speed;
        bestFlagged = flagged;
      }
    }
    auto metres = [](double d) { return static_cast<int>(d >= 100 ? std::lround(d / 10.0) * 10 : std::lround(d)); };
    if (best <= range_) {
      v.danger = bestFlagged ? Danger::Yellow : bestSpeed < 3.0 ? Danger::Stopped : Danger::Slow;
      v.distM = metres(best);
      return;
    }
    // A yellow sector starting within range ahead (cause not located).
    if (map && map->SectorOf(me.mLapDist) >= 0) {
      const int next = (map->SectorOf(me.mLapDist) + 1) % 3;
      const double start = next == 0 ? L : map->SectorStart(next);
      const double d = start - me.mLapDist;
      if (sc.mSectorFlag[next] == 1 && d > 0 && d <= range_) {
        v.danger = Danger::YellowAhead;
        v.distM = metres(d);
        return;
      }
    }
    if (me.mFlag == 6) { v.danger = Danger::Blue; return; }
    if (race && (sc.mGamePhase >= 1 && sc.mGamePhase <= 4)) { v.danger = Danger::Phase; v.phase = sc.mGamePhase; return; }
    if (sc.mGamePhase == 8) { v.danger = Danger::Phase; v.phase = 8; }
  }

  int range_;
  double slow_;
  bool limits_, limitsAlways_, penalties_, lapValid_;
  D2D1_COLOR_F yellow_, blue_, red_, ok_;
  View view_;
};

} // namespace

const WidgetType kTrackInfoWidget{
  "trackinfo", "Track info", "Small bar: danger ahead with the distance, flags, track limits, penalties, lap validity.",
  true, 750, 190, 100, kOptions, CreateWidget<TrackInfoWidget>};
