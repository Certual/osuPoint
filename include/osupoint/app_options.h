#pragma once

#include "osupoint/driver_service.h"
#include <csignal>
#include <cstdlib>
#include <cerrno>

namespace osupoint {

struct AppOptions {
    std::string config_path, device_path;
    std::string platform = "auto";
    uint16_t vendor_id = 0, product_id = 0;
    bool headless = false, force_gui = false, preview = false;
    bool mouse_mode = true, mouse_override = false, debug = false, debug_override = false;
    int spin = 0, core = -1;
    bool spin_override = false;
    double run_for = 0;
};

inline int ParseOptions(int argc, char** argv, AppOptions& options) {
    options.headless = std::string(argv[0]).find("osupointd") != std::string::npos;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--version" || arg == "-v") { std::printf("osuPoint %s (IPC %u)\n", OSUPOINT_VERSION, OSUPOINT_IPC_VERSION); return 1; }
        if (arg == "--help" || arg == "-h") {
            std::printf("osuPoint %s\n--gui | --daemon | --headless\n--config <path>  --device <hidraw>\n"
                        "--mouse | --stylus   --debug | --no-debug\n--spin <0..100> (bounded 0..200 us spin)\n"
                        "--vid <hex> --pid <hex> (device selection filters)\n"
                        "--core <-1..1023> (default -1: automatic CPU affinity)\n"
                        "--platform <auto|x11|wayland> (GLFW 3.4+)\n"
                        "--preview (GUI without hardware/IPC)  --run-for <seconds>\n", OSUPOINT_VERSION);
            return 1;
        }
        if (arg == "--daemon" || arg == "--headless" || arg == "-d") options.headless = true;
        else if (arg == "--gui") { options.force_gui = true; options.headless = false; }
        else if (arg == "--preview") { options.preview = true; options.force_gui = true; options.headless = false; }
        else if (arg == "--mouse" || arg == "--stylus") { options.mouse_override = true; options.mouse_mode = arg == "--mouse"; }
        else if (arg == "--debug" || arg == "--no-debug") { options.debug_override = true; options.debug = arg == "--debug"; }
        else if (arg == "--config" || arg == "--device" || arg == "--platform" || arg == "--vid" || arg == "--pid" || arg == "--spin" || arg == "--core" ||
                 arg == "--core-reader" || arg == "--core-pipeline" || arg == "--run-for") {
            if (++i >= argc) { std::fprintf(stderr, "Missing value for %s\n", arg.c_str()); return -1; }
            const std::string value = argv[i];
            if (arg == "--config") options.config_path = value;
            else if (arg == "--device") options.device_path = value;
            else if (arg == "--platform") {
                if (value != "auto" && value != "x11" && value != "wayland") return -1;
                options.platform = value;
            } else if (arg == "--vid" || arg == "--pid") {
                char* end = nullptr;
                const unsigned long number = std::strtoul(value.c_str(), &end, 16);
                if (end == value.c_str() || *end || number > 65535) return -1;
                if (arg == "--vid") options.vendor_id = static_cast<uint16_t>(number);
                else options.product_id = static_cast<uint16_t>(number);
            }
            else {
                char* end = nullptr;
                const double number = std::strtod(value.c_str(), &end);
                if (end == value.c_str() || *end || !std::isfinite(number)) return -1;
                if (arg == "--run-for") { if (number < 0 || number > 86400) return -1; options.run_for = number; }
                else if (arg == "--spin") {
                    if (number < 0 || number > 100 || number != std::floor(number)) return -1;
                    options.spin = static_cast<int>(number); options.spin_override = true;
                } else {
                    if (number < -1 || number >= 1024 || number != std::floor(number)) return -1;
                    options.core = static_cast<int>(number);
                }
            }
        } else { std::fprintf(stderr, "Unknown option: %s\n", arg.c_str()); return -1; }
    }
    if (options.config_path.empty()) {
        const std::string xdg = ConfigManager::GetDefaultConfigPath();
        // Preserve the existing legacy config, but never silently fall back for
        // an explicitly named file. New installs use XDG rather than the CWD.
        options.config_path = access(xdg.c_str(), R_OK) == 0 ? xdg
            : access("config.ini", R_OK) == 0 ? "config.ini" : xdg;
    }
    return 0;
}

inline void ApplyOverrides(const AppOptions& options, OsuPointConfig& cfg) {
    if (options.mouse_override) cfg.mouse_mode = options.mouse_mode;
    if (options.spin_override) cfg.spin_percent = options.spin;
    if (options.debug_override) cfg.debug_mode = options.debug;
}

inline std::atomic<bool> stop_requested{false};
static_assert(std::atomic<bool>::is_always_lock_free, "Signal handler requires lock-free atomic store");
inline void InstallStopHandlers() {
    struct sigaction action{};
    action.sa_handler = [](int) { stop_requested.store(true, std::memory_order_relaxed); };
    sigemptyset(&action.sa_mask);
    for (int signal : {SIGINT, SIGTERM, SIGHUP}) sigaction(signal, &action, nullptr);
    signal(SIGPIPE, SIG_IGN);
}

inline int RunHeadless(const AppOptions& options) {
    InstallStopHandlers();
    OsuPointConfig cfg;
    ConfigManager::LoadFromFile(options.config_path, cfg);
    ApplyOverrides(options, cfg);
    TransformConfig initial;
    cfg.ToTransformConfig(initial);
    auto& driver = DriverService::Instance();
    if (!driver.Start(options.config_path, cfg.mouse_mode, cfg.spin_percent, -1, options.core, options.device_path, &initial,
                      options.vendor_id, options.product_id)) {
        std::fprintf(stderr, "Cannot start osuPoint: %s\n", std::strerror(errno)); return 1;
    }
    const uint64_t start = GetMonotonicTimeNs();
    while (!stop_requested.load(std::memory_order_relaxed) && driver.IsRunning()) {
        if (options.run_for > 0 && (GetMonotonicTimeNs() - start) * 1e-9 >= options.run_for) break;
        usleep(100000);
    }
    driver.Stop();
    return 0;
}

} // namespace osupoint
