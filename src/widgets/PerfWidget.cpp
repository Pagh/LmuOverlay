// Overlay self-diagnostics: proves (or disproves) that the overlay is cheap.
#include "widgets/Widget.h"

PerfStats g_perf;

namespace {

constexpr float kW = 230.f, kH = 92.f, kPad = 8.f, kRowH = 19.f;

struct View {
  int renderAvgUs = 0, renderMaxUs = 0, lockHoldUs = 0, lockWaitUs = 0, snaps = 0, redraws = 0;
  bool operator==(const View&) const = default;
};

class PerfWidget final : public Widget {
public:
  PerfWidget(const WidgetType& t, const OptionSet& o) : Widget(t, o) {}

  float Width() const override { return kW; }
  float Height() const override { return kH; }

  bool Prepare(const Model&) override {
    View v{static_cast<int>(g_perf.renderMsAvg * 1000), static_cast<int>(g_perf.renderMsMax * 1000),
           static_cast<int>(g_perf.lockHoldUsMax), static_cast<int>(g_perf.lockWaitUsMax),
           static_cast<int>(g_perf.snapshotsPerSec + 0.5f), static_cast<int>(g_perf.redrawsPerSec + 0.5f)};
    if (v == view_) return false;
    view_ = v;
    return true;
  }

  void Draw(Painter& p) override {
    p.Panel(kW, kH);
    float y = kPad;
    Row(p, y, L"Draw avg / max", L"%d / %d µs", view_.renderAvgUs, view_.renderMaxUs);
    Row(p, y, L"LMU lock held max", L"%d µs", view_.lockHoldUs);
    Row(p, y, L"LMU lock wait max", L"%d µs", view_.lockWaitUs);
    Row(p, y, L"Copies / redraws", L"%d / %d per s", view_.snaps, view_.redraws);
  }

private:
  template <typename... A>
  static void Row(Painter& p, float& y, const wchar_t* label, const wchar_t* fmt, A... a) {
    p.Text(kPad, y, 120, kRowH, label, Font::Small, Col::Dim);
    p.Textf(kPad + 110, y, kW - 2 * kPad - 110, kRowH, Font::Text, Col::Text, Align::Right, fmt, a...);
    y += kRowH;
  }

  View view_;
};

} // namespace

const WidgetType kPerfWidget{
  "perf", "Performance", "The overlay's own cost: draw time and how long LMU's data lock was held.",
  false, 40, 40, 500, {}, CreateWidget<PerfWidget>};
