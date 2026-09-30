# osu!Point

A lightweight, standalone drawing tablet driver written in pure C++20 for Windows, designed specifically for osu!.

It runs entirely in user-mode, uses no background runtimes or virtual kernel drivers, and compiles into a single ~300 KB binary using under 1.9 MB of RAM.

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
- XP-Pen Star 03 V2

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

Open the MSVC **x64 Native Tools Command Prompt**. Compile the resource file first if you modified icons (`rc.exe osuPoint.rc`), then compile:

Standard extreme-low-latency build:
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
