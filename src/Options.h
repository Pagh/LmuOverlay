#pragma once
#include "Ini.h"
#include <d2d1.h>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Declarative widget options. Each widget lists its options once; the settings
// window builds its controls from this list and the widget reads the values
// through OptionSet, so adding an option is a one-line change.
enum class OptType { Bool, Int, Float, Color, Choice };

struct OptionDef {
  const char* key;
  const char* label;
  OptType type;
  double def = 0;          // Bool/Int/Float; Color as 0xRRGGBBAA; Choice = default index
  double min = 0, max = 0; // Int/Float range
  const char* choices = nullptr; // Choice: "a|b|c"
  const char* help = nullptr;
  const char* group = "Options";
};

constexpr OptionDef OptBool(const char* key, const char* label, bool def, const char* help = nullptr) {
  return {key, label, OptType::Bool, def ? 1.0 : 0.0, 0, 1, nullptr, help};
}
constexpr OptionDef OptInt(const char* key, const char* label, int def, int min, int max, const char* help = nullptr) {
  return {key, label, OptType::Int, double(def), double(min), double(max), nullptr, help};
}
constexpr OptionDef OptFloat(const char* key, const char* label, double def, double min, double max, const char* help = nullptr) {
  return {key, label, OptType::Float, def, min, max, nullptr, help};
}
constexpr OptionDef OptColor(const char* key, const char* label, uint32_t rgba, const char* help = nullptr) {
  return {key, label, OptType::Color, double(rgba), 0, 0, nullptr, help, "Colours"};
}
constexpr OptionDef OptChoice(const char* key, const char* label, const char* choices, int def, const char* help = nullptr) {
  return {key, label, OptType::Choice, double(def), 0, 0, choices, help};
}

// Colour helpers ("#RRGGBB" or "#RRGGBBAA" in the INI).
uint32_t ParseColor(const std::string& s, uint32_t def);
std::string FormatColor(uint32_t rgba);
D2D1_COLOR_F ToD2D(uint32_t rgba);
std::vector<std::string> SplitChoices(const char* choices);

// Read-only view of one widget's section, falling back to schema defaults.
class OptionSet {
public:
  OptionSet(const IniDoc& doc, std::string section, std::span<const OptionDef> schema)
      : doc_(doc), section_(std::move(section)), schema_(schema) {}

  bool Bool(const char* key) const;
  int Int(const char* key) const;
  float Float(const char* key) const;
  D2D1_COLOR_F Color(const char* key) const;
  int Choice(const char* key) const; // index into the choices list

  const std::string& Section() const { return section_; }

private:
  const OptionDef& Def(const char* key) const;

  const IniDoc& doc_;
  std::string section_;
  std::span<const OptionDef> schema_;
};

// Value of an option as stored in the doc (or its default), for the settings UI.
double GetOptionValue(const IniDoc& doc, const std::string& section, const OptionDef& d);
void SetOptionValue(IniDoc& doc, const std::string& section, const OptionDef& d, double value);
