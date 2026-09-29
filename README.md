# osu!Point

A lightweight, standalone drawing tablet driver written in pure C++20 for Windows, designed specifically for osu!.

It runs entirely in user-mode, uses no background runtimes or virtual kernel drivers, and compiles into a single ~300 KB binary using under 3 MB of RAM.

## Hardware Status & Testing

The codebase contains configuration profiles for over 160 tablet models, but only the following devices have been physically tested and verified on real hardware:

- Wacom One (CTL-472)
- Wacom Bamboo (CTL-471)
- XP-Pen Star G430S

### Beta Testers Wanted
If you own any other tablet (Wacom, XP-Pen, Huion, Gaomon, Veikk), please test the driver and report whether your device works properly (detection, pen tracking, clicks, proximity). You can submit reports by opening an issue on GitHub.

## Features

- Native C++20 with zero dynamic memory allocations in the hot input loop.
- Direct Win32 HID communication without virtual drivers (VMulti).
- Standalone ~300 KB executable with under 3 MB of RAM usage.
- Built-in GUI: visual area preview, drag-to-resize/move, 16:9 ratio lock, rotation, and display selection.
- Two-thread pipeline: USB polling and coordinate mapping are decoupled via lock-free seqlocks.
- Automatic P-core CPU affinity pinning on hybrid processors.
- MMCSS Pro Audio thread scheduling and 0.5 ms timer resolution via NTAPI.

## Setup

1. Close existing tablet software (OpenTabletDriver, Wacom Center, Pentablet) so osu!Point can claim the USB handle.
2. In osu! settings:
   - Turn OFF "Raw Input".
   - Keep "Sensitivity" at 1.0x.
   (osu!Point injects absolute desktop coordinates via SendInput, so Raw Input is unnecessary).
3. Launch osuPoint.exe. The driver will attempt to auto-detect your tablet. Adjust your active area and test.

## Adding Custom Tablets

If your tablet is not in the built-in database, you can place a .cfg file in a tablets/ subfolder next to the executable:

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

Settings are saved in config.ini in the executable folder and reload automatically on save:

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

Open the MSVC x64 Native Tools Command Prompt:

Standard build:
```cmd
cl.exe /nologo /O2 /Ob3 /Ot /GL /Gy /Zc:inline /std:c++20 /fp:precise /DNDEBUG /GS- /GR- /EHs-c- /MT /Gw /utf-8 osupoint.cpp osuPoint.res /Fe:osuPoint.exe /link /SUBSYSTEM:WINDOWS /LTCG /OPT:REF /OPT:ICF
```

AVX2 build:
```cmd
cl.exe /nologo /O2 /Ob3 /Ot /GL /Gy /Zc:inline /std:c++20 /arch:AVX2 /fp:precise /DNDEBUG /GS- /GR- /EHs-c- /MT /Gw /utf-8 osupoint.cpp osuPoint.res /Fe:osuPoint.exe /link /SUBSYSTEM:WINDOWS /LTCG /OPT:REF /OPT:ICF
```

## Transparency Note

This project was developed with assistance from AI (LLMs) by a beginner developer. All code, API implementations, and packet parsers have been manually debugged, reviewed, and validated on available hardware.

## License

GPL-3.0
