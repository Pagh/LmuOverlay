cmake_minimum_required(VERSION 3.20)
project(LmuOverlay LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The Studio 397 SDK headers ship with the game and may not be redistributed,
# so we include them straight from the LMU install instead of copying them.
set(LMU_SDK_DIR "D:/SteamLibrary/steamapps/common/Le Mans Ultimate/Support/SharedMemoryInterface"
    CACHE PATH "Folder containing SharedMemoryInterface.hpp from the LMU install")

if(NOT EXISTS "${LMU_SDK_DIR}/SharedMemoryInterface.hpp")
  message(FATAL_ERROR "SharedMemoryInterface.hpp not found in LMU_SDK_DIR='${LMU_SDK_DIR}'. "
                      "Pass -DLMU_SDK_DIR=<LMU>/Support/SharedMemoryInterface")
endif()

# Dear ImGui (MIT) for the settings window, pinned to a release.
include(FetchContent)
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.92.9b
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui)
add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
if(MSVC)
  set_property(TARGET imgui PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_executable(LmuOverlay WIN32
  src/main.cpp
  src/App.cpp
  src/Ini.cpp
  src/Options.cpp
  src/Settings.cpp
  src/Timing.cpp
  src/LapTrace.cpp
  src/TrackMap.cpp
  src/WheelButtons.cpp
  res/app.rc
  src/Log.cpp
  src/GameWatch.cpp
  src/Updater.cpp
  src/Json.cpp
  src/LmuRest.cpp
  src/SettingsWindow.cpp
  src/SharedMemoryReader.cpp
  src/DemoSource.cpp
  src/Model.cpp
  src/Overlay.cpp
  src/Painter.cpp
  src/widgets/Registry.cpp
  src/widgets/RelativeWidget.cpp
  src/widgets/StandingsWidget.cpp
  src/widgets/DeltaWidget.cpp
  src/widgets/SectorsWidget.cpp
  src/widgets/DamageWidget.cpp
  src/widgets/FuelWidget.cpp
  src/widgets/InputsWidget.cpp
  src/widgets/TyresWidget.cpp
  src/widgets/PerfWidget.cpp
  src/widgets/TrackInfoWidget.cpp
  src/widgets/TrackMapWidget.cpp
  src/widgets/RadarWidget.cpp
  src/widgets/ClassWarnWidget.cpp
  src/widgets/StrategyWidget.cpp
)

set_target_properties(LmuOverlay PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
target_include_directories(LmuOverlay PRIVATE src "${LMU_SDK_DIR}")
target_compile_definitions(LmuOverlay PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(LmuOverlay PRIVATE imgui d3d11 dxgi d2d1 dwrite dcomp dwmapi winhttp dinput8 dxguid wevtapi)

# Console diagnostic: prints the raw shared-memory values (run while LMU is running).
add_executable(LmuProbe tools/LmuProbe.cpp)
set_target_properties(LmuProbe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
target_include_directories(LmuProbe PRIVATE src "${LMU_SDK_DIR}")
target_compile_definitions(LmuProbe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
  target_compile_options(LmuProbe PRIVATE /utf-8)
  set_property(TARGET LmuProbe PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(MSVC)
  target_compile_options(LmuOverlay PRIVATE /W4 /permissive- /utf-8 /wd4100)
  # Static CRT: single self-contained exe, no VC++ redistributable needed.
  set_property(TARGET LmuOverlay PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Manual check of the updater (tools/UpdaterTest.cpp).
add_executable(LmuUpdaterTest tools/UpdaterTest.cpp src/Updater.cpp src/Json.cpp src/Ini.cpp src/Log.cpp)
set_target_properties(LmuUpdaterTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tools")
target_include_directories(LmuUpdaterTest PRIVATE src)
target_compile_definitions(LmuUpdaterTest PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(LmuUpdaterTest PRIVATE winhttp)
if(MSVC)
  target_compile_options(LmuUpdaterTest PRIVATE /utf-8)
  set_property(TARGET LmuUpdaterTest PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
