<div align="center">
  <img src="logo.svg" alt="osu!Point Logo" width="128" />

  # osu!Point
  
  A modern, ultra-low latency tablet driver for osu!, digital art, and everyday pen input.

  [![release](https://img.shields.io/github/v/release/Certual/osuPoint?style=flat-square)](https://github.com/Certual/osuPoint/releases/latest)
  [![build](https://img.shields.io/github/actions/workflow/status/Certual/osuPoint/build.yml?style=flat-square&label=build)](https://github.com/Certual/osuPoint/actions)
  [![platform](https://img.shields.io/badge/platform-Windows-0078D6?style=flat-square&logo=windows&logoColor=white)](https://github.com/Certual/osuPoint)
  [![license](https://img.shields.io/github/license/Certual/osuPoint?style=flat-square)](https://github.com/Certual/osuPoint/blob/main/LICENSE)
  [![c++](https://img.shields.io/badge/C%2B%2B-20-00599C?style=flat-square&logo=c%2B%2B)](https://github.com/Certual/osuPoint)
</div>

## About

**osu!Point** is designed to bypass the traditional Windows input pipeline, directly parsing raw HID reports and injecting absolute coordinates to achieve the lowest possible input latency.

## Performance & Architecture

In side-by-side testing against OpenTabletDriver, builds show roughly a 50% reduction in end-to-end input delay.

This was measured on a 240 Hz monitor recorded with a smartphone camera in 480 FPS slow-motion, calculating the frame delta between physical pen movement and the first visible cursor movement on screen.

Low latency is achieved through:
- Direct syscall dispatch via `NtUserSendInput` (`win32u.dll`) with `MOUSEEVENTF_MOVE_NOCOALESCE`, bypassing user-mode `user32.dll` wrappers, Windows Ink, and virtual driver queues (`VMulti`).
- Pipelined double-buffered `OVERLAPPED` USB transfers (`FILE_SKIP_SET_EVENT_ON_HANDLE`) that rearm `ReadFile` on the host controller immediately before user-space packet processing begins.
- A 64-byte cache-line-aligned lock-free SPSC ring buffer (`SpscRing<RawPacket, 64>`) and seqlock configuration store (`AtomicConfigStore`) with zero heap allocations on the hot path.
- SIMD-accelerated (`AVX2` + `FMA` / `SSE2`) coordinate transformation and boundary clipping.
- Configurable **Latency Spin** (`Eco 0%` to `100%`), allowing users to choose between event-driven `< 0.2%` CPU usage or continuous L1 cache polling.
- Optional **Adaptive Kinematic Extrapolation** (`1.0`–`5.0 ms` lookahead `tau`) with turnaround and braking overshoot suppression.
- Sub-millisecond system timer resolution (`0.5 ms` via `NtSetTimerResolution`), disabled thread power throttling, MMCSS `"Pro Audio"` scheduling, and automatic P-core thread affinity pinning on hybrid CPUs.

## Hardware Support

The driver includes built-in specifications for over 160 tablet models across Wacom, XP-Pen, Huion, Gaomon, VEIKK, UC-Logic, Artisul, and Ugee:
- Automatic 16-bit, 17-bit, 18-bit, and 24-bit coordinate decoding.
- Automatic Huion / Gaomon / XP-Pen binary String Descriptor `200` interrogation to read exact hardware `max_x`, `max_y`, and `LPI` on shared-PID controllers.
- Multi-endpoint monitoring (up to 4 concurrent interfaces) with automatic vendor wake-up handshakes (`SetFeature`, `SetOutputReport`, `WriteFile`) and conflict detection.

Devices physically verified by the author and community:
- Wacom One (`CTL-472`)
- Wacom Bamboo (`CTL-471`)
- Wacom Intuos S / M (`CTL-4100` / `CTL-6100`, including WL/Bluetooth variants)
- XP-Pen Star `G430S`

If you test another tablet model (Wacom, XP-Pen, Huion, Gaomon, VEIKK), open an issue on GitHub with your results or `--probe` output.

## Features

- Standalone ~600 KB executable with static CRT linkage (`/MT`) and under 3 MB of RAM usage.
- **Area Tab:** Interactive area preview canvas with drag-to-move/resize, aspect ratio lock (`Screen` / `Area` auto-match with configurable decimal precision), rotation, area clipping (`Clip Area`), area lock (`Lock Area`), pen tip click toggle, and area presets.
- **Settings Tab:** Dynamic Windows 11 Mica/Dark/Light theme integration, 9 built-in color palettes, custom color picker and theme presets, custom background image with adjustable box blur (`0–20 px`) and card opacity (`10–100%`), Kinematic Extrapolation controls, Latency Spin control, and live VT100 debug telemetry console.
- **System Tray Integration:** Minimizes cleanly to the system tray with quick toggles for Pen Click and window restoration.

## Setup

1. Close conflicting tablet drivers (OpenTabletDriver, Wacom Center, Pentablet) or hide the native device via HidHide so **osu!Point** can open the USB interface in exclusive mode.
2. In **osu!** settings:
   - Turn **OFF** `Raw Input`.
   - Keep `Sensitivity` at `1.0x`.
3. Launch `osuPoint.exe` (or `osuPoint-noavx.exe` on older CPUs without AVX2 support).

## Command-Line Options

```cmd
osuPoint.exe --help                  # Show available command-line options
osuPoint.exe --debug                 # Enable live VT100 telemetry console and verbose logs
osuPoint.exe --list-devices          # List detected tablet HID interfaces and match scores
osuPoint.exe --probe                 # Dump HID descriptor capabilities and 50 raw packets to probe_dump.txt
osuPoint.exe --calibrate             # Print live raw/normalized/screen coordinate min-max traces
osuPoint.exe --dump-reports <file>   # Append raw incoming HID reports to a binary file
osuPoint-dev.exe --selftest          # Run V1-V21 golden decode vectors, transform, and extrapolation tests
```

# Adding Custom Tablets

To override a built-in profile or add an unlisted model, place a `.cfg` file inside the `tablets/` folder next to `osuPoint.exe`:

```ini, TOML
name=My Custom Tablet
vid=0x28BD
pid=0x0914
max_x=32000
max_y=20000
width_mm=160.0
height_mm=100.0
report_len=14
report_id=0x02
x_offset=2
y_offset=4
coord_bits=16
tip_pressure_threshold=0
init_output=0x02 0xB0 0x04
```

Supported initialization directives in `.cfg` files: `init_feature`, `init_output`, and `init_write`.

## Configuration (`config.ini`)

Settings are stored in `config.ini` next to the executable and hot-reloaded automatically when modified:

```ini, TOML
width=80
height=45
center_x=76
center_y=47.5
rotation=0
monitor=0
lock_ratio=1
ratio=16:9
ratio_precision=2
lock_drag=0
clip_area=0
pen_click=1
barrel_button=1
antialiasing=1
extrapolation=0
extrapolation_tau=2.5
spin_percent=0
debug_mode=0
palette=system
ui_opacity=80
bg_blur=0
active_preset=Default
```

# Building

## Build Script

Run `build.bat` from the repository root:

```DOS
build.bat 1  # AVX2 Release (osuPoint.exe)
build.bat 2  # SSE2 Compatibility (osuPoint-noavx.exe)
build.bat 3  # Developer + Selftest (osuPoint-dev.exe)
build.bat 4  # Build all targets
```

## Manual Compilation

From an x64 Native Tools Command Prompt for VS:

**AVX2 Release (`osuPoint.exe`):**

```DOS
cl.exe /nologo /O2 /Ot /Oi /Ob3 /GL /Gy /Gw /Oy /cgthreads1 /Brepro /Zc:inline /Zc:threadSafeInit- /Zc:tlsGuards- /Zc:nrvo /jumptablerdata /presetPadding- /favor:blend /guard:cf- /volatile:iso /std:c++20 /arch:AVX2 /fp:fast /fp:except- /Gv /GA /Qfast_transcendentals /Qspectre- /sdl- /DNDEBUG /D_HAS_EXCEPTIONS=0 /D_DISABLE_VECTOR_ANNOTATION /D_ITERATOR_DEBUG_LEVEL=0 /GS- /GR- /EHs- /EHc- /EHa- /MT /utf-8 /W4 /WX /I . /Fe:osuPoint.exe osuPoint.cpp src\cli.cpp src\log.cpp src\selftest.cpp osuPoint.res /link /SUBSYSTEM:WINDOWS /LTCG /OPT:REF /OPT:ICF /MERGE:.rdata=.text /GUARD:NO /CETCOMPAT:NO /DYNAMICBASE:NO /HIGHENTROPYVA:NO /INCREMENTAL:NO /FILEALIGN:4096 /STACK:0x100000,0x100000 /HEAP:0x100000,0x100000 /DEBUG:NONE /RELEASE
```

**SSE2 Baseline (`osuPoint-noavx.exe`):**

```DOS
cl.exe /nologo /O2 /Ot /Oi /Ob3 /GL /Gy /Gw /Oy /cgthreads1 /Brepro /Zc:inline /Zc:threadSafeInit- /Zc:tlsGuards- /Zc:nrvo /jumptablerdata /presetPadding- /favor:blend /guard:cf- /volatile:iso /std:c++20 /arch:AVX /fp:fast /fp:except- /Gv /GA /Qfast_transcendentals /Qspectre- /sdl- /DNDEBUG /D_HAS_EXCEPTIONS=0 /D_DISABLE_VECTOR_ANNOTATION /D_ITERATOR_DEBUG_LEVEL=0 /GS- /GR- /EHs- /EHc- /EHa- /MT /utf-8 /W4 /WX /I . /Fe:osuPoint.exe osuPoint.cpp src\cli.cpp src\log.cpp src\selftest.cpp osuPoint.res /link /SUBSYSTEM:WINDOWS /LTCG /OPT:REF /OPT:ICF /MERGE:.rdata=.text /GUARD:NO /CETCOMPAT:NO /DYNAMICBASE:NO /HIGHENTROPYVA:NO /INCREMENTAL:NO /FILEALIGN:4096 /STACK:0x100000,0x100000 /HEAP:0x100000,0x100000 /DEBUG:NONE /RELEASE
```

# License

GPL-3.0
