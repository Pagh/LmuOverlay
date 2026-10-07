#include "Log.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace {
std::wstring g_path;
std::mutex g_mutex; // Windows event callbacks log from their own thread
}

void LogOpen(const std::wstring& dir) {
  CreateDirectoryW(dir.c_str(), nullptr);
  g_path = dir + L"\\overlay.log";
  WIN32_FILE_ATTRIBUTE_DATA a{};
  if (GetFileAttributesExW(g_path.c_str(), GetFileExInfoStandard, &a) && a.nFileSizeLow > 2u * 1024 * 1024)
    MoveFileExW(g_path.c_str(), (dir + L"\\overlay.old.log").c_str(), MOVEFILE_REPLACE_EXISTING);
}

void Logf(const char* fmt, ...) {
  if (g_path.empty()) return;
  char line[512];
  SYSTEMTIME t;
  GetLocalTime(&t);
  int n = snprintf(line, sizeof(line), "%04d-%02d-%02d %02d:%02d:%02d  ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
                   t.wSecond);
  va_list args;
  va_start(args, fmt);
  n += vsnprintf(line + n, sizeof(line) - n - 2, fmt, args);
  va_end(args);
  if (n > static_cast<int>(sizeof(line)) - 2) n = sizeof(line) - 2;
  line[n++] = '\n';
  std::lock_guard lock(g_mutex);
  FILE* f = nullptr;
  if (_wfopen_s(&f, g_path.c_str(), L"ab") == 0 && f) {
    fwrite(line, 1, n, f);
    fclose(f);
  }
}
