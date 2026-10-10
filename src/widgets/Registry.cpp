#include "widgets/Widget.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
const WidgetType* const kAll[] = {&kStandingsWidget, &kDeltaWidget,  &kTrackInfoWidget, &kSectorsWidget,
                                  &kTrackMapWidget,  &kRelativeWidget, &kClassWarnWidget, &kRadarWidget,
                                  &kInputsWidget,    &kAidsWidget,    &kTyresWidget,     &kStrategyWidget,
                                  &kDamageWidget,    &kLapHistoryWidget, &kFuelWidget,  &kPerfWidget};
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

namespace {
// "Medium" -> 'M', "Soft" -> 'S', "Wet" -> 'W', "Hard" -> 'H'; anything else: its first letter.
wchar_t CompoundLetter(const char (&name)[8]) {
  for (char c : name) {
    if (!c) break;
    if (isalpha(static_cast<unsigned char>(c))) return static_cast<wchar_t>(toupper(static_cast<unsigned char>(c)));
  }
  return 0;
}
D2D1_COLOR_F CompoundColor(wchar_t c) {
  switch (c) {
    case L'S': return Col::Rgb(0xFF5A5A);
    case L'M': return Col::Rgb(0xFFD23F);
    case L'H': return Col::Rgb(0xE8ECF2);
    case L'W': return Col::Rgb(0x4DA3FF);
    case L'I': return Col::Rgb(0x3DDC84);
    default: return Col::Dim;
  }
}
} // namespace

CarState CarStateFor(const Snapshot& s, const VehicleScoringInfoV01& v) {
  CarState c;
  c.fuel = static_cast<int>(std::lround(v.mFuelFraction * 100.0 / 255.0));
  const Snapshot::CarModel* m = s.CarFor(v.mID);
  if (!m) return c;
  const double ve = m->virtualEnergy > 1.5f ? m->virtualEnergy / 100.0 : m->virtualEnergy;
  if (ve > 0.0) c.energy = static_cast<int>(std::lround(std::clamp(ve, 0.0, 1.0) * 100.0));
  c.tyreF = CompoundLetter(m->tyreF);
  c.tyreR = CompoundLetter(m->tyreR);
  if (!v.mIsPlayer && !s.othersWear) return c; // not sent online: would read 100 % for everyone
  double sum = 0;
  int n = 0;
  for (double w : m->wear) if (w > 0.0 && w <= 1.0) { sum += w; ++n; }
  if (n == 4) c.tread = static_cast<int>(std::lround(sum / 4 * 100.0));
  return c;
}

void DrawCarFuel(Painter& p, float x, float y, float w, float h, const CarState& c, float alpha) {
  auto level = [alpha](int pct, D2D1_COLOR_F normal) {
    D2D1_COLOR_F col = pct < 10 ? Col::Bad : pct < 25 ? Col::Warn : normal;
    col.a *= alpha;
    return col;
  };
  if (c.energy >= 0) {
    // Energy on the right (what usually runs out first), fuel left of it.
    p.Textf(x, y, w - 2, h, Font::Small, level(c.energy, Col::Info), Align::Right, L"%d%%", c.energy);
    if (c.fuel >= 0) p.Textf(x, y, w - 34, h, Font::Small, level(c.fuel, Col::Dim), Align::Right, L"%d", c.fuel);
  } else if (c.fuel >= 0) {
    p.Textf(x, y, w - 2, h, Font::Small, level(c.fuel, Col::Dim), Align::Right, L"%d%%", c.fuel);
  }
}

void DrawCarTyres(Painter& p, float x, float y, float w, float h, const CarState& c, float alpha) {
  float tx = x + 4;
  auto letter = [&](wchar_t ch) {
    D2D1_COLOR_F col = CompoundColor(ch);
    col.a *= alpha;
    const wchar_t s[2] = {ch, 0};
    p.Text(tx, y, 12, h, s, Font::TextBold, col, Align::Center);
    tx += 12;
  };
  if (c.tyreF) letter(c.tyreF);
  if (c.tyreR && c.tyreR != c.tyreF) letter(c.tyreR);
  if (c.tread >= 0) {
    D2D1_COLOR_F col = c.tread < 30 ? Col::Bad : c.tread < 60 ? Col::Warn : Col::Dim;
    col.a *= alpha;
    p.Textf(x, y, w - 2, h, Font::Small, col, Align::Right, L"%d%%", c.tread);
  }
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
