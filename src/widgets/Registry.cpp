#include "widgets/Widget.h"
#include <cstdio>
#include <cstring>

namespace {
const WidgetType* const kAll[] = {&kStandingsWidget, &kDeltaWidget,  &kTrackInfoWidget, &kSectorsWidget,
                                  &kTrackMapWidget,  &kRelativeWidget, &kClassWarnWidget, &kRadarWidget,
                                  &kInputsWidget,    &kTyresWidget,   &kStrategyWidget,  &kDamageWidget,
                                  &kFuelWidget,      &kPerfWidget};
}

std::span<const WidgetType> WidgetTypes() {
  // Stable array of values for range-for in the UI.
  static const std::vector<WidgetType> all = [] {
    std::vector<WidgetType> v;
    for (const WidgetType* t : kAll) v.push_back(*t);
    return v;
  }();
  return all;
}

const WidgetType* FindWidgetType(std::string_view id) {
  for (const WidgetType& t : WidgetTypes())
    if (id == t.id) return &t;
  return nullptr;
}

std::vector<OptionDef> FullSchema(const WidgetType& t) {
  std::vector<OptionDef> s;
  auto add = [&s](OptionDef d, const char* group) { d.group = group; s.push_back(d); };
  add(OptBool("enabled", "Enabled", t.enabledByDefault), "Layout");
  add(OptInt("x", "X position", t.defX, -1000, 8000, "Pixels from the left edge of the overlay's monitor"), "Layout");
  add(OptInt("y", "Y position", t.defY, -1000, 8000, "Pixels from the top edge of the overlay's monitor"), "Layout");
  add(OptFloat("scale", "Scale", 1.0, 0.5, 3.0), "Layout");
  add(OptInt("update_ms", "Update interval (ms)", t.defIntervalMs, 10, 2000,
             "How often the widget checks for changes. It only redraws when something it shows changed."),
      "Layout");
  add(OptColor("background", "Background", 0x15181ECC), "Colours");
  for (const OptionDef& d : t.options) s.push_back(d);
  return s;
}

WidgetList CreateWidgets(const IniDoc& profile) {
  WidgetList list;
  for (const WidgetType& t : WidgetTypes()) {
    const std::vector<OptionDef> schema = FullSchema(t);
    const OptionSet o(profile, t.id, schema);
    if (o.Bool("enabled")) list.push_back(t.create(t, o));
  }
  return list;
}

void FormatGap(wchar_t* buf, size_t n, int ms, int decimals, const wchar_t* prefix) {
  if (decimals >= 3) swprintf_s(buf, n, L"%ls%d.%03d", prefix, ms / 1000, ms % 1000);
  else if (decimals == 2) swprintf_s(buf, n, L"%ls%d.%02d", prefix, ms / 1000, (ms % 1000) / 10);
  else swprintf_s(buf, n, L"%ls%d.%d", prefix, ms / 1000, (ms % 1000) / 100);
}

void FormatTime(wchar_t* buf, size_t n, int ms, int decimals) {
  if (ms <= 0) { swprintf_s(buf, n, L"—"); return; }
  const int div = decimals >= 3 ? 1 : decimals == 2 ? 10 : 100;
  if (ms >= 60000) swprintf_s(buf, n, L"%d:%02d.%0*d", ms / 60000, (ms / 1000) % 60, decimals, (ms % 1000) / div);
  else swprintf_s(buf, n, L"%d.%0*d", ms / 1000, decimals, (ms % 1000) / div);
}

void CarNumber(wchar_t (&out)[8], const VehicleScoringInfoV01& v) {
  out[0] = 0;
  const char* name = v.mVehicleName;
  const char* end = name + strnlen(name, sizeof(v.mVehicleName));
  const char* hash = nullptr;
  for (const char* p = name; p < end; ++p) if (*p == '#') hash = p;
  if (!hash) return;
  int n = 0;
  for (const char* p = hash + 1; p < end && n < 6 && *p >= '0' && *p <= '9'; ++p) out[n++] = static_cast<wchar_t>(*p);
  out[n] = 0;
}

TimeTint TintFor(const Timing* timing, const VehicleScoringInfoV01& v, int k, double time) {
  if (!timing || time <= 0) return kTintNormal;
  if (const Timing::ClassBest* cb = timing->Class(v.mVehicleClass)) {
    const double best = k < 3 ? cb->best.s[k] : cb->best.lap;
    if (best > 0 && time <= best + 1e-4) return kTintClass;
  }
  if (const Timing::Vehicle* vt = timing->Find(v.mID)) {
    const double best = k < 3 ? vt->best.s[k] : vt->best.lap;
    if (best > 0 && time <= best + 1e-4) return kTintPersonal;
  }
  return kTintNormal;
}

namespace {
struct BrandEntry { const char* prefix; const wchar_t* code; unsigned rgb; };
const BrandEntry kBrands[] = {
  {"Ferrari", L"FER", 0xD7261E},   {"Porsche", L"POR", 0xA9A9A9},  {"Toyota", L"TOY", 0xEB0A1E},
  {"Peugeot", L"PEU", 0x4C5E86},   {"Cadillac", L"CAD", 0xB89B5E}, {"BMW", L"BMW", 0x1C69D4},
  {"Alpine", L"ALP", 0x2F6BFF},    {"Lamborghini", L"LAM", 0xC9A21C}, {"Isotta", L"ISO", 0x8B1A1A},
  {"Glickenhaus", L"GLI", 0x6E6E6E}, {"Vanwall", L"VAN", 0x2E7D32}, {"Oreca", L"ORE", 0x2F7DE1},
  {"Ligier", L"LIG", 0x2B4C9B},    {"Ginetta", L"GIN", 0xC0392B},  {"Duqueine", L"DUQ", 0x7F8C8D},
  {"Aston", L"AST", 0x00796B},     {"Corvette", L"COR", 0xD4A900}, {"Chevrolet", L"COR", 0xD4A900},
  {"Ford", L"FOR", 0x1351D8},      {"Lexus", L"LEX", 0x8A96A3},    {"McLaren", L"MCL", 0xFF8000},
  {"Mercedes", L"MER", 0x00A19B},  {"Genesis", L"GEN", 0x9C6B30},  {"Acura", L"ACU", 0xC8102E},
  {"Audi", L"AUD", 0xBB0A30},
};
} // namespace

Brand BrandFor(const Snapshot& s, const VehicleScoringInfoV01& v) {
  Brand b;
  const char* model = s.ModelFor(v.mID);
  if (!model || !model[0]) return b;
  const size_t len = strnlen(model, sizeof(Snapshot::CarModel::name));
  for (const BrandEntry& e : kBrands) {
    const size_t n = strlen(e.prefix);
    if (len >= n && _strnicmp(model, e.prefix, n) == 0) {
      wcscpy_s(b.code, e.code);
      b.color = Col::Rgb(e.rgb);
      return b;
    }
  }
  // Unknown make: first three letters of the model, neutral colour.
  int k = 0;
  for (size_t i = 0; i < len && k < 3; ++i)
    if (isalnum(static_cast<unsigned char>(model[i]))) b.code[k++] = static_cast<wchar_t>(toupper(static_cast<unsigned char>(model[i])));
  b.code[k] = 0;
  b.color = Col::Rgb(0x5A6270);
  return b;
}

void DrawBrand(Painter& p, float x, float y, float w, float h, const Brand& b) {
  if (!b.code[0]) return;
  D2D1_COLOR_F bg = b.color;
  bg.a = 0.85f;
  p.FillRounded(x, y + 3, w, h - 6, 3, bg);
  p.Text(x, y, w, h, b.code, Font::Small, Col::Text, Align::Center);
}

void DrawPlaceholder(Painter& p, float w, float h, const wchar_t* label) {
  p.Panel(w, h);
  p.Text(0, 0, w, h, label, Font::TextBold, Col::Dim, Align::Center);
}

int ClassRank(const char* cls) {
  auto has = [cls](const char* s) { return strstr(cls, s) != nullptr; };
  if (has("Hyper") || has("LMH") || has("LMDh")) return 5;
  if (has("LMP2")) return 4;
  if (has("LMP3")) return 3;
  if (has("GTE")) return 2;
  if (has("GT3")) return 1;
  return 0;
}
