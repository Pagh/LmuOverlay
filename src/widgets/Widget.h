#pragma once
#include "Model.h"
#include "Options.h"
#include "Painter.h"
#include <memory>
#include <span>
#include <vector>

class Widget;

// Static description of a widget kind: its INI section, defaults and options.
struct WidgetType {
  const char* id;           // INI section name in profile files
  const char* name;         // shown in the settings window
  const char* description;
  bool enabledByDefault;
  int defX, defY;           // position relative to the overlay's monitor
  int defIntervalMs;
  std::span<const OptionDef> options; // widget-specific options (common ones are added automatically)
  std::unique_ptr<Widget> (*create)(const WidgetType&, const OptionSet&);
};

std::span<const WidgetType> WidgetTypes();
const WidgetType* FindWidgetType(std::string_view id);
// Common options (enabled, position, scale, colours...) + the widget's own.
std::vector<OptionDef> FullSchema(const WidgetType& t);

// A widget turns the Model into a small comparable "view" in Prepare() and is
// only redrawn when that view changed, so most frames cost nothing.
class Widget {
public:
  Widget(const WidgetType& type, const OptionSet& o)
      : type_(type),
        x_(o.Int("x")),
        y_(o.Int("y")),
        interval_(o.Int("update_ms") / 1000.0),
        scale_(o.Float("scale")),
        bg_(o.Color("background")) {}
  virtual ~Widget() = default;

  const WidgetType& Type() const { return type_; }
  const char* Section() const { return type_.id; }
  int X() const { return x_; }
  int Y() const { return y_; }
  void SetPos(int x, int y) { x_ = x; y_ = y; }
  double Interval() const { return interval_; }
  float Scale() const { return scale_; }
  D2D1_COLOR_F Background() const { return bg_; }

  virtual float Width() const = 0;   // unscaled pixels
  virtual float Height() const = 0;

  // Rebuild the view from the model. Return true if it differs from what's on screen.
  virtual bool Prepare(const Model& m) = 0;
  virtual void Draw(Painter& p) = 0;

  double nextUpdate = 0.0;           // managed by the overlay

private:
  const WidgetType& type_;
  int x_, y_;
  double interval_;
  float scale_;
  D2D1_COLOR_F bg_;
};

using WidgetList = std::vector<std::unique_ptr<Widget>>;
// Instantiates every enabled widget of a profile.
WidgetList CreateWidgets(const IniDoc& profile);

template <typename T>
std::unique_ptr<Widget> CreateWidget(const WidgetType& t, const OptionSet& o) { return std::make_unique<T>(t, o); }

// Perf counters filled by the app loop, shown by the perf widget.
struct PerfStats {
  float renderMsAvg = 0.f, renderMsMax = 0.f;
  float lockHoldUsMax = 0.f, lockWaitUsMax = 0.f;
  float snapshotsPerSec = 0.f;
  float redrawsPerSec = 0.f;
};
extern PerfStats g_perf;

// Widget type descriptors, one per widget file.
extern const WidgetType kRelativeWidget, kStandingsWidget, kDeltaWidget, kSectorsWidget, kFuelWidget, kInputsWidget,
    kTyresWidget, kDamageWidget, kPerfWidget, kTrackInfoWidget, kTrackMapWidget, kRadarWidget, kClassWarnWidget,
    kStrategyWidget;

// Contextual widgets draw nothing while they have nothing to say; in edit mode (and the
// settings preview) they show this labelled box instead, so they can still be positioned.
void DrawPlaceholder(Painter& p, float w, float h, const wchar_t* label);

// Class ranking by speed (Hypercar > LMP2 > LMP3 > GTE > GT3), 0 if unknown.
int ClassRank(const char* vehicleClass);

// Game strings are fixed-size char arrays (not always NUL-terminated); UTF-8 with Latin-1 fallback.
template <size_t N, size_t M>
inline void CopyName(wchar_t (&dst)[N], const char (&src)[M]) {
  const int len = static_cast<int>(strnlen(src, M));
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, len, dst, static_cast<int>(N - 1));
  if (n <= 0 && len > 0) n = MultiByteToWideChar(1252, 0, src, len, dst, static_cast<int>(N - 1));
  dst[n > 0 ? n : 0] = 0;
}
inline int Quantize(double v, double step) { return static_cast<int>(v / step + (v >= 0 ? 0.5 : -0.5)); }

// Formats a non-negative time with 1..3 decimals into buf ("12.345").
void FormatGap(wchar_t* buf, size_t n, int ms, int decimals, const wchar_t* prefix = L"");
// Sector / lap time with 1..3 decimals: "31.204" or "1:45.123".
void FormatTime(wchar_t* buf, size_t n, int ms, int decimals);

// Car number parsed from the vehicle name ("... #397"), empty if none.
void CarNumber(wchar_t (&out)[8], const VehicleScoringInfoV01& v);

// Car make from the model name ("BMW M4 LMGT3" -> "BMW"), as a short code plus a brand colour.
struct Brand {
  wchar_t code[5]{};
  D2D1_COLOR_F color{};
  bool operator==(const Brand& o) const { return wcscmp(code, o.code) == 0; }
};
Brand BrandFor(const Snapshot& s, const VehicleScoringInfoV01& v);
void DrawBrand(Painter& p, float x, float y, float w, float h, const Brand& b);

// Timing colours: 0 normal, 1 driver's own best this session, 2 fastest in class this session.
enum TimeTint : uint8_t { kTintNormal, kTintPersonal, kTintClass };
TimeTint TintFor(const Timing* timing, const VehicleScoringInfoV01& v, int sector /*0..2, 3 = lap*/, double time);
