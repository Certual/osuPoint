# osu!Point

A minimalist drawing tablet driver for osu!, written in pure C++20. It communicates directly with USB devices in user-mode, requires no kernel drivers or digital signatures, and maps pen coordinates to your screen without abstraction layers, filters, or latency.

---

## Why does this exist?

Most osu! players use OpenTabletDriver (OTD). OTD is a great project with broad hardware support, but it runs on C# (.NET Core). That means:
* Running inside a virtual machine with a Garbage Collector (GC), which can cause occasional micro-stutters;
* Injecting cursor movement through virtual mouse drivers (VMulti), adding an extra queue to the input pipeline;
* Consuming 50 to 150 MB of RAM with around 0.5 to 1.5 ms of software processing overhead.

osu!Point is built for a single purpose: sending raw pen position to the game as fast as the hardware allows. It strips out jitter smoothing, tip pressure tracking, and gestures. What remains is coordinate transformation math running in CPU registers in under 50 nanoseconds.

---

## Comparison

| Metric | Official Wacom Driver | OpenTabletDriver | osu!Point |
| :--- | :--- | :--- | :--- |
| **Language** | C++ / Services | C# (.NET Core) | **Native C++20** |
| **Garbage Collector (GC)** | None | Yes (.NET GC) | **None** |
| **Software Latency** | 4–15 ms (filters) | 0.5–1.5 ms | **< 50 nanoseconds** |
| **Allocations per Report** | Yes | Yes | **0 bytes (registers/stack)** |
| **RAM Usage** | ~200 MB | ~80 MB | **~1.5 MB** |
| **Binary Size** | ~150 MB | ~45 MB | **~120 KB (single .exe)** |
| **Kernel Driver / Cert** | Required | Required for VMulti | **None (User-Mode)** |

---

## Features

* **Division-free polling loop:** Scale factors are precalculated when settings change. The loop only performs basic additions and multiplications.
* **Arbitrary rotation angles:** Supports any rotation angle with decimal precision (such as -3 or 14.5 degrees). Trigonometric functions (`sin`/`cos`) are precomputed and never run inside the loop.
* **Standard coordinate system:** Area is defined by Width, Height, Center X, and Center Y in millimeters, identical to OpenTabletDriver.
* **Hardware boundary clamping:** The active area is mathematically constrained inside the physical tablet dimensions and cannot extend outside.
* **Live visual preview:** Displays the rotated active area polygon relative to the physical tablet boundaries in real time.
* **Modular tablet profiles:** Support for other tablet models can be added using plain text `.cfg` files inside the `tablets/` directory without recompiling.
* **Thread isolation:** The USB polling thread is pinned to an isolated physical CPU core (`Core 2`) and set to `THREAD_PRIORITY_TIME_CRITICAL`.
* **High-resolution system timers:** Automatically requests a 1 ms Windows timer period (`timeBeginPeriod(1)`) to eliminate OS scheduler jitter.
* **Auto-saving:** Settings are stored in `config.ini` in the executable folder.

---

## Building from Source

You will need the Microsoft C++ compiler (MSVC), which comes with Visual Studio or Build Tools for Visual Studio (select the "Desktop development with C++" workload).

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

## Setup & Configuration

1. **Stop conflicting Wacom services:**
   If official Wacom software is installed, open Task Manager $\to$ Services tab $\to$ locate `WTabletServicePro` or `Wacom Professional Service` and stop them. Otherwise, Windows will lock the tablet's USB handle.
2. Run `driver.exe`.
3. By default, the driver initializes to the full tablet surface with zero rotation. Adjust the area and center as needed.
4. **osu! In-Game Settings:**
   * **Raw Input: OFF.** Raw Input in osu! is designed for physical mice to bypass Windows pointer acceleration. For absolute tablet positioning, `SetCursorPos` already bypasses pointer acceleration and writes coordinates directly into Windows subsystem memory without virtualization delay.
   * **Mouse Sensitivity: 1.0x.** Adjust your active area strictly within osu!Point.
   * **Screen Mode:** Exclusive Fullscreen or Borderless.

---

## Adding Other Tablets

On first launch, osu!Point creates a `tablets/` directory and writes a default profile for the Wacom CTL-472.

To add another tablet, create a new `.cfg` file inside the `tablets/` directory (for example, `XP-Pen_G640.cfg`):

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

When started, osu!Point scans connected USB devices, matches the `VID`/`PID`, displays the detected model in the window title, and adapts the area limits and visualizer to the new tablet's physical dimensions.

---

## License

This project is licensed under the MIT License. You are free to use, modify, and distribute it.
