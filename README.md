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
| **RAM Usage** | ~200 MB | ~80 MB | **< 2.5 MB** |
| **Binary Size** | ~150 MB | ~45 MB | **~300 KB (standalone .exe)** |
| **Driver / Kernel Cert Required** | Yes | Yes (for VMulti output) | **None (User-Mode Win32)** |

---

## Supported Tablets (Out-of-the-Box)

The release archive includes pre-configured hardware profiles inside the `tablets/` directory. When launched, **osu!Point automatically detects your connected tablet via USB VID/PID** and loads its physical specs:

### Wacom
* **One by Wacom:** CTL-472, CTL-672, CTL-471
* **Intuos (Classic):** CTL-480, CTH-480, CTL-490, CTL-4100, CTL-6100
* **Bamboo:** CTL-470
* **Intuos 4 & 5:** PTK-440, PTK-640, PTH-450, PTH-650
* **Intuos Pro (Gen 1):** PTH-451, PTH-651, PTH-851
* **Intuos Pro (Gen 2):** PTH-460, PTH-660, PTH-860

### XP-Pen
* **Star Series:** Star G640, Star G430S, Star G640S
* **Deco Series:** Deco 01

### Gaomon
* S620

### Huion
* 420 / H420
* Inspiroy H430P
* Inspiroy H640P

### VEIKK
* S640

---

## Architecture & Key Features

* **Unthrottled 1000 Hz+ Pipeline (`SendInput`):** Instead of `SetCursorPos` (which Windows DWM caps to your display refresh rate), osu!Point uses high-speed `SendInput` mapped to the 16-bit absolute mouse coordinate grid (`0`–`65535`). This guarantees unthrottled 1000 Hz+ throughput for custom firmware and overclocked hardware.
* **Precalculated Inverse Rotation:** Supports arbitrary rotation angles with decimal precision (e.g. `-3°` or `14.5°`). Trigonometric functions (`sin`/`cos`) are precomputed once on input; the tracking loop executes only fast additions and multiplications.
* **Familiar Coordinate System:** Active areas are defined by Width, Height, Center X, and Center Y in millimeters, identical to OpenTabletDriver.
* **Hardware Boundary Clamping:** The active area is mathematically bounded within physical tablet limits, preventing the cursor box from leaving the usable surface even under extreme rotation.
* **Modern Dark UI:** Features a native Windows 10/11 dark title bar (`DWMWA_USE_IMMERSIVE_DARK_MODE`), Segoe UI typography, and a realistic tablet bezel preview that prevents border clipping at Full Area.
* **Procedural 32-bit Transparent Icon:** Built-in anti-aliased ARGB application icon generated dynamically in memory—no external `.ico` files or taskbar background boxes.
* **Thread Affinity & Priority:** The USB polling thread is isolated onto a dedicated physical CPU core (`Core 2`) and elevated to `THREAD_PRIORITY_TIME_CRITICAL`.
* **High-Resolution System Timers:** Automatically enforces a 1 ms Windows timer period (`timeBeginPeriod(1)`) to eliminate OS thread scheduler jitter.
* **Auto-Recovery & Persistence:** Settings are auto-saved to `config.ini` in the executable folder. Corrupted or missing configs self-heal to full-area factory defaults.

---

## Release Package Structure

When downloading the release `.zip`, keep the executable and the `tablets/` folder together:

```text
osuPoint/
├── driver.exe
├── config.ini          (generated on first launch)
└── tablets/
    ├── Wacom_CTL-472.cfg
    ├── Wacom_CTL-480.cfg
    ├── XP-Pen_G640.cfg
    ├── Gaomon_S620.cfg
    └── ... (28 pre-built profiles)
```

---

## Setup & osu! Configuration

### 1. System Preparation
If official tablet software (e.g. Wacom Desktop Center) is installed, stop its background services:
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
4. A standalone `driver.exe` (~130 KB) will be generated.

---

## Adding More Tablets

If your tablet model is not among the 28 pre-configured profiles, you can add support for it by creating a new `.cfg` file in the `tablets/` folder:

```ini
name=Your Tablet Name
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
```

Upon startup, osu!Point will match the USB `VID`/`PID`, display the model name in the title bar, and adapt the coordinate scaling and visual preview to the physical dimensions of that tablet.

---

## License

This project is licensed under the **GNU General Public License v3.0 (GPLv3)**.

You are free to run, study, modify, and redistribute this software. However, any derivative work, fork, or distribution must also remain open-source and licensed under GPLv3. See the [LICENSE](LICENSE) file for the full text.
