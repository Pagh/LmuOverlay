#include "Json.h"
#include <algorithm>
#include <cstdlib>

namespace {

struct Parser {
  std::string_view s;
  size_t i = 0;
  int depth = 0;

  void Ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i; }
  bool Lit(std::string_view w) {
    if (s.substr(i, w.size()) != w) return false;
    i += w.size();
    return true;
  }

  bool String(std::string& out) {
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    while (i < s.size() && s[i] != '"') {
      char c = s[i++];
      if (c == '\\' && i < s.size()) {
        const char e = s[i++];
        switch (e) {
          case 'n': c = '\n'; break;
          case 't': c = '\t'; break;
          case 'r': c = '\r'; break;
          case 'b': c = '\b'; break;
          case 'f': c = '\f'; break;
          case 'u': // keep it simple: BMP code point to UTF-8
            if (i + 4 <= s.size()) {
              const unsigned cp = static_cast<unsigned>(std::strtoul(std::string(s.substr(i, 4)).c_str(), nullptr, 16));
              i += 4;
              if (cp < 0x80) out += static_cast<char>(cp);
              else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
              else { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
            }
            continue;
          default: c = e; break;
        }
      }
      out += c;
    }
    if (i >= s.size()) return false;
    ++i;
    return true;
  }

  bool Value(Json& v) {
    if (++depth > 64) return false;
    Ws();
    if (i >= s.size()) return false;
    bool ok = true;
    const char c = s[i];
    if (c == '{') {
      v.type = Json::Type::Object;
      ++i;
      Ws();
      if (i < s.size() && s[i] == '}') { ++i; }
      else {
        for (;;) {
          Ws();
          std::string key;
          if (!String(key)) { ok = false; break; }
          Ws();
          if (i >= s.size() || s[i] != ':') { ok = false; break; }
          ++i;
          if (!Value(v.object[key])) { ok = false; break; }
          Ws();
          if (i < s.size() && s[i] == ',') { ++i; continue; }
          if (i < s.size() && s[i] == '}') { ++i; break; }
          ok = false;
          break;
        }
      }
    } else if (c == '[') {
      v.type = Json::Type::Array;
      ++i;
      Ws();
      if (i < s.size() && s[i] == ']') { ++i; }
      else {
        for (;;) {
          v.array.emplace_back();
          if (!Value(v.array.back())) { ok = false; break; }
          Ws();
          if (i < s.size() && s[i] == ',') { ++i; continue; }
          if (i < s.size() && s[i] == ']') { ++i; break; }
          ok = false;
          break;
        }
      }
    } else if (c == '"') {
      v.type = Json::Type::String;
      ok = String(v.string);
    } else if (Lit("true")) { v.type = Json::Type::Bool; v.boolean = true; }
    else if (Lit("false")) { v.type = Json::Type::Bool; v.boolean = false; }
    else if (Lit("null")) { v.type = Json::Type::Null; }
    else {
      const std::string num(s.substr(i, std::min<size_t>(64, s.size() - i)));
      char* end = nullptr;
      v.number = std::strtod(num.c_str(), &end);
      if (end == num.c_str()) ok = false;
      else { v.type = Json::Type::Number; i += static_cast<size_t>(end - num.c_str()); }
    }
    --depth;
    return ok;
  }
};

} // namespace

bool Json::Parse(std::string_view text, Json& out) {
  Parser p{text};
  out = Json{};
  return p.Value(out);
}

const Json* Json::Get(std::string_view key) const {
  if (type != Type::Object) return nullptr;
  const auto it = object.find(key);
  return it == object.end() ? nullptr : &it->second;
}

const Json* Json::Path(std::initializer_list<std::string_view> keys) const {
  const Json* j = this;
  for (std::string_view k : keys) {
    j = j->Get(k);
    if (!j) return nullptr;
  }
  return j;
}
