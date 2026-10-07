#pragma once
#include <string>

// Small text log (bin\logs\overlay.log): sessions, profile / reference switches and a once-a-minute
// performance line. A line at a time (thread-safe), so it costs nothing.
// The file is rotated to overlay.old.log when it passes 2 MB.
void LogOpen(const std::wstring& dir);
void Logf(const char* fmt, ...);
