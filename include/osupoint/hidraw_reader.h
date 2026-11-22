#pragma once

#include "osupoint/tablet_spec.h"
#include <atomic>
#include <cstdint>
#include <cstddef>

struct DiscoveredTablet {
    char hidraw_path[64] = {0};
    char evdev_path[64] = {0};
    char device_name[128] = {0};
    uint16_t vid = 0;
    uint16_t pid = 0;
    uint32_t bustype = 0;
    RuntimeTabletSpec spec;
    bool found = false;
};

class HidrawReader {
public:
    HidrawReader() noexcept;
    ~HidrawReader() noexcept;

    HidrawReader(const HidrawReader&) = delete;
    HidrawReader& operator=(const HidrawReader&) = delete;

    static bool DiscoverTablet(
        DiscoveredTablet& out_tablet,
        const char* explicit_device = nullptr,
        uint16_t filter_vid = 0,
        uint16_t filter_pid = 0) noexcept;

    bool Open(const DiscoveredTablet& tablet) noexcept;
    void Close() noexcept;

    [[nodiscard]] bool IsOpen() const noexcept { return m_hidraw_fd.load(std::memory_order_relaxed) >= 0; }
    [[nodiscard]] int GetFd() const noexcept { return m_hidraw_fd.load(std::memory_order_relaxed); }
    [[nodiscard]] const DiscoveredTablet& GetTablet() const noexcept { return m_tablet; }
    [[nodiscard]] int GetInitializationError() const noexcept { return m_init_error; }
    [[nodiscard]] bool IsEvdevGrabbed() const noexcept { return m_evdev_grabbed; }

private:
    static bool FindMatchingEvdev(const char* hidraw_node_name, uint16_t vid, uint16_t pid, char* out_evdev, size_t out_size) noexcept;

    std::atomic<int> m_hidraw_fd{ -1 };
    int m_evdev_grab_fd = -1;
    DiscoveredTablet m_tablet;
    int m_init_error = 0;
    bool m_evdev_grabbed = false;
};
