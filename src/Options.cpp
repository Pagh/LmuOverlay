#include "Options.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

uint32_t ParseColor(const std::string& s, uint32_t def) {
  if (s.size() < 7 || s[0] != '#') return def;
  char* end = nullptr;
  const unsigned long v = std::strtoul(s.c_str() + 1, &end, 16);
  const size_t digits = static_cast<size_t>(end - (s.c_str() + 1));
  if (digits == 6) return (static_cast<uint32_t>(v) << 8) | 0xFF;
  if (digits == 8) return static_cast<uint32_t>(v);
  return def;
}

std::string FormatColor(uint32_t rgba) {
  char buf[16];
  if ((rgba & 0xFF) == 0xFF) snprintf(buf, sizeof(buf), "#%06X", rgba >> 8);
  else snprintf(buf, sizeof(buf), "#%08X", rgba);
  return buf;
}

D2D1_COLOR_F ToD2D(uint32_t c) {
  return {((c >> 24) & 0xFF) / 255.f, ((c >> 16) & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, (c & 0xFF) / 255.f};
}

std::vector<std::string> SplitChoices(const char* choices) {
  std::vector<std::string> out;
  if (!choices) return out;
  const char* p = choices;
  while (*p) {
    const char* bar = strchr(p, '|');
    out.emplace_back(p, bar ? bar - p : strlen(p));
    if (!bar) break;
    p = bar + 1;
  }
  return out;
}

double GetOptionValue(const IniDoc& doc, const std::string& section, const OptionDef& d) {
  const std::string* raw = doc.Find(section, d.key);
  if (!raw) return d.def;
  switch (d.type) {
    case OptType::Bool:
    case OptType::Int: return std::clamp<double>(doc.GetInt(section, d.key, int(d.def)), d.min, d.max);
    case OptType::Float: return std::clamp(doc.GetFloat(section, d.key, d.def), d.min, d.max);
    case OptType::Color: return ParseColor(*raw, static_cast<uint32_t>(d.def));
    case OptType::Choice: {
      const auto list = SplitChoices(d.choices);
      for (size_t i = 0; i < list.size(); ++i)
        if (_stricmp(list[i].c_str(), raw->c_str()) == 0) return double(i);
      return d.def;
    }
  }
  return d.def;
}

void SetOptionValue(IniDoc& doc, const std::string& section, const OptionDef& d, double v) {
  switch (d.type) {
    case OptType::Bool: doc.SetBool(section, d.key, v != 0); break;
    case OptType::Int: doc.SetInt(section, d.key, static_cast<int>(v)); break;
    case OptType::Float: doc.SetFloat(section, d.key, v); break;
    case OptType::Color: doc.Set(section, d.key, FormatColor(static_cast<uint32_t>(v))); break;
    case OptType::Choice: {
      const auto list = SplitChoices(d.choices);
      const size_t i = static_cast<size_t>(v);
      if (i < list.size()) doc.Set(section, d.key, list[i]);
      break;
    }
  }
}

const OptionDef& OptionSet::Def(const char* key) const {
  for (const OptionDef& d : schema_)
    if (strcmp(d.key, key) == 0) return d;
  static const OptionDef kMissing{"", "", OptType::Int};
  return kMissing; // programming error: key not in the widget's schema
}

bool OptionSet::Bool(const char* key) const { return GetOptionValue(doc_, section_, Def(key)) != 0; }
int OptionSet::Int(const char* key) const { return static_cast<int>(GetOptionValue(doc_, section_, Def(key))); }
float OptionSet::Float(const char* key) const { return static_cast<float>(GetOptionValue(doc_, section_, Def(key))); }
D2D1_COLOR_F OptionSet::Color(const char* key) const {
  return ToD2D(static_cast<uint32_t>(GetOptionValue(doc_, section_, Def(key))));
}
int OptionSet::Choice(const char* key) const { return static_cast<int>(GetOptionValue(doc_, section_, Def(key))); }
