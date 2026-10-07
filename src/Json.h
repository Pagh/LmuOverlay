#pragma once
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Tiny JSON DOM, just enough for LMU's REST responses (a few KB, parsed a few
// times per minute on a background thread).
struct Json {
  enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
  double number = 0;
  bool boolean = false;
  std::string string;
  std::vector<Json> array;
  std::map<std::string, Json, std::less<>> object;

  static bool Parse(std::string_view text, Json& out);

  const Json* Get(std::string_view key) const; // object member or nullptr
  const Json* Path(std::initializer_list<std::string_view> keys) const;
  double Num(double def = 0) const { return type == Type::Number ? number : def; }
};
