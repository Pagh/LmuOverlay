#pragma once
// Single entry point for the Studio 397 shared-memory SDK headers, which are
// read from the LMU install (see LMU_SDK_DIR in CMakeLists.txt).

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <utility>
#include <optional>

#pragma warning(push)
#pragma warning(disable : 4201 4245 4505 4996) // nameless unions, sign conversions, unused static fn
#include "SharedMemoryInterface.hpp"
#pragma warning(pop)

constexpr int kMaxVehicles = 104; // matches the fixed arrays in SharedMemoryScoringData / TelemetryData
