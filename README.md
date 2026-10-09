# LMU Overlay

A low-overhead HUD overlay for Le Mans Ultimate, built to avoid the microstutters
caused by heavier overlay tools.

## Why it shouldn't stutter

| Common stutter source | What this overlay does instead |
|---|---|
| A plugin DLL inside the game (`Plugins/*.dll`) doing work on LMU's threads | **Nothing is loaded into LMU.** Reads LMU's own `LMU_Data` shared memory from a separate process (read-only mapping). |
| Holding the game's shared-memory lock too long, or being preempted while holding it | The lock is held only for a few `memcpy`s (player telemetry + scoring, not all 104 cars' telemetry). The reader thread is high priority but sleeps between copies, so it can't be preempted mid-copy. Copies are rate-limited (`poll_hz`) instead of on every game frame. |
| Many translucent windows (one per widget) repainting at 50–100 Hz | **One** click-through window with no redirection bitmap. Each widget is a DirectComposition visual with a small GPU surface, redrawn **only when its displayed values change**. |
| Garbage-collection / interpreter pauses | C++ with no allocation in the per-frame path. |
| Competing with the game for CPU | The process runs at below-normal priority. Optional `affinity_mask` to keep it off the cores you choose. |

Measured in `--demo` on a Ryzen 5 9600X with all widgets enabled: about 3% of one core.
Enable the `perf` widget to see draw time and how long LMU's lock was held.

## Install (no build needed)

1. Download `LmuOverlay.exe` from the [latest release](https://github.com/Pagh/LmuOverlay/releases/latest).
2. Put it in its own folder (e.g. `Documents\LmuOverlay`): it writes its settings, profiles, lap records and logs next to itself.
3. Run it. Windows SmartScreen may warn because the exe isn't signed: *More info* > *Run anyway*.

Updates: the overlay checks GitHub when it starts and shows a tray notification when a new version is out.
Install it from Settings > General > Updates > **Update now**: the exe is replaced and restarted, your settings,
profiles and records are kept.

**[Guida in italiano: come funziona ogni widget e come personalizzarlo](docs/GUIDA.md)**

Publishing a new version (maintainer): `powershell -ExecutionPolicy Bypass -File tools\release.ps1 -Version 1.0.1 -Notes "What changed"`.

## Build

Requires Visual Studio 2022 with the "Desktop development with C++" workload (MSVC + Windows SDK + CMake/Ninja).
The first build downloads Dear ImGui v1.92.9b (MIT) with CMake FetchContent.

```
build.bat
```

Produces `bin\LmuOverlay.exe`. The LMU SDK headers are read directly from the game install
(they may not be redistributed). If LMU is installed somewhere else:

```
build.bat -DLMU_SDK_DIR="X:/path/Le Mans Ultimate/Support/SharedMemoryInterface"
```

## Run

- `bin\LmuOverlay.exe` reads the live game. Start it before or after LMU. Starting it again while it's running opens its settings window.
- `bin\LmuOverlay.exe --demo` runs a synthetic race, for layout and testing without the game.

| Hotkey | Action |
|---|---|
| Ctrl+Alt+E | Move widgets (drag them; positions are saved) |
| Ctrl+Alt+O | Show / hide overlay |
| Ctrl+Alt+D | Switch the delta reference: your best (LMU) → session best → all-time best → lobby fastest lap → last lap |
| Ctrl+Alt+P | Next profile (until the session type changes) |
| Ctrl+Alt+Q | Quit |

Wheel / button box buttons can do the same (next / previous delta reference, next profile, show / hide):
Settings > General > Wheel buttons > Set, then press the button. They're read through DirectInput,
non-exclusive and in the background, so LMU keeps full control of the wheel.

Tray icon: double-click opens **Settings**, right-click opens the menu (Settings, Move widgets, Show overlay, Reload settings from disk, Quit).

## Settings window

- **Widgets**: tick a widget to show it, click its name to configure it. The options
  (position, scale, update rate, colours and widget-specific settings) are generated from
  each widget's option list. Changes apply live and are saved automatically.
  "Copy to all profiles" copies a widget's look (not its position) to every profile.
- **Profiles**: create, duplicate, rename and delete profiles, and choose which profile is
  used for Practice / Qualifying / Race (or always the same one). While the settings window
  is open, the overlay shows the profile you're editing; when you close it, the session's
  profile is used again.
- **General**: monitor, global scale, "only while driving", data rate, redraw cap,
  process priority, CPU threads, startup behaviour.

**Preview** (on by default) keeps the overlay visible while the window is open, using demo
data when you're not on track. The window and its GPU resources only exist while it's open.

## Monitors

The overlay covers one monitor. By default (**Automatic**) it follows the monitor LMU's window
is on and remembers it while the game is closed. Widget positions are relative to that
monitor's top-left corner and are always clamped onto it, so a widget can't be lost off-screen.

## Files (next to the exe)

- `settings.ini`: general settings and the session-to-profile mapping.
- `profiles\<name>.ini`: one section per widget with every option written out.
- A v1 `LmuOverlay.ini` is migrated automatically on first start (kept as `LmuOverlay.ini.old`).

## Widgets

| Widget | Shows |
|---|---|
| standings | Session info on top: session, time left, lap / estimated race laps, race length, pit stop required or not, track / air temperature, rain. Before a race starts: race length in laps and what to fill up with. Then your class: leaders + cars around you, make, best / last lap, fuel + virtual energy left, tyre compound + tread, gap (race: time behind on track; practice / qualifying: best-lap difference) |
| delta | Live delta (number + bar) against a selectable reference: your best (LMU's own delta), session best, all-time best (any car of your class), the lobby's fastest lap in your class, or your last valid lap (with the spread of your last 5 laps, for consistency) |
| trackinfo | Small bar under the delta, only when something matters: stopped / slow car ahead with the distance in metres, local yellow, full-course yellow, blue flag, race phase, track-limit points, penalties, lap valid / invalid (practice, qualifying) |
| sectors | Compact (default): three boxes with this lap's sector gaps to the reference + last / best / ideal lap. Table: this lap, last lap, session best, all-time best (saved in `records\`), lobby best |
| trackmap | Circuit outline (learned from all cars' positions, saved in `records\<track>.map.ini`), every car in its class colour, yellow sectors highlighted |
| relative | Cars nearest on track: make, last lap, best lap, last-lap sectors, gap, and how the gap changed over your last lap; optional fuel / energy and tyres columns |
| classwarn | Red pill when a faster class is closing in behind you ("HYPERCAR 1.8 s behind") |
| radar | Side bars while a car is alongside (CAR LEFT / RIGHT / 3-WIDE) |
| inputs | RPM / shift light, gear, speed, TC / cut / slip / ABS / map, brake bias, water / oil (or battery), throttle / brake trace of the last 12 s with lock-ups and wheel spin marked |
| tyres | Inner-layer (or surface / carcass) temperature with L/C/R strip, coloured against the compound's optimal temperature, pressure, tread left, brake temperature; optional READY TO PUSH badge |
| strategy | Races: pit stop required → fuel / energy, laps to the end, pit window, what to add, stop time; otherwise a one-line fuel check. Practice / qualifying: plan for the race set in Settings > General > Race plan (time or laps) |
| damage | After a hit: car silhouette with the hit zones, aero and suspension damage %, repair time, lap time lost; for a few seconds after any contact ("CONTACT · NO DAMAGE"), with the session's contact count |
| fuel | Detailed fuel / energy table (off by default; Strategy covers it) |
| perf | The overlay's own cost (off by default) |

Timing colours everywhere: **purple** = fastest in your class this session, **green** = that driver's own best, yellow = slower (sectors widget).

### Damage data

Body dents, detached parts, flat / detached wheels and impacts come from shared memory. Aero damage,
suspension damage and the repair time are only exposed by LMU's local REST API (`localhost:6397`,
the same one TinyPedal uses): the overlay asks on a background thread right after an impact and every
few seconds while driving (General > LMU REST API). "Pace" compares your average clean lap before the
first damage with your laps since; it resets when the car is repaired.

### Race length and pit stops

LMU doesn't tell plugins whether a pit stop is mandatory, so the overlay works it out: estimated race
laps (time left divided by your pace; when the clock runs out the overall leader finishes the lap) times
fuel / energy per lap, against a full tank / 100 % energy. If one tank can't do it, a stop is required
and Strategy shows the plan; otherwise it's just a fuel check. Pace: your race laps, else your best this
session, your all-time best here, LMU's estimate. Fuel / energy per lap: measured on clean laps and saved
per track and car (`records\`), else LMU's own garage estimate. The all-time pace is per class.

### Delta references

LMU's own delta only compares against your best lap. For the other references the overlay records
"lap traces" (elapsed time every 5 m): your laps from telemetry at ~60 Hz, other cars' laps from scoring
data (LMU updates it about 6 times per second, so a lobby reference is accurate to a few hundredths).
Your all-time best (lap, sectors and trace) is shared by every car of a class on a track
(`records\<track> - class <class>.ini`); fuel / energy use stays per car (`records\<track> - <car>.ini`).
`tools\import_results.py` seeds the class bests from LMU's own results files (`UserData\Log\Results`);
A lobby reference only exists for laps driven while the overlay was running.

## Code layout

```
src/
  main.cpp                 entry point, single instance
  App.*                    main loop, profiles + session switching, monitor following, tray, hotkeys
  SettingsWindow.*         the control panel (Dear ImGui, only alive while open)
  Settings.*               settings.ini, profile files, session kinds, monitor list
  Ini.*, Options.*         INI document + declarative widget options (schema -> UI + values)
  SharedMemoryReader.*     LMU_Data reader thread (the only code that touches the game's lock)
  DemoSource.*             synthetic data for --demo and the settings preview
  Snapshot.h               copied data + lock-free triple buffer
  Model.*                  derived values (player, race length, fuel / energy planning, pit stop required)
  Overlay.*                overlay window, D3D11 / Direct2D / DirectComposition, edit mode
  Painter.*                drawing helpers, fonts, colours
  Timing.*                 sector / lap bookkeeping for the field, all-time records, fuel use, pace before/after damage
  TrackMap.*               track outline + sector boundaries learned from car positions
  LmuRest.*, Json.*        background client for LMU's REST API (damage details)
  widgets/                 one file per widget (options + drawing); Registry.cpp lists them
tools/LmuProbe.cpp         console diagnostic
```

Adding a widget: create `widgets/FooWidget.cpp` with its `kOptions` list and a `WidgetType`,
then add it to `Registry.cpp` and `CMakeLists.txt`. The settings window picks it up automatically.

## Moving widgets

Press **Ctrl+Alt+E** (or "Move widgets" in the settings window / tray menu). The screen dims, every enabled widget
is shown with a yellow outline even if the game isn't running, and you can drag them with
the mouse. Positions snap to a 4 px grid and are saved to the ini as you drop them. Press
Ctrl+Alt+E again to finish. In-game, open a menu or pause first so LMU releases the mouse cursor.

## Diagnostics

`bin\logs\overlay.log`: sessions, profile / reference switches, one line per lap with what the
strategy logic thinks (fuel per lap, laps to go, stop required...) and once a minute the overlay's own
cost (draw time, how long LMU's lock was held, redraws, CPU). Send it along when something looks wrong.

`bin\LmuProbe.exe` (console) connects to LMU like the overlay does and prints the raw values
(session, flags, track limits, player, fuel, virtual energy, aids, tyres). Run it while LMU is running.
`LmuProbe watch` (8 s, while driving through corners) checks the car's axis convention and sector
boundaries; `LmuProbe map` (15 s) checks the world's handedness from the track outline.

## Verified / still to verify in-game

- Verified: the `LMU_Data` connection (about 100 frames signalled per second); `mBrakeTemp` is Kelvin; `mInRealtime` is 0 in the garage.
- Verified: car frame is x = left, y = up, z = back (left-handed world; maps draw x right, z up); `mSectorFlag` is 11 = green,
  1 = yellow, index i = sector i+1; before a race starts `mEndET` is invalid and `mSessionTimeRemaining` holds the countdown;
  track limits = `mTrackLimitsSteps` / `mTrackLimitsStepsPerPoint` (penalty at `StepsPerPenalty`).
- To verify on track: `mVirtualEnergy` units (handled as a fraction 0–1 or as a percentage).
