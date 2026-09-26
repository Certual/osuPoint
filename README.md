# osu!Point

A lightweight, low-latency drawing tablet driver for osu!, written in C++20. It communicates directly with USB HID devices in user-mode without virtual mouse drivers (VMulti), background services, or runtime garbage collection.

---

## Features

* **Direct HID Communication:** Reads raw reports directly via Win32 HID APIs, bypassing virtual kernel emulation layers.
* **Dual-Core Real-Time Pipeline:** HID reading and coordinate processing run on two dedicated threads, each pinned to a separate physical P-core (with automatic hybrid-CPU detection) to eliminate scheduling contention between them.
* **SIMD Coordinate Transform:** Rotation, scaling, and multi-monitor mapping are evaluated with vectorized (SSE2/AVX2/FMA) dual-axis math on the hot path.
* **Adaptive Input Queue:** The primary coordinate endpoint is kept shallow (4 buffered packets) to avoid backlog, while auxiliary endpoints on multi-interface tablets get deeper buffering (16) without affecting movement latency.
* **Zero Runtime Overhead:** No garbage collector, zero dynamic allocations in the input loop.
* **Real-Time Priority:** Uses MMCSS Pro Audio thread scheduling and locks kernel timer resolution to 0.5 ms.
* **Broad Hardware Compatibility:** Ships with a built-in database of 160+ tablet configurations (ported from OpenTabletDriver) covering Wacom, XP-Pen, Huion, Gaomon, VEIKK, and more — no manual `.cfg` file needed for most tablets. Automatic interface scoring picks the correct digitizer/vendor/mouse endpoint out of everything a tablet exposes.
* **Multi-Endpoint Support:** Tablets that split pen data and auxiliary controls (buttons, wheel) across separate USB interfaces are read from up to 4 endpoints in parallel.
* **Toggleable Pen Click:** Enable or disable the pen tip click independently of cursor movement — from the settings window or the tray icon — for tracking practice or when you want the tablet as a pure pointing device. Includes stuck-click protection for abrupt pen lifts.
* **Conflicting Driver Detection:** Automatically detects other tablet drivers (OpenTabletDriver, PenTablet, etc.) still running in the background and flags it in the window title/tray tooltip.
* **Letterboxing & Multi-Monitor:** Native absolute mapping to primary/secondary monitors and letterboxed osu! resolutions without aspect-ratio warping.
* **Clip Area:** Optional hard-clip of the cursor at the configured active-area border, instead of free-tracking past it.
* **Live Hot-Reload:** Updates active area and display settings from `config.ini` in real time without restarting.

---

## Latency Benchmark (480 FPS)

High-speed camera test measuring input lag from physical pen movement to the first on-screen cursor response.

### Test Setup
* **Camera:** 480 FPS (1 frame ≈ 2.08 ms)
* **Monitor:** 240 Hz (~4.17 ms frame interval)
* **Tablet:** Wacom One CTL-472 (700 Hz firmware)
* **Analysis Tool:** Kinovea

### Results

| Sample | OpenTabletDriver v0.6.7 | osu!Point v0.4.0 |
| :--- | :---: | :---: |
| **Run 1** | 5 frames (10.42 ms) | 5 frames (10.42 ms) |
| **Run 2** | 4 frames (8.33 ms) | 4 frames (8.33 ms) |
| **Run 3** | 4 frames (8.33 ms) | 3 frames (6.25 ms) |
| **Run 4** | 5 frames (10.42 ms) | 3 frames (6.25 ms) |
| **Average** | **4.50 frames (9.38 ms)** | **3.75 frames (7.81 ms)** |
| **Range** | 8.33 – 10.42 ms ($\Delta$ 2.08 ms) | 6.25 – 10.42 ms ($\Delta$ 4.17 ms) |

* **Latency:** osu!Point responds **~1.57 ms faster** on average (~17% reduction).
* **Jitter:** The 2–4 ms variation between runs is driven by the 240 Hz monitor refresh intervals (4.17 ms per frame), not driver instability.

---

## Setup & In-Game Configuration

### 1. Close Existing Drivers
Disable any running tablet software or background services (e.g. stop `WTabletServicePro` in Task Manager for Wacom, or exit OpenTabletDriver). osu!Point will flag a known conflicting driver process in its window title/tray tooltip if one is still running.

### 2. osu! Settings (Important)
* **Raw Input: OFF.**
  osu!Point sends absolute coordinates (`MOUSEEVENTF_ABSOLUTE`), which Windows maps 1:1 without acceleration curves. Enabling Raw Input makes the game treat absolute coordinates as relative deltas, causing cursor snapping.
* **Mouse Sensitivity: 1.0x.**
  Adjust your play area strictly inside the osu!Point app or `config.ini`.

  `config.ini` generated automatically on the first launch. Changes apply immediately upon saving the file.

### 3. Pen Click
Click synthesis from the pen tip can be toggled independently of movement — via the "Pen Click" checkbox in the app window, or the tray icon's right-click menu. Turn it off to use the tablet purely for cursor tracking (e.g. aim practice) without triggering clicks.

---

## Supported Tablets

The driver auto-detects connected tablets in two stages:

1. **User profiles** — any `.cfg` file placed in the `tablets/` folder.
2. **Built-in database** — 160+ configurations ported from OpenTabletDriver, matched automatically by device ID and, where several variants share an ID, by the tablet's own USB product name. No manual configuration required for these.

Tested and confirmed working:

* **Wacom:** CTL-472, CTL-471, and the wider CTL/CTH/CTE/Intuos family via the built-in database
* **XP-Pen:** G430, G430S, G640, Star 03, Deco series, and other Rev A/B report variants
* **Gaomon, Huion, VEIKK:** covered via the built-in database (shared HID report families)

### Adding Custom Tablets
If your tablet isn't recognized automatically, create a `.cfg` file in the `tablets/` folder:

```ini
name=Tablet Name
vid=0x056A
pid=0x037A
max_x=15200
max_y=9500
width_mm=152.0
height_mm=95.0
report_len=10
report_id=0x02
x_offset=2
y_offset=4
init_feature=0x02 0x02
init_output=
```

`init_output` is optional and only needed for tablets that require an output-report "wake" handshake before they start streaming.

---

## Building from Source

Requires MSVC (Visual Studio 2022 / Build Tools). `builtin_tablets.h` must be in the same folder as `osuPoint.cpp` — it's included directly and holds the built-in tablet database.

Run inside **x64 Native Tools Command Prompt for VS**:

```cmd
cl.exe /nologo /O2 /Ob3 /Ot /GL /std:c++20 /fp:precise /DNDEBUG /GS- /GR- /EHs-c- /MT /Gw /utf-8 osupoint.cpp osupoint.res /Fe:osuPoint.exe /link /SUBSYSTEM:WINDOWS /LTCG /OPT:REF /OPT:ICF
```
For AVX2 build
```cmd
cl.exe /nologo /O2 /Ob3 /Ot /GL /std:c++20 /arch:AVX2 /fp:precise /DNDEBUG /GS- /GR- /EHs-c- /MT /Gw /utf-8 osupoint.cpp osupoint.res /Fe:osuPoint.exe /link /SUBSYSTEM:WINDOWS /LTCG /OPT:REF /OPT:ICF
```
---

## Known Limitations

* On tablets that only expose a legacy HID *mouse-mode* interface (no dedicated digitizer report), the Pen Click toggle suppresses clicks via a system-wide low-level mouse hook. This cannot distinguish the tablet's own clicks from a separately connected physical mouse — if you use both at once on such a tablet, disabling Pen Click will silence both.

---

## AI Disclosure

This project is developed with the assistance of AI (LLMs) for code generation, architecture planning, and debugging. The codebase and build artifacts are manually tested, verified with high-speed camera benchmarking, and maintained for stability and minimal latency.

---

## License

Licensed under the [GNU General Public License v3.0 (GPLv3)](LICENSE).
