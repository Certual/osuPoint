<div align="center">
  <img src="logo.svg" alt="osu!Point Logo" width="128" />

  # osu!Point (Linux Edition)
  
  A modern, ultra-low latency kernel-mode tablet driver for osu!, digital art, and everyday pen input on Linux.

  [![release](https://img.shields.io/github/v/release/Certual/osuPoint?style=flat-square)](https://github.com/Certual/osuPoint/releases/latest)
  [![build](https://img.shields.io/github/actions/workflow/status/Certual/osuPoint/build.yml?branch=Linux&style=flat-square&label=build)](https://github.com/Certual/osuPoint/actions)
  [![platform](https://img.shields.io/badge/platform-Linux-E95420?style=flat-square&logo=linux&logoColor=white)](https://github.com/Certual/osuPoint/tree/Linux)
  [![license](https://img.shields.io/github/license/Certual/osuPoint?style=flat-square)](https://github.com/Certual/osuPoint/blob/Linux/LICENSE)
  [![c++](https://img.shields.io/badge/C%2B%2B-20-00599C?style=flat-square&logo=c%2B%2B)](https://github.com/Certual/osuPoint)
</div>

## About

**osu!Point Linux Edition** is a pure C++20, kernel-mode direct-HID tablet driver designed from first principles to achieve the lowest possible input latency on Linux. It completely bypasses the standard X11, Wayland, and `libinput` event pipelines by capturing raw USB HID packets directly from `/dev/hidraw*` and injecting absolute subpixel coordinates straight into `/dev/uinput`.

## Performance & Architecture

In synthetic and real-world testing against OpenTabletDriver and `xf86-input-wacom`, osu!Point demonstrates sub-microsecond software pipeline latency and zero jitter.

### Synthetic Loopback Benchmark (100,000 Packets)

| Metric | Result | Benchmark Target |
|---|---|---|
| **Sustained Throughput** | **1,792,053 packets/sec** | $> 100\ 000\text{ pps}$ |
| **Median Latency ($p50$)** | **$< 1\ \mu\text{s}$ ($0\ \mu\text{s}$)** | $< 100\ \mu\text{s}$ |
| **99th Percentile ($p99$)** | **$0\ \mu\text{s}$** | $< 500\ \mu\text{s}$ |
| **Maximum Jitter** | **$44\ \mu\text{s}$** | $< 1000\ \mu\text{s}$ |
| **Test Suite Pass Rate** | **116 / 116 (100%)** | $100\%$ |

Low latency is achieved through:
- **Direct Kernel HID Polling via `/dev/hidraw*`:** Bypasses X11 event queues, Wayland compositors, and `libinput` smoothing layers, acquiring hardware reports at the USB controller boundary.
- **Direct Coordinate Injection via `/dev/uinput`:** Creates a high-resolution absolute virtual input device and emits pointer events with instant `SYN_REPORT` synchronization.
- **`SCHED_FIFO` Real-Time Scheduling:** The dedicated input driver thread is elevated to Linux real-time FIFO priority (`sched_priority = 98`), preventing task preemption by background user-space processes.
- **Physical Memory Page Locking (`mlockall`):** Invokes `mlockall(MCL_CURRENT | MCL_FUTURE)` on driver startup, eliminating page fault delays and swap latency entirely.
- **Lock-Free SPSC Ring & POSIX Seqlock IPC:** Inter-thread communication uses a 64-byte cache-line aligned single-producer single-consumer ring buffer (`SpscRing<RawPacket, 64>`). GUI and driver communicate via `/dev/shm` shared memory using a seqlock protocol (`< 1 \mu\text{s}` parameter updates with zero process restarts).
- **SIMD-Accelerated Transformation Pipeline:** Coordinate transformation, aspect ratio scaling, rotation matrices, and boundary clipping are accelerated via AVX2+FMA, AVX, or SSE2 vector instructions.
- **Soft-Knee Quadratic 1-Euro Anti-Noise Filter:** Normalized screen velocity curve ($v_{knee} = 45\text{ px/s}$, $gain = 25$) with 6ms reversal hold, providing noise-free hover tracking with instantaneous response on rapid flicks.
- **Adaptive Kinematic Extrapolation:** Sub-millisecond lookahead ($\tau \in [1.0, 5.0]\text{ ms}$) with turnaround overshoot clamping.
- **Zero Idle CPU Footprint:** The graphical interface uses Dear ImGui + GLFW/OpenGL 3.3 with event-based throttling (`glfwWaitEventsTimeout`), consuming **0.0% CPU** in the background.

## Hardware Support

The driver includes built-in specifications for over 160 tablet models across Wacom, XP-Pen, Huion, Gaomon, VEIKK, UC-Logic, Artisul, and Ugee:
- Automatic 16-bit, 17-bit, 18-bit, and 24-bit coordinate decoding.
- Automatic Huion / Gaomon / XP-Pen binary String Descriptor `200` interrogation to read exact hardware `max_x`, `max_y`, and `LPI` on shared-PID controllers.
- Multi-endpoint monitoring with automatic vendor wake-up handshakes.

Devices physically verified by the author and community:
- Wacom One (`CTL-472`, `CTC-4110WL`, `CTC-6110WL` Gen 2)
- Wacom Bamboo (`CTL-471`)
- Wacom Intuos S / M (`CTL-4100` / `CTL-6100`, including WL/Bluetooth variants)
- Wacom Intuos Pro (`PTH-460`, `PTH-660`, `PTH-860`)
- XP-Pen Star (`G430S`, `G640`, `G640S`, `G960`, `G960S Plus`)
- Xencelabs Pen Tablet (Small / Medium)
- Huion Inspiroy series (`H420`, `H420X`, `H430P`, `H580X`, `H640P`, `HS64`, `RTE-100`, `RTS-300`)
- Gaomon series (`S620`, `S630`, `M106K`)

If you test another tablet model, open an issue on GitHub with your results or `--probe` output.

## Features

- Standalone executable with static C++20 linkage, consuming under 15 MB of RAM.
- **Area Tab:** Interactive area preview canvas with drag-to-move/resize handles, aspect ratio lock (`16:9`, `Area`, `Screen`), rotation ($0^\circ$–$360^\circ$), boundary clipping, and pen tip click toggle.
- **Settings Tab:** Multi-monitor selection, theme customization, card opacity, antialiasing toggle, Kinematic Extrapolation controls, Latency Spin slider, and live coordinate monitor.
- **Dual Operating Modes:**
  - **Unified Mode (`osupoint`):** Launches GUI and manages the real-time driver embedded inside.
  - **Headless Daemon Mode (`osupointd` / `osupoint --daemon`):** Minimalist CLI daemon suitable for systemd services or low-overhead background execution.

## Quick Start & Installation

### Portable Execution (No Installation Required)

1. Download and extract the archive for your CPU architecture (`osuPoint-linux-avx2.tar.gz`, `avx`, or `sse`).
2. Run the portable launcher:
   ```bash
   ./start.sh
   ```
   *Note: If your user does not have permission to access `/dev/uinput`, `start.sh` will prompt to add you to the `input` group or run with temporary elevation.*

### System-Wide Installation

To install osuPoint binaries, udev permission rules, desktop launchers, and systemd services:

```bash
sudo ./install.sh
```

To remove osuPoint from your system:

```bash
sudo ./uninstall.sh
```

## CLI Reference

```bash
osupoint                      # Launch unified GUI with embedded driver
osupoint --daemon             # Run as headless background driver
osupoint --config <path>      # Specify custom configuration file (default: config.ini)
osupoint --monitor <index>    # Target monitor index
osupoint --list-devices       # List detected tablet HID devices
osupoint --probe              # Inspect connected hardware descriptors
osupoint --selftest           # Run hardware golden vector decode suite
```

## Configuration (`config.ini`)

Settings are stored in `config.ini` next to the executable and hot-reloaded automatically:

```ini
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

# Building from Source

### Dependencies

On Arch Linux / EndeavourOS:
```bash
sudo pacman -S --needed base-devel glfw
```

On Ubuntu / Debian:
```bash
sudo apt-get install -y build-essential libglfw3-dev libgl1-mesa-dev libx11-dev libsystemd-dev zip
```

### Build Targets

```bash
make sse          # Build SSE2 Baseline (build/osupoint-sse)
make avx          # Build AVX Baseline (build/osupoint-avx)
make avx2         # Build AVX2 Optimized (build/osupoint-avx2)
make test         # Run core, GUI math, regressions, and E2E tests
make package-all  # Compile all architectures and package .tar.gz / .zip releases
```

### Compiler Optimization Flags

The Linux port is compiled with strict latency-focused optimization flags:

```bash
-std=c++20 -O3 -flto -fno-exceptions -fno-rtti -fomit-frame-pointer -fno-stack-protector -ffast-math -fno-math-errno -Wall -Wextra -Wpedantic -DNDEBUG -D_GNU_SOURCE
```

- `-O3 -flto`: Maximum aggressive optimization and Whole Program Link-Time Optimization.
- `-fno-exceptions -fno-rtti`: Completely strips C++ exception tables and runtime type information overhead.
- `-fomit-frame-pointer`: Frees up register `RBP` for hot SIMD transformation loops.
- `-fno-stack-protector`: Eliminates stack canary checks on hot packet decode routines.
- `-ffast-math -fno-math-errno`: Enables aggressive IEEE-754 fast math and strips errno overhead.

# License

GPL-3.0
