# osu!Point

A high-performance, ultra-low-latency drawing tablet driver built specifically for osu!, written in pure C++20. It communicates directly with USB HID devices in user-mode, requires no kernel drivers or digital signatures, and delivers true 1000 Hz+ report rates with zero garbage collection and sub-microsecond software overhead.

---

## Why does this exist?

Most osu! players rely on OpenTabletDriver (OTD). While OTD is a great project with broad hardware compatibility, it is built on C# (.NET Core). That architecture introduces several inherent drawbacks for competitive rhythm gameplay:
* **Garbage Collector (GC) pauses:** Periodic background memory sweeps that can cause subtle frame time spikes and micro-stutters;
* **Virtualization overhead:** Routing movement through virtual kernel mouse drivers (VMulti), adding an extra queue to the input pipeline;
* **Resource consumption:** Consumes 50 to 150 MB of RAM with 0.5 to 1.5 ms of software latency.

**osu!Point** eliminates all intermediate layers. It contains no jitter smoothing, no pressure tracking, and no background runtimes. The polling thread reads raw hardware packets, applies precalculated coordinate transformations, and injects clean mouse events in under 50 nanoseconds.

---

## Comparison

| Metric | Official Wacom Driver | OpenTabletDriver | osu!Point |
| :--- | :--- | :--- | :--- |
| **Language** | C++ / Background Services | C# (.NET Core) | **Native C++20** |
| **Garbage Collector (GC)** | None | Yes (.NET GC) | **None (Zero allocations)** |
| **Software Pipeline Latency** | 4–15 ms (smoothing filters) | 0.5–1.5 ms | **< 50 nanoseconds** |
| **Memory Allocations per Packet** | Yes | Yes | **0 bytes (pure registers)** |
| **RAM Usage** | ~200 MB | ~80 MB | **< 2 MB** |
| **Binary Size** | ~150 MB | ~45 MB | **~120 KB (standalone .exe)** |
| **Driver / Kernel Cert Required** | Yes | Yes (for VMulti output) | **None (User-Mode Win32)** |

---

## Architecture & Key Features

* **High-Rate Absolute Event Stream (`SendInput`):** Instead of using `SetCursorPos` (which Windows DWM throttles to your monitor's display refresh rate), osu!Point uses high-speed `SendInput` mapped to the 16-bit Windows absolute mouse grid (`0`–`65535`). This guarantees unthrottled 1000 Hz+ throughput for overclocked hardware and custom firmware mods.
* **Precalculated Inverse Rotation:** Supports arbitrary rotation angles with decimal precision (e.g. `-3°` or `14.5°`). Trigonometric functions (`sin`/`cos`) are precomputed only when settings change; the game loop executes only fast additions and multiplications.
* **Familiar Coordinate System:** Active areas are defined by Width, Height, Center X, and Center Y in millimeters, identical to OpenTabletDriver.
* **Hardware Boundary Clamping:** The active area is mathematically constrained inside the physical tablet dimensions, preventing the cursor box from leaving the usable surface even under extreme rotation.
* **Live Visual Preview:** Displays the rotated area polygon inside a hardware-clipped GDI canvas that prevents visual artifacts.
* **Modular Tablet Profiles:** Support for any tablet model can be added via plain text `.cfg` files inside the `tablets/` directory without recompiling.
* **Thread Affinity & Priority:** The USB polling thread is isolated onto a dedicated physical CPU core (`Core 2`) and elevated to `THREAD_PRIORITY_TIME_CRITICAL`.
* **High-Resolution System Timers:** Automatically enforces a 1 ms Windows timer period (`timeBeginPeriod(1)`) to eliminate OS thread scheduler jitter.
* **Auto-Recovery & Persistence:** Settings are auto-saved to `config.ini` in the executable folder. Corrupted or missing configs self-heal to full area defaults.

---

## Building from Source

You will need the Microsoft C++ compiler (MSVC), available through Visual Studio or Build Tools for Visual Studio (select the "Desktop development with C++" workload).

1. Open **x64 Native Tools Command Prompt for VS**.
2. Navigate to the project directory:
```cmd
cd C:\path\to\osuPoint
```
3. Compile the release binary:
```cmd
cl /O2 /Oi /Ot /GL /std:c++20 driver.cpp /link /SUBSYSTEM:WINDOWS
```
4. A standalone `driver.exe` (~120 KB) will be generated.

---

## Setup & osu! Configuration

### 1. System Preparation
If official Wacom software is installed, stop its background services:
1. Open Task Manager $\to$ Services tab.
2. Locate `WTabletServicePro` or `Wacom Professional Service` $\to$ Right-click $\to$ **Stop**.
*(Otherwise, Windows will prevent user-mode applications from accessing the tablet's USB handle).*

### 2. In-Game Settings (Critical)
* **Raw Input: OFF.** 
  > **Why:** Windows pointer acceleration curves only affect *relative* mouse deltas ($\Delta X, \Delta Y$). Because osu!Point uses `MOUSEEVENTF_ABSOLUTE`, Windows applies **zero acceleration**—the mapping is 100% linear. Enabling Raw Input in osu! forces the game engine to interpret absolute coordinates as relative deltas, which can cause the cursor to snap to the top-left corner or introduce redundant normalization math.
* **Mouse Sensitivity: 1.0x.**
  > Adjust your play area strictly inside osu!Point using millimeters.
* **Screen Mode: Exclusive Fullscreen or Borderless.**

---

## Adding Other Tablets

On first launch, osu!Point creates a `tablets/` directory and generates a default configuration file for the Wacom CTL-472.

To add another tablet (such as an XP-Pen, Gaomon, or Huion), create a new `.cfg` file in the `tablets/` folder (for example, `XP-Pen_G640.cfg`):

```ini
name=XP-Pen G640
vid=0x28BD
pid=0x0094
max_x=32767
max_y=32767
width_mm=152.4
height_mm=101.6
report_len=8
report_id=0x02
x_offset=2
y_offset=4
init_feature=
```

When launched, osu!Point scans connected USB devices, matches the `VID`/`PID`, displays the detected model in the window title, and adapts the coordinate scaling and visual preview to the physical dimensions of that tablet.

---

## License

This project is licensed under the MIT License. You are free to use, modify, and distribute it.
