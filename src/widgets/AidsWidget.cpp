// TC / ABS activity: one row each. The label lights up while the aid is working, and the strip
// next to it shows when it worked over the last few seconds (newest on the right), so you can
// tell which corner exit / braking zone triggered it. Optionally how many times it kicked in this lap.
#include "widgets/Widget.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr float kPad = 6.f, kRowH = 22.f, kGap = 4.f, kLabelW = 64.f, kCountW = 36.f;
constexpr int kSamples = 160;

struct View {
  bool valid = false;
  int tc = 0, tcMax = 0, abs = 0, absMax = 0;
  bool tcNow = false, absNow = false;
  uint32_t seq = 0;             // strip content; stays 0 while the strips are empty (no redraws)
  int tcCount = 0, absCount = 0;
  bool operator==(const View&) const = default;
};

const OptionDef kOptions[] = {
  OptInt("width", "Width (px)", 300, 160, 900),
  OptInt("history_seconds", "Strip length (s)", 5, 2, 20),
  OptBool("show_count", "Count activations this lap", false),
  OptColor("tc", "TC working", 0xFFC23DFF),
  OptColor("abs", "ABS working", 0x4DA3FFFF),
};

class AidsWidget final : public Widget {
public:
  AidsWidget(const WidgetType& t, const OptionSet& o)
      : Widget(t, o),
        w_(static_cast<float>(o.Int("width"))),
        seconds_(o.Int("history_seconds")),
        count_(o.Bool("show_count")),
        tcCol_(o.Color("tc")),
        absCol_(o.Color("abs")) {}

  float Width() const override { return w_; }
  float Height() const override { return 2 * kPad + 2 * kRowH + kGap; }

  bool Prepare(const Model& m) override {
    View v;
    if (m.onTrack && m.telem) {
      Sample(m, *m.telem);
      const TelemInfoV01& t = *m.telem;
      v.valid = true;
      v.tc = t.mTC; v.tcMax = t.mTCMax;
      v.abs = t.mABS; v.absMax = t.mABSMax;
      // Hold the light a moment so short pulses are visible.
      v.tcNow = m.now - lastOn_[0] < 0.12;
      v.absNow = m.now - lastOn_[1] < 0.12;
      v.seq = active_[0] + active_[1] > 0 ? seq_ : 0;
      if (count_) { v.tcCount = hits_[0]; v.absCount = hits_[1]; }
    } else {
      n_ = 0;
      active_[0] = active_[1] = 0;
    }
    if (v == view_) return false;
    view_ = v;
    return true;
  }

  void Draw(Painter& p) override {
    const float h = Height();
    p.Panel(w_, h);
    const View& v = view_;
    if (!v.valid) { p.Text(0, 0, w_, h, L"TC / ABS", Font::TextBold, Col::Dim, Align::Center); return; }
    DrawRow(p, kPad, 0, L"TC", v.tc, v.tcMax, v.tcNow, tcCol_, v.tcCount);
    DrawRow(p, kPad + kRowH + kGap, 1, L"ABS", v.abs, v.absMax, v.absNow, absCol_, v.absCount);
  }

private:
  void DrawRow(Painter& p, float y, int k, const wchar_t* name, int level, int max, bool on, D2D1_COLOR_F col,
               int hits) const {
    // Label: name + setting; filled with the aid's colour while it's working.
    p.FillRounded(kPad, y, kLabelW, kRowH, 3, on ? col : Col::Row);
    const D2D1_COLOR_F tc = on ? Col::Rgb(0x15181E) : Col::Text;
    if (max > 0) {
      p.Text(kPad + 6, y, kLabelW - 12, kRowH, name, Font::Small, on ? tc : Col::Dim, Align::Left);
      p.Textf(kPad + 6, y, kLabelW - 12, kRowH, Font::TextBold, tc, Align::Right, L"%d", level);
    } else {
      p.Text(kPad + 6, y, kLabelW - 12, kRowH, name, Font::Small, on ? tc : Col::Dim, Align::Left);
      p.Text(kPad + 6, y, kLabelW - 12, kRowH, L"—", Font::Text, on ? tc : Col::Dim, Align::Right);
    }

    // Strip: oldest on the left. Runs of active samples become one block each.
    const float sx = kPad + kLabelW + 6, sw = w_ - sx - kPad - (count_ ? kCountW : 0.f);
    p.FillRounded(sx, y, sw, kRowH, 3, Col::Row);
    const float step = sw / kSamples;
    D2D1_COLOR_F fill = col;
    fill.a = 0.85f;
    int run = -1;
    for (int i = 0; i <= n_; ++i) {
      const bool a = i < n_ && buf_[k][(head_ - n_ + i + kSamples) % kSamples];
      if (a && run < 0) run = i;
      if (!a && run >= 0) {
        const float x0 = sx + sw - (n_ - run) * step, x1 = sx + sw - (n_ - i) * step;
        p.Fill(x0, y + 3, std::max(2.f, x1 - x0), kRowH - 6, fill);
        run = -1;
      }
    }
    if (count_) p.Textf(w_ - kPad - kCountW, y, kCountW, kRowH, Font::Small, hits ? Col::Text : Col::Dim, Align::Right,
                        L"%d×", hits);
  }

  void Sample(const Model& m, const TelemInfoV01& t) {
    const bool on[2] = {t.mTCActive, t.mABSActive};
    // Count separate activations (a gap of 0.4 s starts a new one), per lap.
    if (t.mLapNumber != lap_) { lap_ = t.mLapNumber; hits_[0] = hits_[1] = 0; }
    for (int k = 0; k < 2; ++k) {
      if (!on[k]) continue;
      if (m.now - lastOn_[k] > 0.4) ++hits_[k];
      lastOn_[k] = m.now;
      acc_[k] = true;
    }
    const double step = static_cast<double>(seconds_) / kSamples;
    if (m.now - lastSample_ < step) return;
    if (m.now - lastSample_ > 1.0) { n_ = 0; active_[0] = active_[1] = 0; } // after a pause: fresh strips
    lastSample_ = m.now;
    for (int k = 0; k < 2; ++k) {
      if (n_ == kSamples && buf_[k][head_]) --active_[k]; // overwriting the oldest
      buf_[k][head_] = acc_[k];
      if (acc_[k]) ++active_[k];
      acc_[k] = false;
    }
    head_ = (head_ + 1) % kSamples;
    n_ = std::min(n_ + 1, kSamples);
    ++seq_;
  }

  float w_;
  int seconds_;
  bool count_;
  D2D1_COLOR_F tcCol_, absCol_;
  View view_;

  bool buf_[2][kSamples]{};
  bool acc_[2]{};
  int active_[2]{};             // active samples in the strip
  int head_ = 0, n_ = 0;
  uint32_t seq_ = 0;
  double lastSample_ = 0, lastOn_[2] = {-100, -100};
  long lap_ = -1;
  int hits_[2]{};
};

} // namespace

const WidgetType kAidsWidget{
  "aids", "TC / ABS activity", "Lights up while traction control / ABS are working, with a strip of the last seconds "
  "showing when they kicked in.",
  true, 684, 808, 16, kOptions, CreateWidget<AidsWidget>};
