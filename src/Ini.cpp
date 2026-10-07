#include "Ini.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>

namespace {

std::string_view Trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
  return s;
}

} // namespace

std::wstring Widen(std::string_view s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

std::string Narrow(std::wstring_view w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

bool IniDoc::Load(const std::wstring& path) {
  sections_.clear();
  FILE* f = nullptr;
  if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return false;
  std::string data;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
  fclose(f);
  if (data.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xEF) data.erase(0, 3); // UTF-8 BOM

  Section* cur = nullptr;
  size_t pos = 0;
  while (pos <= data.size()) {
    size_t end = data.find('\n', pos);
    if (end == std::string::npos) end = data.size();
    const std::string_view line = Trim(std::string_view(data).substr(pos, end - pos));
    pos = end + 1;
    if (line.empty() || line[0] == ';' || line[0] == '#') continue;
    if (line.front() == '[' && line.back() == ']') {
      cur = &GetOrAddSection(Trim(line.substr(1, line.size() - 2)));
      continue;
    }
    const size_t eq = line.find('=');
    if (eq == std::string_view::npos || !cur) continue;
    const std::string_view key = Trim(line.substr(0, eq)), value = Trim(line.substr(eq + 1));
    cur->values.emplace_back(std::string(key), std::string(value));
  }
  return true;
}

bool IniDoc::Save(const std::wstring& path) const {
  std::string out;
  if (!header.empty()) {
    size_t pos = 0;
    while (pos < header.size()) {
      size_t end = header.find('\n', pos);
      if (end == std::string::npos) end = header.size();
      out += "; ";
      out.append(header, pos, end - pos);
      out += "\r\n";
      pos = end + 1;
    }
    out += "\r\n";
  }
  for (const Section& s : sections_) {
    out += "[" + s.name + "]\r\n";
    for (const auto& [k, v] : s.values) out += k + "=" + v + "\r\n";
    out += "\r\n";
  }

  const std::wstring tmp = path + L".tmp";
  FILE* f = nullptr;
  if (_wfopen_s(&f, tmp.c_str(), L"wb") != 0 || !f) return false;
  const bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
  fclose(f);
  return ok && MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

const IniDoc::Section* IniDoc::FindSection(std::string_view name) const {
  for (const Section& s : sections_)
    if (_strnicmp(s.name.c_str(), name.data(), name.size()) == 0 && s.name.size() == name.size()) return &s;
  return nullptr;
}

IniDoc::Section& IniDoc::GetOrAddSection(std::string_view name) {
  if (const Section* s = FindSection(name)) return const_cast<Section&>(*s);
  sections_.push_back(Section{std::string(name), {}});
  return sections_.back();
}

const std::string* IniDoc::Find(std::string_view section, std::string_view key) const {
  const Section* s = FindSection(section);
  if (!s) return nullptr;
  for (const auto& [k, v] : s->values)
    if (k.size() == key.size() && _strnicmp(k.c_str(), key.data(), key.size()) == 0) return &v;
  return nullptr;
}

std::string IniDoc::Get(std::string_view section, std::string_view key, std::string_view def) const {
  const std::string* v = Find(section, key);
  return v ? *v : std::string(def);
}

int IniDoc::GetInt(std::string_view section, std::string_view key, int def) const {
  const std::string* v = Find(section, key);
  if (!v || v->empty()) return def;
  char* end = nullptr;
  const long long r = std::strtoll(v->c_str(), &end, 0);
  return end != v->c_str() ? static_cast<int>(r) : def;
}

double IniDoc::GetFloat(std::string_view section, std::string_view key, double def) const {
  const std::string* v = Find(section, key);
  if (!v || v->empty()) return def;
  char* end = nullptr;
  const double r = std::strtod(v->c_str(), &end); // "C" locale: always '.' as decimal separator
  return end != v->c_str() ? r : def;
}

void IniDoc::Set(std::string_view section, std::string_view key, std::string value) {
  Section& s = GetOrAddSection(section);
  for (auto& [k, v] : s.values)
    if (k.size() == key.size() && _strnicmp(k.c_str(), key.data(), key.size()) == 0) { v = std::move(value); return; }
  s.values.emplace_back(std::string(key), std::move(value));
}

void IniDoc::SetFloat(std::string_view section, std::string_view key, double v) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%.4g", v);
  Set(section, key, buf);
}
