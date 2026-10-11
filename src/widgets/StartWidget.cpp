// Race start: during the formation lap and the countdown, how far (metres and seconds, at the
// leader's speed) the overall leader is from the points where LMU started the countdown and went
// green the last time here (learned at each start, see Timing::Start), or from the start line the
// first time. Plus your speed, the pit limiter and the distance to the car you follow.
// Shown only before the green in races (and a few seconds after it).
#include "widgets/Widget.h"
#include <algorithm>
#include <climits>
#include <cmath>

namespace {

constexpr float kW = 320.f, kH = 92.f, kPad = 10.f;

enum class Stage : uint8_t { Hidden, Formation, Countdown, Green };

struct View {
  Stage stage = Stage::Hidden;
  bool leaderIsYou = false;
  int toCountdown = INT_MIN, toGreen = INT_MIN; // m (leader), INT_MIN = not learned
  int toLine = 0;                               // m (leader to the start line)
  int etaCountdown = -1, etaGreen = -1, etaLine = -1; // s at the leader's speed
  int lights = 0, lightsOn = 0;
  int speed = 0;                                // your speed, km/h
  bool limiter = false;
  int ahead = -1;                               // m to the car you follow
  bool operator==(const View&) const = default;
};

const OptionDef kOptions[] = {
  OptInt("green_seconds", "After the green, keep showing for (s)", 4, 0, 20),
  OptColor("green", "Green", 0x3DDC84FF),
  OptColor("countdown", "Countdown", 0xFFC23DFF),
};

class StartWidget final : public Widget {
public:
  StartWidget(const WidgetType& t, const OptionSet& o)
      : Widget(t, o), greenS_(o.Int("green_seconds")), green_(o.Color("green")), countdown_(o.Color("countdown")) {}

  float Width() const override { return kW; }
  float Height() const override { return kH; }

  bool Prepare(const Model& m) override {
    View v;
    if (m.onTrack && m.trackLength > 0 && m.timing) Build(m, v);
    if (v == view_) return false;
    view_ = v;
    return true;
  }

  void Draw(Painter& p) override {
    const View& v = view_;
    if (v.stage == Stage::Hidden) { if (p.EditMode()) DrawPlaceholder(p, kW, kH, L"RACE START"); return; }
    p.Panel(kW, kH);
    if (v.stage == Stage::Green) {
      p.Text(0, 0, kW, kH, L"GREEN  ·  GO", Font::Big, green_, Align::Center);
      return;
    }
    const float x = kPad, w = kW - 2 * kPad;
    // Line 1: stage + start lights.
    p.Text(x, 4, w, 18, v.stage == Stage::Countdown ? L"START COUNTDOWN" : L"FORMATION LAP", Font::TextBold,
           v.stage == Stage::Countdown ? countdown_ : Col::Text);
    if (v.lights > 0) p.Textf(x, 4, w, 18, Font::Small, Col::Dim, Align::Right, L"lights %d / %d", v.lightsOn, v.lights);
    else if (v.leaderIsYou) p.Text(x, 4, w, 18, L"you lead", Font::Small, countdown_, Align::Right);

    // Line 2 (big): the next thing that happens, from the leader's position.
    wchar_t big[48], sub[64];
    D2D1_COLOR_F c = Col::Text;
    if (v.stage == Stage::Formation && v.toCountdown != INT_MIN && v.toCountdown > 0) {
      Eta(big, L"COUNTDOWN in", v.toCountdown, v.etaCountdown);
      c = countdown_;
    } else if (v.toGreen != INT_MIN && v.toGreen > -50) {
      Eta(big, L"GREEN in", std::max(0, v.toGreen), v.etaGreen);
      c = green_;
    } else {
      Eta(big, L"LINE in", v.toLine, v.etaLine);
    }
    p.Text(x, 22, w, 30, big, Font::Mid, c);
    if (v.toGreen == INT_MIN) wcscpy_s(sub, L"first start here: learning where it goes green");
    else if (v.stage == Stage::Formation && v.toCountdown != INT_MIN && v.toCountdown > 0)
      swprintf_s(sub, L"then green %d m later", v.toCountdown - v.toGreen);
    else swprintf_s(sub, L"leader %d m from the line", v.toLine);
    p.Text(x, 50, w, 16, sub, Font::Small, Col::Dim);

    // Line 3: you.
    p.Textf(x, 70, w, 18, Font::Text, Col::Text, Align::Left, L"%d km/h", v.speed);
    p.Text(x + 74, 70, 100, 18, v.limiter ? L"LIMITER ON" : L"limiter off", Font::Small, v.limiter ? green_ : Col::Dim);
    if (v.ahead >= 0) p.Textf(x, 70, w, 18, Font::Text, Col::Text, Align::Right, L"%d m to the car ahead", v.ahead);
  }

private:
  static void Eta(wchar_t (&out)[48], const wchar_t* what, int metres, int secs) {
    if (secs >= 0) swprintf_s(out, L"%ls %d m · %d s", what, metres, secs);
    else swprintf_s(out, L"%ls %d m", what, metres);
  }

  void Build(const Model& m, View& v) {
    const Snapshot& s = *m.snap;
    const ScoringInfoV01& sc = s.scoring;
    const bool race = sc.mSession >= 10 && sc.mSession <= 13;
    const int phase = sc.mGamePhase;
    if (!race) return;
    if (phase < 3 || phase > 5) { greenAt_ = -1; return; }
    if (phase == 5) {
      // Just gone green (seen the countdown / formation first): say so for a moment.
      if (seenPre_ && greenAt_ < 0) greenAt_ = m.now;
      seenPre_ = false;
      if (greenAt_ >= 0 && m.now - greenAt_ < greenS_) v.stage = Stage::Green;
      return;
    }
    seenPre_ = true;
    greenAt_ = -1;
    v.stage = phase == 4 ? Stage::Countdown : Stage::Formation;

    const double L = m.trackLength;
    const VehicleScoringInfoV01* leader = m.player;
    for (int i = 0; i < s.numVehicles; ++i)
      if (s.vehicles[i].mPlace == 1) leader = &s.vehicles[i];
    v.leaderIsYou = leader == m.player;
    double before = L - m.LiveLapDist(*leader);
    if (before > L * 0.5) before -= L;
    const double ls = std::hypot(leader->mLocalVel.x, leader->mLocalVel.z);
    auto eta = [ls](double metres) { return ls > 3.0 && metres >= 0 ? static_cast<int>(std::lround(metres / ls)) : -1; };
    v.toLine = static_cast<int>(std::lround(before));
    v.etaLine = eta(before);
    const Timing::StartPoints& sp = m.timing->Start();
    if (sp.hasCountdown) {
      const double d = before - sp.countdownBefore;
      v.toCountdown = Quantize(d, 10.0) * 10;
      v.etaCountdown = eta(d);
    }
    if (sp.hasGreen) {
      const double d = before - sp.greenBefore;
      v.toGreen = Quantize(d, 10.0) * 10;
      v.etaGreen = eta(d);
    }
    v.lights = sc.mNumRedLights;
    v.lightsOn = sc.mStartLight;

    if (m.telem) {
      const TelemVect3& vel = m.telem->mLocalVel;
      v.speed = static_cast<int>(std::lround(std::sqrt(vel.x * vel.x + vel.y * vel.y + vel.z * vel.z) * 3.6));
      v.limiter = m.telem->mSpeedLimiterActive;
    }
    // The car you follow: one place ahead overall.
    const double myD = m.LiveLapDist(*m.player);
    for (int i = 0; i < s.numVehicles; ++i) {
      if (s.vehicles[i].mPlace != m.player->mPlace - 1) continue;
      double d = m.LiveLapDist(s.vehicles[i]) - myD;
      if (d < -L * 0.5) d += L;
      if (d > L * 0.5) d -= L;
      if (d >= 0) v.ahead = static_cast<int>(std::lround(d));
    }
  }

  int greenS_;
  D2D1_COLOR_F green_, countdown_;
  bool seenPre_ = false;
  double greenAt_ = -1;
  View view_;
};

} // namespace

const WidgetType kStartWidget{
  "start", "Race start",
  "Before the green: how far the leader is from where LMU starts the countdown and goes green (learned at each start "
  "here), your speed, limiter and the distance to the car you follow.",
  true, 800, 200, 100, kOptions, CreateWidget<StartWidget>};
