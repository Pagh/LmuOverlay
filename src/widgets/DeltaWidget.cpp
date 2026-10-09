// Live delta against a chosen reference lap: your best (LMU's own delta), your session best,
// your all-time best on this track (any car of your class), the lobby's fastest lap in your class,
// or your last valid lap (to drive consistently, with the spread of your last laps).
// A wide, low bar: reference on the left, delta in the middle, the lap you're on (predicted
// time, even if invalidated) on the right. Ctrl+Alt+D or a wheel button cycles the reference.
#include "widgets/Widget.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr float kH = 40.f, kPad = 10.f, kSideW = 130.f;

struct View {
  bool valid = false;
  bool hasRef = false;       // a reference lap exists
  bool hasDelta = false;     // ... and the delta is available right now
  bool outLapWait = false;   // out lap before sector 2
  int deltaMs = 0;           // quantised to the configured decimals
  bool invalid = false;
  DeltaRef ref = DeltaRef::LmuBest;
  int refLapMs = 0;
  int spreadCs = -1;         // last-lap reference: spread of your last laps, 0.01 s (-1 = not yet)
  int predictedMs = 0;       // reference + delta
  int lapDs = 0;             // current lap time, 0.1 s
  bool flash = false;        // reference just changed
  bool operator==(const View&) const = default;
};

const OptionDef kOptions[] = {
  OptChoice("reference", "Reference lap", "your best (LMU)|session best|all-time best|lobby fastest lap|last lap", 0,
            "Ctrl+Alt+D (or a wheel button, Settings > General) switches it while driving.\n"
            "Lobby fastest lap: the quickest lap in your class this session, as LMU reports it.\n"
            "Last lap: your previous valid lap, to drive consistently; also shows the spread (±) of your last 5 laps."),
  OptInt("decimals", "Decimals", 3, 1, 3),
  OptInt("width", "Width (px)", 440, 260, 900),
  OptFloat("range", "Bar range (s)", 2.0, 0.2, 10.0, "Delta at which the bar is full"),
  OptBool("show_bar", "Show bar", true),
  OptBool("show_reference", "Show reference (left)", true),
  OptBool("show_lap", "Show the lap you're on (right)", true, "Predicted lap time, shown even if the lap is invalidated"),
  OptColor("faster", "Faster", 0x3DDC84FF),
  OptColor("slower", "Slower", 0xFF5A5AFF),
};

class DeltaWidget final : public Widget {
public:
  DeltaWidget(const WidgetType& t, const OptionSet& o)
      : Widget(t, o),
        decimals_(o.Int("decimals")),
        w_(static_cast<float>(o.Int("width"))),
        range_(o.Float("range")),
        showBar_(o.Bool("show_bar")),
        showRef_(o.Bool("show_reference")),
        showLap_(o.Bool("show_lap")),
        faster_(o.Color("faster")),
        slower_(o.Color("slower")) {}

  float Width() const override { return w_; }
  float Height() const override { return kH; }

  bool Prepare(const Model& m) override {
    View v;
    if (m.onTrack && m.telem && m.player && m.timing) Build(m, v);
    if (v == view_) return false;
    view_ = v;
    return true;
  }

  void Draw(Painter& p) override {
    p.Panel(w_, kH);
    const View& v = view_;
    if (!v.valid) { p.Text(0, 0, w_, kH, L"DELTA", Font::TextBold, Col::Dim, Align::Center); return; }

    wchar_t buf[48];
    if (showRef_) {
      const D2D1_COLOR_F c = v.flash ? Col::Warn : Col::Dim;
      if (v.spreadCs >= 0)
        p.Textf(kPad, 3, kSideW, 16, Font::Small, c, Align::Left, L"vs %ls  ±%.2f", DeltaRefLabel(v.ref), v.spreadCs / 100.0);
      else
        p.Textf(kPad, 3, kSideW, 16, Font::Small, c, Align::Left, L"vs %ls", DeltaRefLabel(v.ref));
      if (v.refLapMs > 0) {
        FormatLapTime(buf, 48, v.refLapMs / 1000.0);
        p.Text(kPad, 17, kSideW, 18, buf, Font::TextBold, v.flash ? Col::Warn : Col::Text);
      } else {
        p.Text(kPad, 17, kSideW, 18, L"no lap yet", Font::Small, Col::Dim);
      }
    }
    if (showLap_ && v.lapDs > 0) {
      const float x = w_ - kPad - kSideW;
      p.Text(x, 3, kSideW, 16, v.invalid ? L"LAP  ·  INVALID" : L"LAP", Font::Small, v.invalid ? slower_ : Col::Dim, Align::Right);
      if (v.hasDelta && v.predictedMs > 0) FormatLapTime(buf, 48, v.predictedMs / 1000.0);
      else swprintf_s(buf, L"%d:%04.1f", v.lapDs / 600, (v.lapDs % 600) / 10.0);
      p.Text(x, 17, kSideW, 18, buf, Font::TextBold, v.invalid ? slower_ : Col::Text, Align::Right);
    }

    const float midW = w_ - 2 * (kPad + kSideW);
    const float mx = kPad + kSideW;
    if (v.outLapWait) {
      p.Text(mx, 0, midW, kH - 4, L"OUT LAP · delta from S2", Font::Small, Col::Dim, Align::Center);
      return;
    }
    if (!v.hasDelta) {
      p.Text(mx, 0, midW, kH - 4, v.hasRef ? L"—" : L"no reference lap yet", Font::Small, Col::Dim, Align::Center);
      return;
    }
    const double delta = v.deltaMs / 1000.0;
    const D2D1_COLOR_F c = delta <= 0 ? faster_ : slower_;
    p.Textf(mx, 1, midW, kH - 8, Font::Big, c, Align::Center, L"%+.*f", decimals_, delta);
    if (!showBar_) return;
    const float barW = w_ - 2 * kPad, mid = w_ * 0.5f, by = kH - 5.f;
    p.Fill(kPad, by, barW, 3, Col::Rgb(0x2B313C, 0.9f));
    const float len = static_cast<float>(std::min(std::fabs(delta) / range_, 1.0)) * (barW * 0.5f);
    if (delta <= 0) p.Fill(mid - len, by, len, 3, c);
    else p.Fill(mid, by, len, 3, c);
    p.Fill(mid - 1, by - 2, 2, 7, Col::Text);
  }

private:
  void Build(const Model& m, View& v) const {
    const Timing& tm = *m.timing;
    v.valid = true;
    v.ref = m.deltaRef;
    v.flash = m.now - m.deltaRefChangedAt < 2.0;
    v.invalid = m.telem->mLapInvalidated || tm.CurrentLapInvalid();
    const double refLap = tm.ReferenceLap(m.deltaRef);
    v.hasRef = refLap > 0;
    if (v.hasRef) v.refLapMs = static_cast<int>(std::lround(refLap * 1000.0));
    if (m.deltaRef == DeltaRef::LastLap) {
      const double spread = tm.Consistency();
      if (spread > 0) v.spreadCs = static_cast<int>(std::lround(spread * 100.0));
    }
    const double lapT = tm.CurrentLapTime();
    if (lapT > 0 && !m.player->mInPits) v.lapDs = static_cast<int>(lapT * 10.0);
    v.outLapWait = tm.OutLapWaiting() && !m.player->mInPits && v.hasRef;
    double delta = 0;
    if (!v.outLapWait && tm.Delta(m.deltaRef, delta)) {
      v.hasDelta = true;
      const double step = decimals_ >= 3 ? 0.001 : decimals_ == 2 ? 0.01 : 0.1;
      v.deltaMs = Quantize(std::clamp(delta, -99.0, 99.0), step) * static_cast<int>(step * 1000.0 + 0.5);
      if (v.hasRef) v.predictedMs = static_cast<int>(std::lround((refLap + delta) * 100.0)) * 10;
    }
  }

  int decimals_;
  float w_;
  double range_;
  bool showBar_, showRef_, showLap_;
  D2D1_COLOR_F faster_, slower_;
  View view_;
};

} // namespace

const WidgetType kDeltaWidget{
  "delta", "Delta", "Live delta to a reference lap: your best, session best, all-time best, the lobby's fastest or your last lap.",
  true, 740, 128, 33, kOptions, CreateWidget<DeltaWidget>};
