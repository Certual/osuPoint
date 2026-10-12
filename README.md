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

**osu!Point** is designed to bypass the traditional Windows input pipeline, directly parsing HID reports and injecting absolute mouse coordinates to achieve the absolute minimum input latency possible.

## Performance & Testing

In side-by-side testing against OpenTabletDriver, newer builds show roughly a 50% reduction in input delay. 

This was measured on a 240 Hz monitor recorded with a smartphone camera in 480 FPS slow-motion, calculating the frame delta between the physical hand/pen movement and the first visible cursor movement on screen.

The lower latency is achieved by:
- Bypassing virtual driver queues (VMulti) and injecting directly via absolute desktop coordinates.
- Using C++20 `std::binary_semaphore` (which utilizes the undocumented Windows `WaitOnAddress` syscall for the user-space fast-path) to drop thread-wake overhead to **~20ns**.
- Using a 100% lock-free packed 64-bit atomic integer to pass coordinate packets between threads without context-switching the USB polling thread.
- Setting the Windows HID input buffer queue to 4 reports.
- Using high-resolution NTAPI timers (0.5 ms) and MMCSS Pro Audio thread scheduling.

## Hardware Status & Testing

The codebase contains configuration profiles for over 160 tablet models. The following devices have been physically tested or verified by the community to work perfectly:

- Wacom One (CTL-472)
- Wacom Bamboo (CTL-471)
- Wacom Intuos S / M (CTL-4100 / CTL-6100, including WL/Bluetooth variants)
- XP-Pen Star G430S

### Beta Testers Wanted
If you own any other tablet (Wacom, XP-Pen, Huion, Gaomon, Veikk), please test the driver and report whether your device works properly (detection, pen tracking, clicks, proximity). You can submit reports by opening an issue on GitHub. Unknown XP-Pen devices will automatically attempt to use a generic fallback profile with a magic wake-up packet.

## Features

- Native C++20 with zero dynamic memory allocations in the hot input loop.
- Direct Win32 HID communication without virtual drivers (VMulti).
- Standalone ~300 KB executable with under 3 MB of RAM usage.
- Built-in GUI: visual area preview, drag-to-resize/move, 16:9 ratio lock, rotation, and display selection.
- Two-thread pipeline (USB Polling / Coordinate Processing) decoupled via C++20 lock-free atomics and `std::jthread`.
- Automatic P-core CPU affinity pinning on hybrid processors.

## Setup

1. Close existing tablet software (OpenTabletDriver, Wacom Center, Pentablet) so osu!Point can claim the USB handle.
2. In osu! settings:
   - Turn OFF "Raw Input".
   - Keep "Sensitivity" at 1.0x.
   (osu!Point injects absolute desktop coordinates via SendInput, so Raw Input is unnecessary).
3. Launch `osuPoint.exe`. The driver will attempt to auto-detect your tablet. Adjust your active area and test.

## Adding Custom Tablets

If your tablet is not in the built-in database, you can place a `.cfg` file in a `tablets/` subfolder next to the executable:

```ini
name=My Tablet
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
```

## Configuration

Settings are saved in `config.ini` in the executable folder and reload automatically on save:

```ini
width=80.0
height=55.0
center_x=76.0
center_y=47.5
rotation=0.0
monitor=0
lock_ratio=1
ratio=16:9
clip_area=0
pen_click=1
```

## Building

### Quick Build Script
Run `build.bat` to interactively select a target or pass an argument:
```cmd
build.bat 1   # AVX2 Release (osuPoint.exe)
build.bat 2   # SSE2 Compatibility (osuPoint-noavx.exe)
build.bat 3   # Developer + Selftest (osuPoint-dev.exe)
build.bat 4   # All targets
```

### Manual Compilation
Open the MSVC **x64 Native Tools Command Prompt** and run:

AVX2 Release build:
```cmd
cl.exe /nologo /O2 /Ot /Oi /Ob3 /GL /Gy /Gw /Oy /cgthreads1 /Brepro /Zc:inline /Zc:threadSafeInit- /Zc:tlsGuards- /Zc:nrvo /jumptablerdata /presetPadding- /favor:blend /guard:cf- /volatile:iso /std:c++20 /fp:fast /fp:except- /Gv /GA /Qfast_transcendentals /Qspectre- /sdl- /DNDEBUG /D_HAS_EXCEPTIONS=0 /D_DISABLE_VECTOR_ANNOTATION /D_ITERATOR_DEBUG_LEVEL=0 /GS- /GR- /EHs- /EHc- /EHa- /MT /utf-8 /W4 /WX /I . /I src /arch:AVX2 /Fe:osuPoint.exe src\platform.cpp src\tablet_decoder.cpp src\driver_pipeline.cpp src\config_manager.cpp src\ui_view.cpp src\osuPoint.cpp src\osuPoint.res /link /SUBSYSTEM:WINDOWS /LTCG /OPT:REF /OPT:ICF /MERGE:.rdata=.text /GUARD:NO /CETCOMPAT:NO /DYNAMICBASE:NO /HIGHENTROPYVA:NO /INCREMENTAL:NO /FILEALIGN:4096 /STACK:0x100000,0x100000 /HEAP:0x100000,0x100000 gdiplus.lib comdlg32.lib /DEBUG:NONE /RELEASE
```

SSE2 Compatibility build:
```cmd
cl.exe /nologo /O2 /Ot /Oi /Ob3 /GL /Gy /Gw /Oy /cgthreads1 /Brepro /Zc:inline /Zc:threadSafeInit- /Zc:tlsGuards- /Zc:nrvo /jumptablerdata /presetPadding- /favor:blend /guard:cf- /volatile:iso /std:c++20 /fp:fast /fp:except- /Gv /GA /Qfast_transcendentals /Qspectre- /sdl- /DNDEBUG /D_HAS_EXCEPTIONS=0 /D_DISABLE_VECTOR_ANNOTATION /D_ITERATOR_DEBUG_LEVEL=0 /GS- /GR- /EHs- /EHc- /EHa- /MT /utf-8 /W4 /WX /I . /I src /Fe:osuPoint-noavx.exe src\platform.cpp src\tablet_decoder.cpp src\driver_pipeline.cpp src\config_manager.cpp src\ui_view.cpp src\osuPoint.cpp src\osuPoint.res /link /SUBSYSTEM:WINDOWS /LTCG /OPT:REF /OPT:ICF /MERGE:.rdata=.text /GUARD:NO /CETCOMPAT:NO /DYNAMICBASE:NO /HIGHENTROPYVA:NO /INCREMENTAL:NO /FILEALIGN:4096 /STACK:0x100000,0x100000 /HEAP:0x100000,0x100000 gdiplus.lib comdlg32.lib /DEBUG:NONE /RELEASE
```

## Transparency Note

This project was developed with assistance from AI (LLMs) by a beginner developer. All code, API implementations, and packet parsers have been manually debugged, reviewed, and validated on available hardware.

## License

GPL-3.0
