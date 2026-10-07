// Track map: the circuit outline (learned from the cars' positions, saved per track), every
// car as a dot in its class colour, you in white, and yellow-flagged sectors highlighted.
#include "widgets/Widget.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr int kMaxDots = kMaxVehicles;
constexpr float kPad = 8.f;

struct Dot {
  int16_t x = 0, y = 0;
  uint8_t cls = 0;   // index into the class colour list, 255 = player
  bool pit = false;
  bool operator==(const Dot&) const = default;
};

struct View {
  bool valid = false;
  bool ready = false;     // map learned
  int learnPct = 0;
  uint32_t mapVersion = 0;
  bool yellow[3]{};
  int count = 0;
  Dot dots[kMaxDots];
  bool operator==(const View&) const = default;
};

const OptionDef kOptions[] = {
  OptInt("size", "Size (px)", 200, 100, 600),
  OptBool("show_yellow", "Highlight yellow sectors", true),
  OptInt("dot_size", "Car dot size (px)", 5, 2, 12),
  OptColor("track", "Track", 0xC8CED880),
  OptColor("player", "You", 0xF2F4F7FF),
  OptColor("yellow", "Yellow sector", 0xFFC23DFF),
};

class TrackMapWidget final : public Widget {
public:
  TrackMapWidget(const WidgetType& t, const OptionSet& o)
      : Widget(t, o),
        size_(static_cast<float>(o.Int("size"))),
        showYellow_(o.Bool("show_yellow")),
        dot_(static_cast<float>(o.Int("dot_size"))),
        track_(o.Color("track")),
        player_(o.Color("player")),
        yellow_(o.Color("yellow")) {}

  float Width() const override { return size_; }
  float Height() const override { return std::round(size_ * 0.85f); }

  bool Prepare(const Model& m) override {
    View v;
    if (m.onTrack && m.player && m.timing) Build(m, v);
    if (v == view_) return false;
    view_ = v;
    return true;
  }

  void Draw(Painter& p) override {
    const float w = Width(), h = Height();
    if (!view_.valid) { if (p.EditMode()) DrawPlaceholder(p, w, h, L"TRACK MAP"); return; }
    p.Panel(w, h);
    if (!view_.ready) {
      p.Textf(0, 0, w, h, Font::Small, Col::Dim, Align::Center, L"learning the track… %d%%", view_.learnPct);
      return;
    }
    EnsureGeometry(p);
    if (outline_) p.DrawGeometry(outline_.Get(), track_, std::max(3.f, size_ / 40.f));
    for (int s = 0; s < 3; ++s)
      if (view_.yellow[s] && sector_[s]) p.DrawGeometry(sector_[s].Get(), yellow_, std::max(3.f, size_ / 40.f));
    // Others first, you on top.
    for (int pass = 0; pass < 2; ++pass)
      for (int i = 0; i < view_.count; ++i) {
        const Dot& d = view_.dots[i];
        const bool me = d.cls == 255;
        if (me != (pass == 1)) continue;
        const float r = me ? dot_ + 2 : dot_;
        D2D1_COLOR_F c = me ? player_ : ClassColorByIndex(d.cls);
        if (d.pit) c.a = 0.35f;
        if (me) p.FillRounded(d.x - r - 1.5f, d.y - r - 1.5f, 2 * r + 3, 2 * r + 3, r + 1.5f, Col::Rgb(0x0D0F12));
        p.FillRounded(d.x - r, d.y - r, 2 * r, 2 * r, r, c);
      }
  }

private:
  // Fit the outline into the widget (LMU world: z goes up on the map).
  struct Fit { float minX = 0, maxZ = 0, scale = 1, ox = 0, oy = 0; };

  static D2D1_COLOR_F ClassColorByIndex(uint8_t i) {
    static const char* const kNames[] = {"Hyper", "LMP2", "LMP3", "GTE", "GT3", "?"};
    return ClassColor(kNames[std::min<int>(i, 5)]);
  }
  static uint8_t ClassIndex(const char* cls) {
    const int r = ClassRank(cls); // 5 hyper .. 1 GT3, 0 unknown
    return static_cast<uint8_t>(r == 0 ? 5 : 5 - r);
  }

  Fit MakeFit(const TrackMap& map) const {
    Fit f;
    const auto& X = map.X();
    const auto& Z = map.Z();
    float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (size_t i = 0; i < X.size(); ++i) {
      minX = std::min(minX, X[i]); maxX = std::max(maxX, X[i]);
      minZ = std::min(minZ, Z[i]); maxZ = std::max(maxZ, Z[i]);
    }
    const float w = Width() - 2 * kPad, h = Height() - 2 * kPad;
    f.scale = std::min(w / std::max(1.f, maxX - minX), h / std::max(1.f, maxZ - minZ));
    f.minX = minX;
    f.maxZ = maxZ;
    f.ox = kPad + (w - (maxX - minX) * f.scale) / 2;
    f.oy = kPad + (h - (maxZ - minZ) * f.scale) / 2;
    return f;
  }
  static D2D1_POINT_2F Project(const Fit& f, float x, float z) {
    return {f.ox + (x - f.minX) * f.scale, f.oy + (f.maxZ - z) * f.scale};
  }

  void Build(const Model& m, View& v) {
    const TrackMap& map = m.timing->Map();
    const Snapshot& s = *m.snap;
    v.valid = true;
    v.mapVersion = map.Version();
    if (!map.Complete()) {
      v.learnPct = static_cast<int>(map.Progress() * 100.f);
      return;
    }
    v.ready = true;
    if (map.Version() != fitVersion_ || &map != fitMap_) {
      fit_ = MakeFit(map);
      fitVersion_ = map.Version();
      fitMap_ = &map;
      geomVersion_ = 0;
    }
    if (showYellow_)
      for (int k = 0; k < 3; ++k) v.yellow[k] = s.scoring.mSectorFlag[k] == 1 && map.SectorStart(1) > 0;
    for (int i = 0; i < s.numVehicles && v.count < kMaxDots; ++i) {
      const VehicleScoringInfoV01& o = s.vehicles[i];
      if (o.mInGarageStall) continue;
      float x, z;
      if (const Snapshot::CarModel* c = s.CarFor(o.mID)) { x = static_cast<float>(c->pos.x); z = static_cast<float>(c->pos.z); }
      else if (!map.Position(o.mLapDist, x, z)) continue;
      const D2D1_POINT_2F pt = Project(fit_, x, z);
      Dot& d = v.dots[v.count++];
      d.x = static_cast<int16_t>(std::lround(pt.x));
      d.y = static_cast<int16_t>(std::lround(pt.y));
      d.cls = i == m.playerIdx ? 255 : ClassIndex(o.mVehicleClass);
      d.pit = o.mInPits;
    }
    map_ = &map;
  }

  void EnsureGeometry(Painter& p) {
    if (!map_ || (geomVersion_ == map_->Version() && geomMap_ == map_ && factory_ == p.Factory() && outline_)) return;
    geomMap_ = map_;
    factory_ = p.Factory();
    geomVersion_ = map_->Version();
    outline_.Reset();
    for (auto& g : sector_) g.Reset();
    const auto& X = map_->X();
    const auto& Z = map_->Z();
    const size_t n = X.size();
    if (n < 3) return;
    auto build = [&](ComPtr<ID2D1PathGeometry>& g, size_t from, size_t to, bool closed) {
      if (FAILED(factory_->CreatePathGeometry(&g))) return;
      ComPtr<ID2D1GeometrySink> sink;
      if (FAILED(g->Open(&sink))) return;
      sink->SetSegmentFlags(D2D1_PATH_SEGMENT_FORCE_ROUND_LINE_JOIN);
      sink->BeginFigure(Project(fit_, X[from % n], Z[from % n]), D2D1_FIGURE_BEGIN_HOLLOW);
      for (size_t i = from + 1; i <= to; ++i) sink->AddLine(Project(fit_, X[i % n], Z[i % n]));
      sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
      sink->Close();
    };
    build(outline_, 0, n - 1, true);
    const double s2 = map_->SectorStart(1), s3 = map_->SectorStart(2);
    if (s2 > 0 && s3 > s2) {
      const size_t b2 = static_cast<size_t>(s2 / TrackMap::kBin), b3 = static_cast<size_t>(s3 / TrackMap::kBin);
      build(sector_[0], 0, b2, false);
      build(sector_[1], b2, b3, false);
      build(sector_[2], b3, n, false);
    }
  }

  float size_;
  bool showYellow_;
  float dot_;
  D2D1_COLOR_F track_, player_, yellow_;
  View view_;
  const TrackMap* map_ = nullptr;
  const TrackMap* fitMap_ = nullptr;
  const TrackMap* geomMap_ = nullptr;
  Fit fit_;
  uint32_t fitVersion_ = 0, geomVersion_ = 0;
  ID2D1Factory* factory_ = nullptr;
  ComPtr<ID2D1PathGeometry> outline_, sector_[3];
};

} // namespace

const WidgetType kTrackMapWidget{
  "trackmap", "Track map", "Circuit outline with every car; yellow sectors highlighted. Learns each track once.",
  true, 1704, 140, 100, kOptions, CreateWidget<TrackMapWidget>};
