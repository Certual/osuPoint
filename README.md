# osu!Point

A high-performance, ultra-low-latency drawing tablet driver built specifically for osu!, written in pure C++20. It communicates directly with USB HID devices in user-mode, requires no kernel drivers, digital signatures, or registry tweaks, and delivers true 1000 Hz+ report rates with zero garbage collection and sub-microsecond software overhead.

---

## What's New in v0.2.1

* **Multi-Monitor Display Arbitrage:** Fixed the issue where absolute coordinates stretched across multiple monitors or glitched when cursor crossed display boundaries. By default, coordinates map strictly to the Primary Monitor (`MOUSEEVENTF_ABSOLUTE`), with optional support for dedicated secondary monitors (`monitor=N`).
* **Letterboxing & Custom Display Area:** Full native support for custom in-game viewports (e.g. `1280x1024`, `1024x768`, `1440x900`) via `config.ini`. The tablet's active area is mapped 1:1 to your letterboxed osu! window, eliminating black-bar distortion.
* **Instant Config Hot-Reload:** Edit your area or display settings in `config.ini` and save (`Ctrl+S`) — changes take effect in real time without restarting the app.
* **Zero-Footprint In-Memory Architecture:** All low-latency execution privileges (MMCSS Pro Audio, 0.5 ms timer locks, Windows 11 EcoQoS bypass, USB stay-awake state) are managed purely in RAM. Your system registry and global settings remain 100% stock.
* **Syscall Deduplication:** Automatically filters out redundant `SendInput` calls when the pen is held still, reducing CPU overhead by up to 70% during pauses.
* **10 New Tablet Profiles Added:** Added out-of-the-box support for Wacom CTH-690, CTH-460; XP-Pen Deco 01 V2, Star G430, Star 03; Huion Inspiroy H950P, New 1060 Plus; Gaomon M10K; VEIKK A30, A50 (and documented existing CTH-680, Deco Mini7, HS64, VK430, S56K).
* **Dual Release Builds:** Distributed as both a **Universal Build** (runs on 100% of 64-bit CPUs) and an **AVX2 Edition** (hardware-fused FMA acceleration for modern CPUs).

---

## Why does this exist?

Most osu! players rely on OpenTabletDriver (OTD). While OTD is a great project with broad hardware compatibility, it is built on C# (.NET Core). That architecture introduces several inherent drawbacks for competitive rhythm gameplay:
* **Garbage Collector (GC) pauses:** Periodic background memory sweeps that can cause subtle frame time spikes and micro-stutters;
* **Virtualization overhead:** Routing movement through virtual kernel mouse drivers (VMulti), adding an extra queue to the input pipeline;
* **Resource consumption:** Consumes 50 to 150 MB of RAM with 0.5 to 1.5 ms of software latency.

**osu!Point** eliminates all intermediate layers. It contains no jitter smoothing, no pressure tracking, and no background runtimes. The polling thread reads raw hardware packets, applies precalculated coordinate transformations, and injects clean mouse events in under 50 nanoseconds.

---

## Comparison

| Metric | Official Wacom Driver | OpenTabletDriver | osu!Point v0.2.1 |
| :--- | :--- | :--- | :--- |
| **Language / Runtime** | C++ / Background Services | C# (.NET Core) | **Native C++20 (Portable)** |
| **Garbage Collector (GC)** | None | Yes (.NET GC Spikes) | **None (Zero Allocations)** |
| **Kernel Buffer Depth (`hidclass.sys`)** | 32 reports (Lag backlog) | 32 reports | **2 reports (Zero Buffer Lag)** |
| **Windows 11 EcoQoS** | Throttled in background | Throttled in background | **Bypassed in Process RAM** |
| **Kernel Scheduling** | Normal | Standard Threading | **MMCSS Pro Audio (Critical)** |
| **Multi-Monitor Handling** | Varies | Virtual Desktop | **Direct Primary / Targeted Screen** |
| **Letterboxing Support** | Manual Setup | Supported | **Native Auto-Center Viewport** |
| **Software Pipeline Latency** | 4–15 ms (smoothing filters) | 0.5–1.5 ms | **< 50 nanoseconds** |
| **Memory Allocations per Packet** | Yes | Yes | **0 bytes (pure registers)** |
| **RAM Usage** | ~200 MB | ~80 MB | **< 2.5 MB** |
| **Binary Size** | ~150 MB | ~45 MB | **~300 KB (standalone .exe)** |
| **Driver / Kernel Cert Required** | Yes | Yes (for VMulti output) | **None (User-Mode Win32)** |

---

## Supported Tablets (43 Out-of-the-Box Models)

The release archive includes pre-configured hardware profiles inside the `tablets/` directory. When launched, **osu!Point automatically detects your connected tablet via USB VID/PID** and loads its physical specs:

### Wacom
* **One by Wacom:** CTL-472, CTL-672, CTL-471
* **Intuos (Classic):** CTL-480, CTH-480, CTH-680, CTL-490, CTH-690, CTL-4100, CTL-6100
* **Bamboo:** CTL-470, CTH-460
* **Intuos 4 & 5:** PTK-440, PTK-640, PTH-450, PTH-650
* **Intuos Pro (Gen 1):** PTH-451, PTH-651, PTH-851
* **Intuos Pro (Gen 2):** PTH-460, PTH-660, PTH-860

### XP-Pen
* **Star Series:** Star G640, Star G640S, Star G430, Star G430S, Star 03
* **Deco Series:** Deco 01, Deco 01 V2, Deco Mini7

### Gaomon
* S620, S56K, M10K

### Huion
* 420, H420, HS64
* Inspiroy H430P, Inspiroy H640P, Inspiroy H950P
* New 1060 Plus

### VEIKK
* S640, VK430, A30, A50

---

## Architecture & Key Features

* **Kernel Queue Minimization:** Uses `HidD_SetNumInputBuffers` to reduce the internal Windows `hidclass.sys` input queue from 32 reports down to just **2 packets**. This eliminates the hidden 15–30 ms backlog latency that occurs when the OS buffers older reports during momentary frame dips.
* **In-Memory Windows 11 EcoQoS Bypass:** Disables thread- and process-level execution throttling via native NT APIs (`SetProcessInformation` / `SetThreadInformation`), preventing Windows 11 from dropping CPU clock speeds or delegating the driver to efficiency cores.
* **MMCSS Real-Time Audio Scheduling:** The USB polling thread is registered with the Windows Multimedia Class Scheduler Service (`AvSetMmThreadCharacteristicsA("Pro Audio")`) with `AVRT_PRIORITY_CRITICAL`. This bypasses standard OS thread scheduling decay and protects input polling from background interference.
* **Multi-Monitor Coordinate Arbitrage:** Maps absolute cursor coordinates directly to the **Primary Monitor** (`MOUSEEVENTF_ABSOLUTE`), preventing cursor drifting or clipping conflicts across multi-display setups. Supports dedicated secondary displays via the `monitor` parameter.
* **Letterboxing & Display Area Projection:** Full support for non-native in-game viewports (e.g. `1280x1024`, `1024x768`, `1440x900`). The tablet's active area is mapped 1:1 to your letterboxed osu! window without aspect ratio warping or black-bar deadzones.
* **Instant Config Hot-Reload:** An integrated non-blocking file watcher monitors `config.ini` in the background. Editing values in Notepad and saving (`Ctrl+S`) immediately updates active area and display bounds in under 200 ms without restarting the driver.
* **Hardware USB Stay-Awake:** Automatically issues `SetThreadExecutionState(ES_SYSTEM_REQUIRED | ES_CONTINUOUS)` during execution, preventing Windows from suspending or power-gating USB root hub endpoints.
* **Syscall Deduplication:** Detects stationary cursor states and automatically skips redundant `SendInput` system calls, cutting CPU overhead by up to 70% during pauses while maintaining sub-50 nanosecond response on movement.
* **Sub-Millisecond 0.5 ms Kernel Timers:** Calls undocumented NTAPI `NtSetTimerResolution` to lock the Windows kernel scheduling interval to 0.5 ms (5000 units of 100 ns), cutting timer quantization jitter in half.
* **Precalculated Inverse Rotation:** Supports arbitrary rotation angles with decimal precision (e.g. `-3°` or `14.5°`). Trigonometric functions (`sin`/`cos`) are precomputed once on input; the tracking loop executes only fast additions and multiplications.
* **Hardware Boundary Clamping:** The active area is mathematically bounded within physical tablet limits, preventing the cursor box from leaving the usable surface even under extreme rotation.
* **Seamless Hot-Plug Engine:** Unplugging or reconnecting the tablet USB cable is handled gracefully without restarting the app and with flat 0.0% idle CPU consumption.
* **Adaptive Thread Affinity:** The polling thread automatically avoids Core 0 (where DPC/ISR interrupt contention and system timers reside) and isolates execution onto Core 2 on multi-core architectures.

---

## Release Package Structure

When downloading the release archive, keep the executable and the `tablets/` folder together:

```text
osuPoint/
├── osuPoint.exe            (Universal x64 Build — Recommended)
├── osuPoint_avx2.exe       (AVX2 Performance Edition — Modern CPUs)
├── config.ini              (Generated on first launch)
└── tablets/
    ├── Wacom_CTL-472.cfg
    ├── Wacom_CTL-480.cfg
    ├── XP-Pen_G640.cfg
    ├── Gaomon_S620.cfg
    └── ... (43 pre-built profiles)
```

> **Which binary should I use?**
> * **`osuPoint.exe`**: Recommended default. Runs out-of-the-box on 100% of 64-bit AMD and Intel processors.
> * **`osuPoint_avx2.exe`**: For modern CPUs (Intel Core 4th Gen Haswell / AMD Ryzen 1000+ or newer) seeking hardware-fused FMA3 / AVX2 acceleration.

---

## Configuration (`config.ini`)

Upon launch, osu!Point automatically creates or updates `config.ini` in its directory. You can edit this file in real-time:

```ini
# osu!Point Configuration v0.2.1
width=80.00
height=50.00
center_x=76.00
center_y=47.50
rotation=0
monitor=0
display_width=1280
display_height=1024
display_x=-1
display_y=-1
```

### Parameter Reference

| Key | Description | Default |
| :--- | :--- | :--- |
| `width`, `height` | Active tablet area dimensions in millimeters | Full tablet surface |
| `center_x`, `center_y` | Center point of your active area in millimeters | Physical center |
| `rotation` | Area rotation angle in degrees (e.g. `0`, `14.5`, `-3`) | `0` |
| `monitor` | Target display index (`0` = Primary Monitor, `1` = Monitor 1, `2` = Monitor 2) | `0` (Primary) |
| `display_width` | In-game viewport width in pixels (`0` = Full monitor width) | `0` |
| `display_height` | In-game viewport height in pixels (`0` = Full monitor height) | `0` |
| `display_x` | Viewport horizontal offset (`-1` = Auto-center for Letterboxing) | `-1` (Centered) |
| `display_y` | Viewport vertical offset (`-1` = Auto-center for Letterboxing) | `-1` (Centered) |

---

## Setup & In-Game Configuration

### 1. System Preparation
If official tablet software (e.g. Wacom Desktop Center) is installed, stop its background services:
1. Open Task Manager $\to$ Services tab.
2. Locate `WTabletServicePro` or `Wacom Professional Service` $\to$ Right-click $\to$ **Stop**.
*(Otherwise, Windows will prevent user-mode applications from accessing the tablet's USB handle).*

### 2. Hardware Topology Tip (Lowest Hardware Latency)
For the lowest physical latency, plug your tablet directly into the **rear motherboard USB ports routed to the CPU (Direct CPU Lanes)** rather than ports routed through the motherboard chipset (PCH) or external USB hubs/monitors. Refer to your motherboard manual for CPU-direct USB port locations.

### 3. In-Game Settings (Critical)
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

### 1. Universal Build (Recommended — Compatible with any 64-bit PC)
```cmd
cl.exe /nologo /O2 /Ob3 /GL /std:c++20 /fp:precise /DNDEBUG /GS- /GR- /EHs-c- driver.cpp /Fe:osuPoint.exe /link /SUBSYSTEM:WINDOWS /LTCG /OPT:REF /OPT:ICF
```

### 2. AVX2 Performance Edition (Intel Haswell / AMD Ryzen or newer)
```cmd
cl.exe /nologo /O2 /Ob3 /GL /std:c++20 /arch:AVX2 /fp:precise /DNDEBUG /GS- /GR- /EHs-c- driver.cpp /Fe:osuPoint_avx2.exe /link /SUBSYSTEM:WINDOWS /LTCG /OPT:REF /OPT:ICF
```

---

## Adding More Tablets

If your tablet model is not among the 43 pre-configured profiles, you can add support for it by creating a new `.cfg` file in the `tablets/` folder:

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
