#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <cstring>
#include <algorithm>
#include <ostream>

namespace VendorID {
    constexpr uint16_t Wacom      = 0x056A;
    constexpr uint16_t Wacom2     = 0x0531;
    constexpr uint16_t XPPen      = 0x28BD;
    constexpr uint16_t UCLogic    = 0x5543;
    constexpr uint16_t Huion      = 0x256C;
    constexpr uint16_t Veikk1     = 0x2FEB;
    constexpr uint16_t Veikk2     = 0x2F88;
    constexpr uint16_t Artisul    = 0x2610;
    constexpr uint16_t Winbest    = 0x0416;
    constexpr uint16_t Ugee       = 0x2724;
    constexpr uint16_t Xencelabs  = 0x34A4;
    constexpr uint16_t Waltop     = 0x172F;
    constexpr uint16_t Hanvon     = 0x0B57;
    constexpr uint16_t Genius     = 0x0458;

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
        return vid == XPPen || vid == UCLogic || vid == Huion ||
               vid == Veikk1 || vid == Veikk2 || vid == Wacom || vid == Wacom2 ||
               vid == Artisul || vid == Winbest || vid == Ugee ||
               vid == Xencelabs || vid == Waltop || vid == Hanvon || vid == Genius;
    }
}

struct PenSample {
    uint64_t qpc = 0;
    int32_t  raw_x = 0;
    int32_t  raw_y = 0;
    uint16_t pressure = 0;
    uint8_t  flags = 0;    // Bit 0: tip pressed, Bit 1: in range, Bit 2: barrel, Bit 3: raw_hw_tip
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

inline std::ostream& operator<<(std::ostream& os, DecodeReason r) {
    return os << DecodeReasonToString(r);
}

struct RuntimeTabletSpec {
    char     name[64] = "Wacom One CTL-472";
    uint16_t vid = 0x056A;
    uint16_t pid = 0x037A;
    int32_t  max_x = 15200;
    int32_t  max_y = 9500;
    double   phys_w = 152.0;
    double   phys_h = 95.0;
    int32_t  report_len = 10;
    int32_t  report_id = 0x02;
    int32_t  x_offset = 2;
    int32_t  y_offset = 4;
    int32_t  coord_bits = 16;
    uint16_t tip_pressure_threshold = 0;
    bool     is_mouse_mode = false;
};

struct TabletSpec {
    std::string name = "Wacom One CTL-472";
    uint16_t vid = 0x056A;
    uint16_t pid = 0x037A;
    int32_t  max_x = 15200;
    int32_t  max_y = 9500;
    double   phys_w = 152.0;
    double   phys_h = 95.0;
    int32_t  report_len = 10;
    int32_t  report_id = 0x02;
    int32_t  x_offset = 2;
    int32_t  y_offset = 4;
    bool     is_mouse_mode = false;
    int32_t  coord_bits = 16;
    uint16_t tip_pressure_threshold = 0;

    RuntimeTabletSpec ToRuntimeSpec() const noexcept {
        RuntimeTabletSpec r{};
        size_t n = (std::min)(name.size(), sizeof(r.name) - 1);
        std::memcpy(r.name, name.data(), n);
        r.name[n] = '\0';
        r.vid = vid;
        r.pid = pid;
        r.max_x = max_x;
        r.max_y = max_y;
        r.phys_w = phys_w;
        r.phys_h = phys_h;
        r.report_len = report_len;
        r.report_id = report_id;
        r.x_offset = x_offset;
        r.y_offset = y_offset;
        r.coord_bits = coord_bits;
        r.tip_pressure_threshold = tip_pressure_threshold;
        r.is_mouse_mode = is_mouse_mode;
        return r;
    }
};

static inline uint16_t ReadLE16(const uint8_t* p) noexcept {
    uint16_t val;
    std::memcpy(&val, p, sizeof(val));
    return val;
}

static inline uint32_t ReadLE32(const uint8_t* p) noexcept {
    uint32_t val;
    std::memcpy(&val, p, sizeof(val));
    return val;
}

static inline uint32_t ReadLE_N(const uint8_t* p, int bits) noexcept {
    if (bits <= 8) return p[0] & ((1u << bits) - 1);
    if (bits <= 16) {
        uint32_t v = p[0] | (p[1] << 8);
        return bits == 16 ? v : (v & ((1u << bits) - 1));
    }
    uint32_t v = p[0] | (p[1] << 8) | (p[2] << 16);
    return bits >= 24 ? v : (v & ((1u << bits) - 1));
}

static inline uint32_t ReadCoord(const uint8_t* p, int bits) noexcept {
    if (bits == 16) {
        return ReadLE16(p);
    }
    return ReadLE_N(p, bits);
}

inline DecodeReason DecodeSample(const uint8_t* report, int reportSize, const RuntimeTabletSpec& spec, PenSample& out) noexcept {
    if (!report || reportSize < 4) return DecodeReason::ShortReport;
    const size_t rSize = static_cast<size_t>(reportSize);

    if (spec.is_mouse_mode) {
        int btnByte = 0;
        int xByte = 1;
        int yByte = 3;
        if (spec.report_id != 0) {
            if (report[0] != spec.report_id) return DecodeReason::WrongReportId;
            btnByte = 1;
            xByte = 2;
            yByte = 4;
        }
        if (spec.x_offset > 0 && spec.y_offset > 0) {
            xByte = spec.x_offset;
            yByte = spec.y_offset;
        }
        if (reportSize < (std::max)(xByte + 2, yByte + 2)) return DecodeReason::ShortReport;

        uint8_t btns = report[btnByte];
        bool is_pressed = (btns & 0x01) != 0;
        bool barrel = (btns & 0x02) != 0;
        int raw_x = ReadLE16(&report[xByte]);
        int raw_y = ReadLE16(&report[yByte]);

        out.raw_x = raw_x;
        out.raw_y = raw_y;
        out.pressure = is_pressed ? 2047 : 0;
        out.flags = (is_pressed ? 1 : 0) | 2 | (barrel ? 4 : 0);
        return DecodeReason::Ok;
    }

    auto IsZeroSpan = [](const uint8_t* p, int start, int end) noexcept {
        for (int i = start; i < end; ++i) if (p[i] != 0) return false;
        return true;
    };

    if (VendorID::IsXPPenFamily(spec.vid) || VendorID::IsVeikk(spec.vid) || VendorID::IsWacom(spec.vid)) {
        bool isLift = false;
        if (reportSize > 1) {
            if (report[1] == 0xC0 && (!VendorID::IsWacom(spec.vid) || IsZeroSpan(report, 2, (std::min)(reportSize, 9)))) {
                isLift = true;
            } else if (report[1] == 0x80 && IsZeroSpan(report, 2, (std::min)(reportSize, 9))) {
                isLift = true;
            }
        }
        if (VendorID::IsWacom(spec.vid) && reportSize > 0 && (report[0] == 0xC0 || report[0] == 0x80)) {
            if (report[0] == 0x80 || IsZeroSpan(report, 1, (std::min)(reportSize, 9))) {
                isLift = true;
            }
        }
        if (isLift) return DecodeReason::LiftSignature;
    }

    int raw_x = 0, raw_y = 0;
    bool is_pressed = false;
    bool in_range = true;
    bool barrel = false;
    uint16_t pressure = 0;

    if (VendorID::IsWacom(spec.vid)) {
        bool valid = false;
        if (reportSize > 0) {
            if (report[0] == spec.report_id) { valid = true; }
            else if ((spec.report_id == 0x02 || spec.report_id == 0x10) && (report[0] == 0x10 || report[0] == 0x1E || report[0] == 0xC2 || report[0] == 0x82)) { valid = true; }
        }
        if (!valid) return DecodeReason::WrongReportId;
        const int xo = spec.x_offset;
        const int yo = spec.y_offset;
        const int eff_yo = (report[0] == 0x10 && reportSize >= 18 && yo == 4) ? 5 : yo;
        const int coord_bytes = (spec.coord_bits > 16) ? 3 : 2;
        const size_t min_len = static_cast<size_t>((std::max)(xo + coord_bytes, eff_yo + coord_bytes));
        if (rSize < min_len) return DecodeReason::ShortReport;

        bool inRangeOrActive = ((report[1] & 0xC0) != 0) ||
                               ((report[1] & 0x07) != 0) ||
                               (reportSize >= 6 && ReadLE32(&report[2]) != 0);
        if (inRangeOrActive) {
            const bool is24bit = ((spec.coord_bits == 24) || (xo == 2 && eff_yo == 5 && reportSize >= 10)) &&
                                 (rSize >= static_cast<size_t>(eff_yo + 3));
            if (is24bit) {
                raw_x = ReadLE_N(&report[2], 24);
                raw_y = ReadLE_N(&report[eff_yo], 24);
                if (reportSize >= 10) pressure = ReadLE16(&report[8]) & 0x3FFF;
            } else if (xo == 2 && eff_yo == 4 && spec.coord_bits == 16) {
                uint32_t xy = ReadLE32(&report[2]);
                raw_x = static_cast<int>(xy & 0xFFFF);
                raw_y = static_cast<int>(xy >> 16);
                if (reportSize >= 8) pressure = ReadLE16(&report[6]) & 0x1FFF;
            } else {
                raw_x = ReadCoord(&report[xo], spec.coord_bits);
                raw_y = ReadCoord(&report[eff_yo], spec.coord_bits);
                const int press_offset = (eff_yo >= 5) ? (eff_yo + 3) : 6;
                if (reportSize >= press_offset + 2) pressure = ReadLE16(&report[press_offset]) & 0x1FFF;
            }
            is_pressed = (report[1] & 0x01) != 0;
            barrel = (report[1] & 0x06) != 0;
            in_range = true;
        } else {
            in_range = false;
            is_pressed = false;
            raw_x = 0;
            raw_y = 0;
        }
    }
    else if (VendorID::IsVeikk(spec.vid) || (reportSize >= 3 && (report[1] == 0x41 || report[1] == 0x42 || report[1] == 0x43))) {
        if (reportSize >= 3 && (report[1] == 0x42 || report[1] == 0x43)) {
            return DecodeReason::ControlPacket;
        }
        if (reportSize >= 3 && report[1] == 0x41 && (report[2] & 0xF0) != 0xA0) {
            return DecodeReason::LiftSignature;
        }
        if (reportSize < 7) {
            return DecodeReason::ShortReport;
        }
        if (report[1] == 0x41 && (report[2] & 0xF0) == 0xA0) {
            raw_x = ReadLE16(&report[3]);
            raw_y = ReadLE16(&report[5]);
            is_pressed = (report[2] & 0x01) != 0;
            barrel = (report[2] & 0x02) != 0;
            if (reportSize >= 9) {
                pressure = ReadLE16(&report[7]);
            }
        }
    }
    else if (VendorID::IsHuionFamily(spec.vid) && (report[0] == spec.report_id || report[0] == 0x08 || report[0] == 0x07 || report[0] == 0x09 || report[0] == 0x0A || report[0] == 0x0E)) {
        if (reportSize >= 2 && ((report[1] & 0xF0) == 0xE0 || (report[1] & 0xF0) == 0xF0)) {
            return DecodeReason::ControlPacket;
        }
        if (reportSize < 6) {
            return DecodeReason::ShortReport;
        }
        raw_x = ReadLE16(&report[2]);
        raw_y = ReadLE16(&report[4]);
        if (spec.coord_bits > 16) {
            if (reportSize >= 10 && report[9] != 0) {
                raw_x |= (static_cast<int>(report[8]) << 16);
                raw_y |= (static_cast<int>(report[9]) << 16);
            } else if (reportSize >= 9) {
                if (report[8] & 0x01) raw_x |= (1 << 16);
                if (report[8] & 0x02) raw_y |= (1 << 16);
                if (spec.coord_bits >= 18) {
                    if (report[8] & 0x04) raw_x |= (1 << 17);
                    if (report[8] & 0x08) raw_y |= (1 << 17);
                }
            }
        }
        is_pressed = (report[1] & 0x01) != 0;
        barrel = (report[1] & 0x02) != 0;
        if (reportSize >= 8) pressure = ReadLE16(&report[6]);
        if ((report[1] & 0x80) == 0 || report[1] == 0xC0 ||
            (report[1] == 0x80 && raw_x == 0 && raw_y == 0 && pressure == 0)) {
            in_range = false;
            is_pressed = false;
            raw_x = 0;
            raw_y = 0;
        }
    }
    else if (VendorID::IsXPPenFamily(spec.vid)) {
        if (spec.report_id != 0 && report[0] != spec.report_id) return DecodeReason::WrongReportId;
        if (reportSize > 1 && ((report[1] & 0x10) != 0 || (report[1] & 0xF0) == 0xF0)) return DecodeReason::ControlPacket;
        if (reportSize < 6) {
            return DecodeReason::ShortReport;
        }
        if (spec.y_offset == 5 && reportSize >= 10) {
            raw_x = ReadLE_N(&report[2], 24);
            raw_y = ReadLE_N(&report[5], 24);
            pressure = ReadLE16(&report[8]);
        } else {
            raw_x = ReadLE16(&report[2]);
            raw_y = ReadLE16(&report[4]);
            if (spec.coord_bits > 16 && reportSize >= 10) {
                raw_x |= (static_cast<int>(report[8]) << 16);
                raw_y |= (static_cast<int>(report[9]) << 16);
            }
            if (reportSize >= 8) pressure = ReadLE16(&report[6]);
        }
        is_pressed = (report[1] & 0x01) != 0;
        barrel = (report[1] & 0x02) != 0;
        if ((report[1] & 0xE0) == 0 && raw_x == 0 && raw_y == 0 && pressure == 0) {
            in_range = false;
        }
    }
    else {
        const int xo = spec.x_offset;
        const int yo = spec.y_offset;
        const int coord_bytes = (spec.coord_bits > 16) ? 3 : 2;
        const size_t min_len = static_cast<size_t>((std::max)(xo + coord_bytes, yo + coord_bytes));
        if (spec.report_id == 0 || report[0] == spec.report_id || report[0] == 0x10) {
            if (rSize < min_len) {
                return DecodeReason::ShortReport;
            }
            raw_x = ReadCoord(&report[xo], spec.coord_bits);
            raw_y = ReadCoord(&report[yo], spec.coord_bits);
            if (reportSize >= 8) pressure = ReadLE16(&report[6]);
            is_pressed = (report[1] & 0x01) != 0;
            barrel = (report[1] & 0x02) != 0;
            in_range = ((report[1] & 0xE0) != 0) || (raw_x != 0) || (raw_y != 0) || (pressure != 0) || is_pressed || barrel;
        } else {
            if (xo >= 1 && yo >= 1 && rSize >= static_cast<size_t>((std::max)(xo + 1, yo + 1))) {
                raw_x = ReadLE16(&report[xo - 1]);
                raw_y = ReadLE16(&report[yo - 1]);
                is_pressed = (report[0] & 0x01) != 0;
                barrel = (report[0] & 0x02) != 0;
            } else {
                return DecodeReason::WrongReportId;
            }
        }
    }

    const bool raw_hw_tip = (VendorID::IsWacom(spec.vid) ? in_range : is_pressed) && (pressure > 0);
    const uint16_t thresh = spec.tip_pressure_threshold > 0 ? spec.tip_pressure_threshold : 1;
    is_pressed = raw_hw_tip && (pressure >= thresh);

    if (raw_x == 0 && raw_y == 0 && !raw_hw_tip && !in_range) {
        return DecodeReason::ZeroFrame;
    }

    const int max_x_lim = spec.max_x > 0 ? spec.max_x : ((spec.coord_bits > 0 && spec.coord_bits < 31) ? ((1 << spec.coord_bits) - 1) : 0x7FFFFFFF);
    const int max_y_lim = spec.max_y > 0 ? spec.max_y : ((spec.coord_bits > 0 && spec.coord_bits < 31) ? ((1 << spec.coord_bits) - 1) : 0x7FFFFFFF);
    out.raw_x = std::clamp(raw_x, 0, max_x_lim);
    out.raw_y = std::clamp(raw_y, 0, max_y_lim);
    out.pressure = pressure;
    out.flags = (is_pressed ? 1 : 0) | (in_range ? 2 : 0) | (barrel ? 4 : 0) | (raw_hw_tip ? 8 : 0);
    return DecodeReason::Ok;
}

inline DecodeReason DecodeSample(const uint8_t* report, int reportSize, const TabletSpec& spec, PenSample& out) noexcept {
    return DecodeSample(report, reportSize, spec.ToRuntimeSpec(), out);
}

struct GoldenVector {
    std::string name;
    std::vector<uint8_t> data;
    bool expected_valid;
    int expected_x;
    int expected_y;
    int expected_pressure;
};

// Reference golden vectors V1 through V31
inline const std::vector<GoldenVector> g_test_vectors = {
    { "V1 - Wacom CTL-472 Valid Tip", { 0x02, 0x81, 0x3C, 0x20, 0x18, 0x25, 0x05, 0x00, 0x00, 0x00 }, true, 8252, 9496, 5 },
    { "V2 - Wacom Hover",             { 0x02, 0x41, 0x3C, 0x20, 0x18, 0x25, 0x00, 0x00, 0x00, 0x00 }, true, 8252, 9496, 0 },
    { "V3 - Wacom 0x10",              { 0x10, 0x81, 0x3C, 0x20, 0x18, 0x25, 0x05, 0x00, 0x00, 0x00 }, true, 8252, 9496, 5 },
    { "V4 - Huion 0x08",              { 0x08, 0x81, 0x20, 0x7C, 0x50, 0x4E, 0x1F, 0x40, 0x00, 0x00, 0x00, 0x00 }, true, 0x7C20, 0x4E50, 0x401F },
    { "V5 - XP-Pen 12B",              { 0x02, 0x81, 0x64, 0x09, 0x40, 0x06, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00 }, true, 0x0964, 0x0640, 7 },
    { "V6 - XP-Pen 12B bit 0x10",     { 0x02, 0x91, 0x64, 0x09, 0x40, 0x06, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00 }, false, 0, 0, 0 },
    { "V7 - XP-Pen lift",             { 0x02, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, false, 0, 0, 0 },
    { "V8 - VEIKK 9B",                { 0x02, 0x41, 0xA1, 0x64, 0x09, 0x40, 0x06, 0x07, 0x00, 0x00 }, true, 0x0964, 0x0640, 7 },
    { "V9 - Generic wrong id",        { 0x03, 0x81, 0x3C, 0x20, 0x18, 0x25, 0x05, 0x00, 0x00, 0x00 }, false, 0, 0, 0 },
    { "V10 - 17-bit Huion",           { 0x08, 0x81, 0x20, 0x7C, 0x50, 0x4E, 0x1F, 0x40, 0x00, 0x00, 0x00, 0x00 }, true, 0x7C20, 0x4E50, 0x401F },
    { "V11 - zero-frame",             { 0x08, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, false, 0, 0, 0 },
    { "V12 - hover (0,0) in-range",   { 0x02, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, true, 0, 0, 0 },
    { "V13 - Wacom Custom FW Tip",    { 0x02, 0xC0, 0xBE, 0x1C, 0x72, 0x0C, 0x10, 0x00, 0x00, 0x00 }, true, 7358, 3186, 16 },
    { "V14 - Wacom Hover Dist Byte8", { 0x02, 0x40, 0x3C, 0x20, 0x18, 0x25, 0x00, 0x00, 0x35, 0x00 }, true, 8252, 9496, 0 },
    { "V15 - Wacom Barrel Button 1",  { 0x02, 0x42, 0x3C, 0x20, 0x18, 0x25, 0x00, 0x00, 0x00, 0x00 }, true, 8252, 9496, 0 },
    { "V16 - Wacom Barrel Button 2",  { 0x02, 0x44, 0x3C, 0x20, 0x18, 0x25, 0x00, 0x00, 0x00, 0x00 }, true, 8252, 9496, 0 },
    { "V17 - Huion 17-bit with Byte8 High Bits", { 0x08, 0x81, 0x20, 0x7C, 0x50, 0x4E, 0x1F, 0x40, 0x03, 0x00, 0x00, 0x00 }, true, 0x17C20, 0x14E50, 0x401F },
    { "V18 - Wacom Bluetooth Tip",    { 0x82, 0x81, 0x3C, 0x20, 0x18, 0x25, 0x05, 0x00, 0x00, 0x00 }, true, 8252, 9496, 5 },
    { "V19 - Wacom Bluetooth Hover",  { 0xC2, 0x41, 0x3C, 0x20, 0x18, 0x25, 0x00, 0x00, 0x00, 0x00 }, true, 8252, 9496, 0 },
    { "V20 - Wacom Bluetooth Lift",   { 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, false, 0, 0, 0 },
    { "V21 - Wacom X Coord 0xC0 Valid", { 0x02, 0x41, 0xC0, 0x20, 0x18, 0x25, 0x00, 0x00, 0x00, 0x00 }, true, 0x20C0, 9496, 0 },
    { "V22 - Huion ExpressKey Reject (0xE0)", { 0x08, 0xE0, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, false, 0, 0, 0 },
    { "V23 - Huion TouchStrip Reject (0xF0)", { 0x08, 0xF0, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, false, 0, 0, 0 },
    { "V24 - VEIKK ExpressKey Reject (0x42)", { 0x02, 0x42, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, false, 0, 0, 0 },
    { "V25 - VEIKK Pen Lift (0x41 0xC0)",     { 0x02, 0x41, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, false, 0, 0, 0 },
    { "V26 - XP-Pen 24-bit High-Res",        { 0x02, 0x81, 0x10, 0x27, 0x20, 0x4E, 0xFF, 0x1F, 0x01, 0x01, 0x00, 0x00 }, true, 0x12710, 0x14E20, 0x1FFF },
    { "V27 - Wacom Pro 24-bit (0x10)",       { 0x10, 0x81, 0x10, 0x27, 0x01, 0x20, 0x4E, 0x01, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, true, 0x12710, 0x14E20, 5 },
    { "V28 - XP-Pen Hover 0x80 Non-Zero",     { 0x02, 0x80, 0x64, 0x09, 0x40, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, true, 0x0964, 0x0640, 0 },
    { "V29 - Huion Pen Lift 0x00 Reject",     { 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, false, 0, 0, 0 },
    { "V30 - Artisul Zero-Frame Lift Reject", { 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, false, 0, 0, 0 },
    { "V31 - VEIKK Invalid Status 0x00",      { 0x02, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, false, 0, 0, 0 }
};

inline TabletSpec SpecForVector(const GoldenVector& v) noexcept {
    TabletSpec spec{};
    spec.max_x = 2000000;
    spec.max_y = 2000000;

    if (v.name.find("VEIKK") != std::string::npos) {
        spec.vid = VendorID::Veikk1;
    } else if (v.name.find("XP-Pen") != std::string::npos) {
        spec.vid = VendorID::XPPen;
        spec.report_id = 0x02;
        spec.x_offset = 2;
        spec.y_offset = 4;
        if (v.name.find("24-bit") != std::string::npos) {
            spec.coord_bits = 24;
            spec.max_x = 200000;
            spec.max_y = 200000;
        }
    } else if (v.name.find("Huion") != std::string::npos || v.name.find("V11") != std::string::npos) {
        spec.vid = VendorID::Huion;
        spec.report_id = 0x08;
        spec.x_offset = 2;
        spec.y_offset = 4;
        if (v.name.find("17-bit") != std::string::npos) {
            spec.coord_bits = 17;
        }
    } else if (v.name.find("Artisul") != std::string::npos) {
        spec.vid = VendorID::Artisul;
        spec.report_id = 0x02;
        spec.x_offset = 2;
        spec.y_offset = 4;
    } else {
        spec.vid = VendorID::Wacom;
        spec.report_id = 0x02;
        spec.x_offset = 2;
        spec.y_offset = 4;
        if (v.name.find("24-bit") != std::string::npos) {
            spec.coord_bits = 24;
            spec.report_id = 0x10;
            spec.y_offset = 5;
        }
    }
    return spec;
}
