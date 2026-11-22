#pragma once

#include <cstdint>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <cstddef>

namespace VendorID {
    constexpr uint16_t Wacom     = 0x056A;
    constexpr uint16_t Wacom2    = 0x0531;
    constexpr uint16_t XPPen     = 0x28BD;
    constexpr uint16_t UCLogic   = 0x5543;
    constexpr uint16_t Huion     = 0x256C;
    constexpr uint16_t Veikk1    = 0x2FEB;
    constexpr uint16_t Veikk2    = 0x2F88;
    constexpr uint16_t Artisul   = 0x2610;
    constexpr uint16_t Winbest   = 0x0416;
    constexpr uint16_t Ugee      = 0x2724;
    constexpr uint16_t Xencelabs = 0x34A4;
    constexpr uint16_t Waltop    = 0x172F;
    constexpr uint16_t Hanvon    = 0x0B57;
    constexpr uint16_t Genius    = 0x0458;

    constexpr bool IsWacom(uint16_t vid) noexcept {
        return vid == Wacom || vid == Wacom2;
    }
    constexpr bool IsXPPenFamily(uint16_t vid) noexcept {
        return vid == XPPen || vid == UCLogic || vid == Ugee || vid == Xencelabs;
    }
    constexpr bool IsHuionFamily(uint16_t vid) noexcept {
        return vid == Huion;
    }
    constexpr bool IsVeikk(uint16_t vid) noexcept {
        return vid == Veikk1 || vid == Veikk2;
    }
    constexpr bool IsKnownTabletVendor(uint16_t vid) noexcept {
        return IsWacom(vid) || IsXPPenFamily(vid) || IsHuionFamily(vid) || IsVeikk(vid) ||
               vid == Artisul || vid == Winbest || vid == Waltop || vid == Hanvon || vid == Genius;
    }
}

struct alignas(64) RawPacket {
    uint8_t  data[48];
    uint32_t len = 0;
    uint32_t endpointIdx = 0;
    uint64_t timestamp_ns = 0;
};
static_assert(sizeof(RawPacket) == 64, "RawPacket must fit in a single 64-byte cache line");

constexpr uint8_t PEN_FLAG_PRESSED    = 0x01;
constexpr uint8_t PEN_FLAG_IN_RANGE   = 0x02;
constexpr uint8_t PEN_FLAG_BARREL     = 0x04;
constexpr uint8_t PEN_FLAG_RAW_HW_TIP = 0x08;

struct PenSample {
    uint64_t timestamp_ns = 0;
    int32_t  raw_x = 0;
    int32_t  raw_y = 0;
    uint16_t pressure = 0;
    uint8_t  flags = 0;
    uint8_t  endpoint = 0;
};

enum class DecodeReason {
    Ok,
    WrongReportId,
    ShortReport,
    ControlPacket,
    LiftSignature,
    ZeroFrame
};

inline const char* DecodeReasonToString(DecodeReason r) noexcept {
    switch (r) {
        case DecodeReason::Ok: return "Ok";
        case DecodeReason::WrongReportId: return "WrongReportId";
        case DecodeReason::ShortReport: return "ShortReport";
        case DecodeReason::ControlPacket: return "ControlPacket";
        case DecodeReason::LiftSignature: return "LiftSignature";
        case DecodeReason::ZeroFrame: return "ZeroFrame";
        default: return "Unknown";
    }
}

template <typename T, size_t Capacity>
class SpscRing {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
public:
    SpscRing() : m_head(0), m_tailCache(0), m_tail(0), m_headCache(0) {}

    bool TryPush(const T& item) noexcept {
        const size_t head = m_head.load(std::memory_order_relaxed);
        if (head - m_tailCache >= Capacity) {
            m_tailCache = m_tail.load(std::memory_order_acquire);
            if (head - m_tailCache >= Capacity) {
                return false;
            }
        }
        m_buffer[head & (Capacity - 1)] = item;
        m_head.store(head + 1, std::memory_order_release);
        return true;
    }

    bool TryPush(const void* srcData, uint32_t len, uint32_t epIdx, uint64_t timestamp_ns) noexcept {
        const size_t head = m_head.load(std::memory_order_relaxed);
        if (head - m_tailCache >= Capacity) {
            m_tailCache = m_tail.load(std::memory_order_acquire);
            if (head - m_tailCache >= Capacity) {
                return false;
            }
        }
        T& dest = m_buffer[head & (Capacity - 1)];
        const uint32_t toCopy = (len > sizeof(dest.data)) ? static_cast<uint32_t>(sizeof(dest.data)) : len;
        if (srcData && toCopy > 0) {
            std::memcpy(dest.data, srcData, toCopy);
        }
        dest.len = toCopy;
        dest.endpointIdx = epIdx;
        dest.timestamp_ns = timestamp_ns;
        m_head.store(head + 1, std::memory_order_release);
        return true;
    }

    bool TryPop(T& item) noexcept {
        const size_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_headCache) {
            m_headCache = m_head.load(std::memory_order_acquire);
            if (tail == m_headCache) {
                return false;
            }
        }
        item = m_buffer[tail & (Capacity - 1)];
        m_tail.store(tail + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] const T* Peek() const noexcept {
        const size_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_headCache) {
            m_headCache = m_head.load(std::memory_order_acquire);
            if (tail == m_headCache) {
                return nullptr;
            }
        }
        return &m_buffer[tail & (Capacity - 1)];
    }

    [[nodiscard]] T* Peek() noexcept {
        const size_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_headCache) {
            m_headCache = m_head.load(std::memory_order_acquire);
            if (tail == m_headCache) {
                return nullptr;
            }
        }
        return &m_buffer[tail & (Capacity - 1)];
    }

    void Pop() noexcept {
        const size_t tail = m_tail.load(std::memory_order_relaxed);
        m_tail.store(tail + 1, std::memory_order_release);
    }

    [[nodiscard]] bool Empty() const noexcept {
        const size_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_headCache) {
            m_headCache = m_head.load(std::memory_order_acquire);
        }
        return tail == m_headCache;
    }

    [[nodiscard]] size_t Available() const noexcept {
        const size_t tail = m_tail.load(std::memory_order_relaxed);
        const size_t head = m_head.load(std::memory_order_acquire);
        m_headCache = head;
        return head - tail;
    }

private:
    alignas(64) std::atomic<size_t> m_head{ 0 };
    size_t m_tailCache{ 0 };

    alignas(64) std::atomic<size_t> m_tail{ 0 };
    mutable size_t m_headCache{ 0 };

    alignas(64) T m_buffer[Capacity];
};

struct RuntimeTabletSpec {
    char name[64] = "Wacom One CTL-472";
    uint16_t vid = 0x056A;
    uint16_t pid = 0x037A;
    int32_t max_x = 15200;
    int32_t max_y = 9500;
    double phys_w = 152.0;
    double phys_h = 95.0;
    int32_t report_len = 10;
    int32_t report_id = 0x02;
    int32_t x_offset = 2;
    int32_t y_offset = 4;
    int32_t coord_bits = 16;
    uint16_t tip_pressure_threshold = 0;
    bool is_mouse_mode = false;
    char init_output[32] = "";
    char init_feature[32] = "0x02 0x02";
};

struct BuiltinTabletDef {
    uint16_t vid;
    uint16_t pid;
    int max_x;
    int max_y;
    double phys_w;
    double phys_h;
    int report_len;
    uint8_t report_id;
    uint8_t x_offset;
    uint8_t y_offset;
    const char* name;
    const char* init_output;
    const char* init_feature;
};

static const BuiltinTabletDef s_builtinTablets[] = {
    // Wacom Models
    { 0x056A, 0x037A, 15200, 9500, 152.00, 95.00, 10, 0x02, 2, 4, "Wacom One CTL-472", nullptr, "0x02 0x02" },
    { 0x056A, 0x037B, 21600, 13500, 216.00, 135.00, 10, 0x02, 2, 4, "Wacom One CTL-672", nullptr, "0x02 0x02" },
    { 0x056A, 0x030E, 15200, 9500, 152.00, 95.00, 10, 0x02, 2, 4, "Wacom Intuos CTL-480", nullptr, "0x02 0x02" },
    { 0x056A, 0x033B, 15200, 9500, 152.00, 95.00, 10, 0x02, 2, 4, "Wacom Intuos CTL-490", nullptr, "0x02 0x02" },
    { 0x056A, 0x0374, 15200, 9500, 152.00, 95.00, 192, 0x10, 2, 5, "Wacom Intuos CTL-4100", nullptr, "0x02 0x02" },
    { 0x056A, 0x0375, 21600, 13500, 216.00, 135.00, 192, 0x10, 2, 5, "Wacom Intuos CTL-6100", nullptr, "0x02 0x02" },
    { 0x056A, 0x0300, 15200, 9500, 152.00, 95.00, 10, 0x02, 2, 4, "Wacom Bamboo CTL-471", nullptr, "0x02 0x02" },
    { 0x056A, 0x0301, 21600, 13500, 216.00, 135.00, 10, 0x02, 2, 4, "Wacom Bamboo CTL-671", nullptr, "0x02 0x02" },
    { 0x056A, 0x0323, 21600, 13500, 216.00, 135.00, 10, 0x02, 2, 4, "Wacom Intuos CTL-680", nullptr, "0x02 0x02" },
    { 0x056A, 0x033D, 21600, 13500, 216.00, 135.00, 10, 0x02, 2, 4, "Wacom Intuos CTL-690", nullptr, "0x02 0x02" },
    { 0x056A, 0x03F5, 75536, 47210, 159.60, 99.75, 18, 0x10, 2, 5, "Wacom Intuos Pro S", nullptr, nullptr },
    { 0x056A, 0x03F6, 89600, 59200, 224.00, 148.00, 18, 0x10, 2, 5, "Wacom Intuos Pro M", nullptr, nullptr },
    // Huion & Gaomon Models
    { 0x256C, 0x006D, 33020, 20320, 165.10, 101.60, 12, 0x08, 2, 4, "Gaomon S620", nullptr, nullptr },
    { 0x256C, 0x006E, 20480, 12790, 101.60, 56.60, 8, 0x07, 2, 4, "Huion H420", nullptr, nullptr },
    { 0x256C, 0x006D, 24384, 15240, 121.92, 76.20, 12, 0x08, 2, 4, "Huion H430P", nullptr, nullptr },
    { 0x256C, 0x006D, 32000, 20000, 160.00, 100.00, 12, 0x08, 2, 4, "Huion H640P", nullptr, nullptr },
    { 0x256C, 0x006D, 32000, 20000, 160.00, 101.60, 12, 0x08, 2, 4, "Huion HS64", nullptr, nullptr },
    { 0x256C, 0x006E, 42400, 26500, 106.00, 66.00, 12, 0x08, 2, 4, "Huion H420X", nullptr, nullptr },
    { 0x256C, 0x006E, 64000, 40000, 203.20, 127.00, 12, 0x08, 2, 4, "Huion H580X", nullptr, nullptr },
    // XP-Pen & UC-Logic Models
    { 0x28BD, 0x0075, 45720, 29210, 101.60, 76.20, 8, 0x02, 2, 4, "XP-Pen Star G430", "0x02 0xB0 0x02", nullptr },
    { 0x28BD, 0x0913, 20320, 15240, 101.60, 76.20, 14, 0x02, 2, 4, "XP-Pen Star G430S", "0x02 0xB0 0x04", nullptr },
    { 0x28BD, 0x0914, 15999, 9999, 160.00, 100.00, 12, 0x02, 2, 4, "XP-Pen Star G640", "0x02 0xB0 0x04", nullptr },
    { 0x28BD, 0x0906, 32999, 20599, 165.00, 103.00, 12, 0x02, 2, 4, "XP-Pen Star G640S", "0x02 0xB0 0x04", nullptr },
    { 0x5543, 0x0081, 10000, 7500, 100.00, 75.00, 8, 0x02, 2, 4, "UC-Logic Tablet", nullptr, nullptr },
    // VEIKK Models
    { 0x2FEB, 0x0001, 30480, 20320, 152.40, 101.60, 9, 0x02, 3, 5, "VEIKK S640", nullptr, nullptr },
    { 0x2FEB, 0x0002, 50800, 30480, 254.00, 152.40, 9, 0x02, 3, 5, "VEIKK A30", nullptr, nullptr },
    { 0x2FEB, 0x0004, 50800, 30480, 254.00, 152.40, 9, 0x02, 3, 5, "VEIKK A15", nullptr, nullptr },
};
constexpr size_t s_builtinTabletsCount = sizeof(s_builtinTablets) / sizeof(s_builtinTablets[0]);

inline bool FindBuiltinTablet(uint16_t vid, uint16_t pid, RuntimeTabletSpec& out_spec) noexcept {
    for (size_t i = 0; i < s_builtinTabletsCount; ++i) {
        if (s_builtinTablets[i].vid == vid && s_builtinTablets[i].pid == pid) {
            const auto& t = s_builtinTablets[i];
            size_t n = std::min(std::strlen(t.name), sizeof(out_spec.name) - 1);
            std::memcpy(out_spec.name, t.name, n);
            out_spec.name[n] = '\0';
            out_spec.vid = t.vid;
            out_spec.pid = t.pid;
            out_spec.max_x = t.max_x;
            out_spec.max_y = t.max_y;
            out_spec.phys_w = t.phys_w;
            out_spec.phys_h = t.phys_h;
            out_spec.report_len = t.report_len;
            out_spec.report_id = t.report_id;
            out_spec.x_offset = t.x_offset;
            out_spec.y_offset = t.y_offset;
            out_spec.coord_bits = (t.y_offset == 5 && VendorID::IsWacom(vid)) ? 24 : 16;
            out_spec.tip_pressure_threshold = 0;
            out_spec.is_mouse_mode = false;
            if (t.init_feature) {
                std::strncpy(out_spec.init_feature, t.init_feature, sizeof(out_spec.init_feature) - 1);
            } else {
                out_spec.init_feature[0] = '\0';
            }
            if (t.init_output) {
                std::strncpy(out_spec.init_output, t.init_output, sizeof(out_spec.init_output) - 1);
            } else {
                out_spec.init_output[0] = '\0';
            }
            return true;
        }
    }

    // Generic fallback for known vendor
    if (VendorID::IsWacom(vid)) {
        std::snprintf(out_spec.name, sizeof(out_spec.name), "Generic Wacom (0x%04X:0x%04X)", vid, pid);
        out_spec.vid = vid;
        out_spec.pid = pid;
        out_spec.max_x = 15200;
        out_spec.max_y = 9500;
        out_spec.phys_w = 152.0;
        out_spec.phys_h = 95.0;
        out_spec.report_len = 10;
        out_spec.report_id = 0x02;
        out_spec.x_offset = 2;
        out_spec.y_offset = 4;
        out_spec.coord_bits = 16;
        return true;
    }
    if (VendorID::IsHuionFamily(vid)) {
        std::snprintf(out_spec.name, sizeof(out_spec.name), "Generic Huion (0x%04X:0x%04X)", vid, pid);
        out_spec.vid = vid;
        out_spec.pid = pid;
        out_spec.max_x = 32000;
        out_spec.max_y = 20000;
        out_spec.phys_w = 160.0;
        out_spec.phys_h = 100.0;
        out_spec.report_len = 12;
        out_spec.report_id = 0x08;
        out_spec.x_offset = 2;
        out_spec.y_offset = 4;
        out_spec.coord_bits = 16;
        return true;
    }
    if (VendorID::IsXPPenFamily(vid)) {
        std::snprintf(out_spec.name, sizeof(out_spec.name), "Generic XP-Pen (0x%04X:0x%04X)", vid, pid);
        out_spec.vid = vid;
        out_spec.pid = pid;
        out_spec.max_x = 32767;
        out_spec.max_y = 32767;
        out_spec.phys_w = 160.0;
        out_spec.phys_h = 100.0;
        out_spec.report_len = 10;
        out_spec.report_id = 0x02;
        out_spec.x_offset = 2;
        out_spec.y_offset = 4;
        out_spec.coord_bits = 16;
        return true;
    }
    if (VendorID::IsVeikk(vid)) {
        std::snprintf(out_spec.name, sizeof(out_spec.name), "Generic VEIKK (0x%04X:0x%04X)", vid, pid);
        out_spec.vid = vid;
        out_spec.pid = pid;
        out_spec.max_x = 30480;
        out_spec.max_y = 20320;
        out_spec.phys_w = 152.4;
        out_spec.phys_h = 101.6;
        out_spec.report_len = 9;
        out_spec.report_id = 0x02;
        out_spec.x_offset = 3;
        out_spec.y_offset = 5;
        out_spec.coord_bits = 16;
        return true;
    }

    return false;
}
