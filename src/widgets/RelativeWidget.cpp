// Relative: the cars physically closest to you on track, ahead (top) and behind (bottom),
// with their last lap, best lap, last-lap sectors, the gap and how it changed over the last lap.
#include "widgets/Widget.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <unordered_map>

namespace {

constexpr int kMaxEachSide = 6;
constexpr float kRowH = 21.f, kPad = 6.f, kHeaderH = 18.f;
// Column widths (unscaled px).
constexpr float kPosW = 30.f, kNumW = 40.f, kBrandW = 42.f, kNameW = 140.f, kTagW = 34.f, kLapW = 70.f, kSecW = 50.f, kGapW = 58.f,
                kTrendW = 64.f;
constexpr int kHistory = 160;   // gap samples per car (one every 2 s = 5+ minutes)

struct Row {
  int place = 0;            // overall position
  int classPlace = 0;
  wchar_t number[8]{};
  Brand brand;
  wchar_t name[33]{};
  char cls[32]{};
  int gapMs = 0;            // |gap| in ms
  int lapDiff = 0;          // >0 = they are laps ahead of you, <0 = behind
  int lastMs = 0, bestMs = 0;
  int secMs[3]{};           // sectors: this lap's as they complete them, else the previous lap's
  bool secOld[3]{};         // from the previous lap (drawn dimmed)
  TimeTint lastTint = kTintNormal, bestTint = kTintNormal, secTint[3]{};
  int trendMs = INT_MIN;    // change of |gap| over your last lap (negative = closer), INT_MIN = unknown
  bool pit = false;
  bool player = false;
  bool operator==(const Row&) const = default;
};

struct View {
  bool valid = false;
  int count = 0;
  Row rows[2 * kMaxEachSide + 1];
  bool operator==(const View&) const = default;
};

const OptionDef kOptions[] = {
  OptInt("cars_each_side", "Cars ahead / behind", 3, 1, kMaxEachSide),
  OptChoice("position", "Position shown", "class|overall", 0),
  OptBool("show_header", "Column headers", true),
  OptBool("show_number", "Column: car number", true),
  OptBool("show_brand", "Column: car make", true),
  OptBool("show_last", "Column: last lap", true),
  OptBool("show_best", "Column: best lap", true),
  OptBool("show_sectors", "Column: sectors (live)", true,
          "Sectors of the lap they're on as they complete them; the others from their previous lap, dimmed"),
  OptInt("time_decimals", "Lap / sector decimals", 3, 1, 3),
  OptInt("gap_decimals", "Gap decimals", 3, 1, 3),
  OptBool("show_trend", "Column: gap change per lap", true,
          "How much the gap to each car changed over your last lap. Green: good for you\n"
          "(catching the car ahead / pulling away from the car behind)."),
  OptBool("show_class_stripe", "Class colour stripe", true),
  OptColor("player_row", "Your row", 0x3A4458FF),
  OptColor("text", "Text (same lap)", 0xF2F4F7FF),
  OptColor("lapping_you", "Car laps ahead of you", 0xFF8A65FF),
  OptColor("lapped_by_you", "Car laps behind you", 0x7FB8FFFF),
  OptColor("class_best", "Fastest in class", 0xB87CFFFF),
  OptColor("personal_best", "Driver's own best", 0x3DDC84FF),
};

class RelativeWidget final : public Widget {
public:
  RelativeWidget(const WidgetType& t, const OptionSet& o)
      : Widget(t, o),
        each_(o.Int("cars_each_side")),
        overall_(o.Choice("position") == 1),
        header_(o.Bool("show_header")),
        number_(o.Bool("show_number")),
        brand_(o.Bool("show_brand")),
        last_(o.Bool("show_last")),
        best_(o.Bool("show_best")),
        sectors_(o.Bool("show_sectors")),
        timeDecimals_(o.Int("time_decimals")),
        gapDecimals_(o.Int("gap_decimals")),
        trend_(o.Bool("show_trend")),
        stripe_(o.Bool("show_class_stripe")),
        playerRow_(o.Color("player_row")),
        text_(o.Color("text")),
        lapping_(o.Color("lapping_you")),
        lapped_(o.Color("lapped_by_you")),
        classBest_(o.Color("class_best")),
        personalBest_(o.Color("personal_best")) {}

  float Width() const override {
    return kPad * 2 + kPosW + (number_ ? kNumW : 0) + (brand_ ? kBrandW : 0) + kNameW + kTagW + (last_ ? kLapW : 0) + (best_ ? kLapW : 0) +
           (sectors_ ? kSecW * 3 : 0) + kGapW + (trend_ ? kTrendW : 0);
  }
  float Height() const override { return kPad * 2 + (header_ ? kHeaderH : 0) + kRowH * (2 * each_ + 1); }

  bool Prepare(const Model& m) override {
    View v;
    if (m.onTrack && m.trackLength > 0) {
      RecordGaps(m);
      Build(m, v);
    }
    if (v == view_) return false;
    view_ = v;
    return true;
  }

  void Draw(Painter& p) override {
    const float w = Width();
    p.Panel(w, Height());
    if (!view_.valid) {
      p.Text(0, 0, w, Height(), L"RELATIVE", Font::TextBold, Col::Dim, Align::Center);
      return;
    }
    float y = kPad;
    if (header_) {
      float x = kPad + kPosW;
      if (number_) { p.Text(x, y, kNumW, kHeaderH, L"#", Font::Small, Col::Dim, Align::Center); x += kNumW; }
      if (brand_) x += kBrandW;
      p.Text(x + 6, y, kNameW, kHeaderH, L"DRIVER", Font::Small, Col::Dim);
      x += kNameW + kTagW;
      if (last_) { p.Text(x, y, kLapW, kHeaderH, L"LAST", Font::Small, Col::Dim, Align::Right); x += kLapW; }
      if (best_) { p.Text(x, y, kLapW, kHeaderH, L"BEST", Font::Small, Col::Dim, Align::Right); x += kLapW; }
      if (sectors_)
        for (int k = 0; k < 3; ++k, x += kSecW) p.Textf(x, y, kSecW, kHeaderH, Font::Small, Col::Dim, Align::Right, L"S%d", k + 1);
      p.Text(x, y, kGapW - 4, kHeaderH, L"GAP", Font::Small, Col::Dim, Align::Right);
      if (trend_) p.Text(x + kGapW, y, kTrendW - 4, kHeaderH, L"PER LAP", Font::Small, Col::Dim, Align::Right);
      y += kHeaderH;
    }

    for (int i = 0; i < view_.count; ++i, y += kRowH) {
      const Row& r = view_.rows[i];
      if (r.classPlace == 0) continue; // padding row
      if (r.player) p.FillRounded(3, y, w - 6, kRowH, 3, playerRow_);

      D2D1_COLOR_F text = text_;
      if (r.lapDiff > 0) text = lapping_;
      else if (r.lapDiff < 0) text = lapped_;
      if (r.pit) text.a *= 0.45f;
      auto timeColor = [&](TimeTint t) {
        D2D1_COLOR_F c = t == kTintClass ? classBest_ : t == kTintPersonal ? personalBest_ : text;
        if (r.pit) c.a *= 0.6f;
        return c;
      };

      float x = kPad;
      if (stripe_) p.Fill(x, y + 3, 3, kRowH - 6, ClassColor(r.cls));
      p.Textf(x + 4, y, kPosW - 6, kRowH, Font::TextBold, text, Align::Right, L"%d", overall_ ? r.place : r.classPlace);
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
      else if (r.lapDiff != 0) p.Textf(x, y, kTagW, kRowH, Font::Small, text, Align::Center, L"%+dL", r.lapDiff);
      x += kTagW;

      wchar_t buf[24];
      if (last_) {
        FormatTime(buf, 24, r.lastMs, timeDecimals_);
        p.Text(x, y, kLapW, kRowH, buf, Font::Text, r.lastMs ? timeColor(r.lastTint) : Col::Dim, Align::Right);
        x += kLapW;
      }
      if (best_) {
        FormatTime(buf, 24, r.bestMs, timeDecimals_);
        p.Text(x, y, kLapW, kRowH, buf, Font::Text, r.bestMs ? timeColor(r.bestTint) : Col::Dim, Align::Right);
        x += kLapW;
      }
      if (sectors_) {
        for (int k = 0; k < 3; ++k, x += kSecW) {
          FormatTime(buf, 24, r.secMs[k], timeDecimals_);
          D2D1_COLOR_F c = r.secMs[k] ? timeColor(r.secTint[k]) : Col::Dim;
          if (r.secOld[k]) c.a *= 0.45f;
          p.Text(x, y, kSecW, kRowH, buf, Font::Small, c, Align::Right);
        }
      }
      if (!r.player) {
        FormatGap(buf, 24, r.gapMs, gapDecimals_);
        p.Text(x, y, kGapW - 4, kRowH, buf, Font::TextBold, text, Align::Right);
        if (trend_ && r.trendMs != INT_MIN && std::abs(r.trendMs) >= 20) {
          // Ahead (rows above you): shrinking is good. Behind: growing is good.
          const bool ahead = i < view_.count / 2;
          const bool good = ahead ? r.trendMs < 0 : r.trendMs > 0;
          const D2D1_COLOR_F c = good ? personalBest_ : Col::Rgb(0xFF8A65);
          p.Textf(x + kGapW, y, kTrendW - 4, kRowH, Font::Small, c, Align::Right, L"%ls %.2f", r.trendMs < 0 ? L"▼" : L"▲",
                  std::abs(r.trendMs) / 1000.0);
        }
      }
    }
  }

private:
  struct Cand { int idx; double d, od; };

  void Build(const Model& m, View& v) const {
    const Snapshot& s = *m.snap;
    const double L = m.trackLength;
    const VehicleScoringInfoV01& me = *m.player;
    const double lapTime = m.refLapTime > 0 ? m.refLapTime : 120.0;

    // Positions brought up to date (scoring is ~6 Hz) so the order and gaps don't lag.
    const double myD = m.LiveLapDist(me);
    Cand ahead[kMaxVehicles], behind[kMaxVehicles];
    int na = 0, nb = 0;
    for (int i = 0; i < s.numVehicles; ++i) {
      if (i == m.playerIdx) continue;
      const VehicleScoringInfoV01& o = s.vehicles[i];
      if (o.mFinishStatus >= 2) continue; // DNF / DQ
      const double od = m.LiveLapDist(o);
      double d = od - myD;
      if (d > L * 0.5) d -= L;
      if (d < -L * 0.5) d += L;
      (d >= 0 ? ahead[na++] : behind[nb++]) = {i, d, od};
    }
    std::partial_sort(ahead, ahead + std::min(na, each_), ahead + na, [](auto& a, auto& b) { return a.d < b.d; });
    std::partial_sort(behind, behind + std::min(nb, each_), behind + nb, [](auto& a, auto& b) { return a.d > b.d; });
    na = std::min(na, each_);
    nb = std::min(nb, each_);

    v.valid = true;
    // Pad so the player row stays in the middle even with few cars ahead.
    for (int k = 0; k < each_ - na; ++k) v.rows[v.count++] = Row{};
    for (int k = na - 1; k >= 0; --k) v.rows[v.count++] = MakeRow(m, ahead[k], myD, lapTime);
    const int meRow = v.count;
    v.rows[v.count++] = MakeRow(m, Cand{m.playerIdx, 0.0, myD}, myD, lapTime);
    for (int k = 0; k < nb; ++k) v.rows[v.count++] = MakeRow(m, behind[k], myD, lapTime);

    // The cars right in front of / behind you: LMU's own gaps (the ones on the car's display),
    // computed by the game every frame. Used when they agree with ours (same car).
    if (m.telem) {
      auto useLmu = [](Row& r, float lmu) {
        const double g = std::fabs(lmu), ours = r.gapMs / 1000.0;
        if (g > 0.0 && g < 60.0 && std::fabs(g - ours) < std::max(1.0, ours * 0.35)) r.gapMs = Quantize(g, 0.001);
      };
      if (na > 0) useLmu(v.rows[meRow - 1], m.telem->mTimeGapCarAhead);
      if (nb > 0) useLmu(v.rows[meRow + 1], m.telem->mTimeGapCarBehind);
    }
  }

  // Gap history per car, sampled every 2 s, to show how the gap moved over the last lap.
  struct History {
    float t[kHistory]{};   // session time
    float gap[kHistory]{}; // signed distance gap in metres (+ = ahead of you)
    int head = 0, count = 0;
  };

  void RecordGaps(const Model& m) {
    const Snapshot& s = *m.snap;
    const double et = s.scoring.mCurrentET;
    if (et < lastSampleET_) history_.clear(); // new session
    if (et - lastSampleET_ < 2.0 && lastSampleET_ <= et) return;
    lastSampleET_ = et;
    const double L = m.trackLength;
    for (int i = 0; i < s.numVehicles; ++i) {
      if (i == m.playerIdx) continue;
      const VehicleScoringInfoV01& o = s.vehicles[i];
      double d = o.mLapDist - m.player->mLapDist;
      if (d > L * 0.5) d -= L;
      if (d < -L * 0.5) d += L;
      History& h = history_[o.mID];
      if (o.mInPits || m.player->mInPits) { h.count = 0; continue; } // a pit stop breaks the comparison
      h.t[h.head] = static_cast<float>(et);
      h.gap[h.head] = static_cast<float>(d);
      h.head = (h.head + 1) % kHistory;
      h.count = std::min(h.count + 1, kHistory);
    }
  }

  // Change of |gap| (in seconds) since about one lap ago, INT_MIN if not known yet.
  int Trend(const Model& m, long id, double lapTime) const {
    const auto it = history_.find(id);
    if (it == history_.end() || it->second.count < 4) return INT_MIN;
    const History& h = it->second;
    const int last = (h.head - 1 + kHistory) % kHistory;
    const float target = h.t[last] - static_cast<float>(lapTime);
    for (int k = 1; k < h.count; ++k) {
      const int i = (h.head - 1 - k + 2 * kHistory) % kHistory;
      if (h.t[i] > target) continue;
      if (std::fabs(h.gap[i]) > m.trackLength * 0.4 || std::fabs(h.gap[last]) > m.trackLength * 0.4) return INT_MIN;
      const double change = (std::fabs(h.gap[last]) - std::fabs(h.gap[i])) / m.trackLength * lapTime;
      if (std::fabs(change) > 10.0) return INT_MIN; // overtakes across the line, spins...
      return static_cast<int>(std::lround(change * 100.0)) * 10; // 0.01 s steps
    }
    return INT_MIN;
  }

  Row MakeRow(const Model& m, const Cand& c, double myD, double lapTime) const {
    const int idx = c.idx;
    const double d = c.d;
    const Snapshot& s = *m.snap;
    const VehicleScoringInfoV01& o = s.vehicles[idx];
    const VehicleScoringInfoV01& me = *m.player;
    Row r;
    r.place = o.mPlace;
    r.classPlace = ClassPlace(s, o);
    CarNumber(r.number, o);
    r.brand = BrandFor(s, o);
    CopyName(r.name, o.mDriverName);
    memcpy(r.cls, o.mVehicleClass, sizeof(r.cls));
    r.cls[sizeof(r.cls) - 1] = 0;
    // Gap in time along the track at your recorded pace (a car 100 m ahead on a straight is closer
    // in time than one 100 m ahead in a hairpin); proportional to the lap time if no lap recorded yet.
    double gap = -1;
    if (m.timing && idx != m.playerIdx) gap = d >= 0 ? m.timing->TimeAlong(myD, c.od) : m.timing->TimeAlong(c.od, myD);
    if (gap < 0 || gap > lapTime * 0.6) gap = std::fabs(d) / m.trackLength * lapTime;
    r.gapMs = Quantize(gap, 0.001);
    r.pit = o.mInPits;
    r.player = idx == m.playerIdx;
    if (trend_ && !r.player) r.trendMs = Trend(m, o.mID, lapTime);
    // Total distance difference minus on-track offset = whole laps between us.
    const double total = (o.mTotalLaps - me.mTotalLaps) * m.trackLength + (o.mLapDist - me.mLapDist);
    // Laps ahead / behind only mean something in a race (in practice people join at any time).
    const bool race = s.scoring.mSession >= 10 && s.scoring.mSession <= 13;
    if (race) r.lapDiff = static_cast<int>(std::lround((total - d) / m.trackLength));

    if (o.mLastLapTime > 0) r.lastMs = static_cast<int>(std::lround(o.mLastLapTime * 1000.0));
    if (o.mBestLapTime > 0) r.bestMs = static_cast<int>(std::lround(o.mBestLapTime * 1000.0));
    r.lastTint = TintFor(m.timing, o, 3, o.mLastLapTime);
    r.bestTint = TintFor(m.timing, o, 3, o.mBestLapTime) == kTintClass ? kTintClass : kTintNormal;
    // Sectors live: the ones completed on this lap, the rest from the previous lap (dimmed).
    // LMU: mSector 1 = in S1, 2 = in S2, 0 = in S3.
    SectorSet last;
    const bool haveLast = SectorsFromCumulative(o.mLastSector1, o.mLastSector2, o.mLastLapTime, last);
    double cur[3] = {0, 0, 0};
    if ((o.mSector == 2 || o.mSector == 0) && o.mCurSector1 > 0) cur[0] = o.mCurSector1;
    if (o.mSector == 0 && o.mCurSector1 > 0 && o.mCurSector2 > o.mCurSector1) cur[1] = o.mCurSector2 - o.mCurSector1;
    for (int k = 0; k < 3; ++k) {
      double t = cur[k];
      if (t <= 0 && haveLast) {
        t = last.s[k];
        r.secOld[k] = o.mSector != 1; // in S1 the previous lap is the one just finished
      }
      if (t <= 0) continue;
      r.secMs[k] = static_cast<int>(std::lround(t * 1000.0));
      r.secTint[k] = TintFor(m.timing, o, k, t);
    }
    return r;
  }

  static int ClassPlace(const Snapshot& s, const VehicleScoringInfoV01& v) {
    int place = 1;
    for (int i = 0; i < s.numVehicles; ++i) {
      const VehicleScoringInfoV01& o = s.vehicles[i];
      if (o.mPlace < v.mPlace && strncmp(o.mVehicleClass, v.mVehicleClass, sizeof(v.mVehicleClass)) == 0) ++place;
    }
    return place;
  }

  int each_;
  bool overall_, header_, number_, brand_, last_, best_, sectors_;
  int timeDecimals_, gapDecimals_;
  bool trend_, stripe_;
  D2D1_COLOR_F playerRow_, text_, lapping_, lapped_, classBest_, personalBest_;
  View view_;
  std::unordered_map<long, History> history_;
  double lastSampleET_ = -1e9;
};

} // namespace

const WidgetType kRelativeWidget{
  "relative", "Relative", "Cars closest to you on track: gap, last lap, best lap and last-lap sectors.",
  true, 16, 887, 100, kOptions, CreateWidget<RelativeWidget>};
