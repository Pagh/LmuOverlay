#pragma once
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Minimal in-memory INI document (UTF-8). Keeps section/key order so saved
// files stay readable. Only used when loading/saving settings, never per frame.
class IniDoc {
public:
  struct Section {
    std::string name;
    std::vector<std::pair<std::string, std::string>> values;
  };

  bool Load(const std::wstring& path);
  bool Save(const std::wstring& path) const; // atomic: write temp file, then replace

  const std::string* Find(std::string_view section, std::string_view key) const;
  std::string Get(std::string_view section, std::string_view key, std::string_view def = {}) const;
  int GetInt(std::string_view section, std::string_view key, int def) const;
  double GetFloat(std::string_view section, std::string_view key, double def) const;
  bool GetBool(std::string_view section, std::string_view key, bool def) const { return GetInt(section, key, def) != 0; }

  void Set(std::string_view section, std::string_view key, std::string value);
  void SetInt(std::string_view section, std::string_view key, int v) { Set(section, key, std::to_string(v)); }
  void SetFloat(std::string_view section, std::string_view key, double v);
  void SetBool(std::string_view section, std::string_view key, bool v) { SetInt(section, key, v ? 1 : 0); }

  bool HasSection(std::string_view section) const { return FindSection(section) != nullptr; }
  const std::vector<Section>& Sections() const { return sections_; }

  std::string header; // comment block written at the top of the file (without ';')

private:
  const Section* FindSection(std::string_view name) const;
  Section& GetOrAddSection(std::string_view name);

  std::vector<Section> sections_;
};

// UTF-8 <-> UTF-16 helpers.
std::wstring Widen(std::string_view s);
std::string Narrow(std::wstring_view s);
