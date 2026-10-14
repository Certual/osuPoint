#include "tablet_spec.h"
#include <algorithm>
#include <cstdint>
#include <cstring>


static __forceinline uint16_t ReadLE16(const uint8_t* p) noexcept {
    uint16_t val;
    std::memcpy(&val, p, sizeof(val));
    return val;
}

static __forceinline uint32_t ReadLE32(const uint8_t* p) noexcept {
    uint32_t val;
    std::memcpy(&val, p, sizeof(val));
    return val;
}

static __forceinline uint32_t ReadLE_N(const uint8_t* p, int bits) noexcept {
    if (bits <= 8)  return p[0] & ((1u << bits) - 1);
    if (bits <= 16) {
        uint32_t v = p[0] | (p[1] << 8);
        return bits == 16 ? v : (v & ((1u << bits) - 1));
    }
    uint32_t v = p[0] | (p[1] << 8) | (p[2] << 16);
    return bits >= 24 ? v : (v & ((1u << bits) - 1));
}

static __forceinline uint32_t ReadCoord(const uint8_t* p, int bits) noexcept {
    if (bits == 16) [[likely]] {
        return ReadLE16(p);
    }
    return ReadLE_N(p, bits);
}

DecodeReason DecodeSample(const uint8_t* report, int reportSize, const TabletSpec& spec, PenSample& out) noexcept {
    if (!report || reportSize < 4) return DecodeReason::ShortReport;

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
        if (reportSize > 1 && (report[1] == 0xC0 || report[1] == 0x80)) {
            if (!VendorID::IsWacom(spec.vid) || IsZeroSpan(report, 2, (std::min)(reportSize, 9))) {
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
            else if (spec.report_id == 0x02 && (report[0] == 0x10 || report[0] == 0xC2 || report[0] == 0x82)) { valid = true; }
        }
        if (!valid) return DecodeReason::WrongReportId;
        const int xo = spec.x_offset;
        const int yo = spec.y_offset;
        const int min_len = (std::max)(xo + 2, yo + 2);
        if (reportSize < min_len) return DecodeReason::ShortReport;
        
        bool inRangeOrActive = ((report[1] & 0xC0) != 0) ||
                               ((report[1] & 0x07) != 0) ||
                               (reportSize >= 6 && ReadLE32(&report[2]) != 0);
        if (inRangeOrActive) {
            const int eff_yo = (report[0] == 0x10 && reportSize >= 18 && yo == 4) ? 5 : yo;
            const int eff_bits = (eff_yo >= 5 && reportSize >= eff_yo + 3) ? 24 : spec.coord_bits;
            if (eff_bits == 16 && xo == 2 && eff_yo == 4) [[likely]] {
                uint32_t xy = ReadLE32(&report[2]);
                raw_x = static_cast<int>(xy & 0xFFFF);
                raw_y = static_cast<int>(xy >> 16);
            } else {
                raw_x = ReadCoord(&report[xo], eff_bits);
                raw_y = ReadCoord(&report[eff_yo], eff_bits);
            }
            const int press_offset = (eff_yo >= 5) ? (eff_yo + 3) : 6;
            if (reportSize >= press_offset + 2) pressure = ReadLE16(&report[press_offset]) & 0x1FFF;
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
    else if (reportSize >= 7 && report[1] == 0x41 && (report[2] & 0xF0) == 0xA0) {
        raw_x = ReadLE16(&report[3]);
        raw_y = ReadLE16(&report[5]);
        is_pressed = (report[2] & 0x01) != 0;
        barrel = (report[2] & 0x02) != 0;
        if (reportSize >= 9) {
            pressure = ReadLE16(&report[7]);
        }
    }
    else if (VendorID::IsHuionFamily(spec.vid) && (report[0] == spec.report_id || report[0] == 0x08 || report[0] == 0x07 || report[0] == 0x09 || report[0] == 0x0A || report[0] == 0x0E)) {
        if (reportSize >= 6) {
            raw_x = ReadLE16(&report[2]);
            raw_y = ReadLE16(&report[4]);
            if (spec.coord_bits > 16 && reportSize >= 9) {
                if (report[8] & 0x01) raw_x |= (1 << 16);
                if (report[8] & 0x02) raw_y |= (1 << 16);
                if (spec.coord_bits >= 18) {
                    if (report[8] & 0x04) raw_x |= (1 << 17);
                    if (report[8] & 0x08) raw_y |= (1 << 17);
                }
            }
            is_pressed = (report[1] & 0x01) != 0;
            barrel = (report[1] & 0x02) != 0;
            if (reportSize >= 8) pressure = ReadLE16(&report[6]);
            if (report[0] == 0x08 && report[1] == 0x80) {
                in_range = false; // ZeroFrame
            }
        }
    }
    else if (VendorID::IsXPPenFamily(spec.vid)) {
        if (spec.report_id != 0 && report[0] != spec.report_id) return DecodeReason::WrongReportId;
        if (reportSize > 1 && (report[1] & 0x10) != 0) return DecodeReason::ControlPacket;
        if (reportSize >= 6) {
            raw_x = ReadLE16(&report[2]);
            raw_y = ReadLE16(&report[4]);
            if (spec.coord_bits > 16 && reportSize >= 10) {
                raw_x |= (static_cast<int>(report[8]) << 16);
                raw_y |= (static_cast<int>(report[9]) << 16);
            }
            is_pressed = (report[1] & 0x01) != 0;
            barrel = (report[1] & 0x02) != 0;
            if (reportSize >= 8) pressure = ReadLE16(&report[6]);
        }
    }
    else {
        const int xo = spec.x_offset;
        const int yo = spec.y_offset;
        const int min_len = (std::max)(xo + 2, yo + 2);
        if (spec.report_id == 0 || report[0] == spec.report_id || report[0] == 0x10) {
            if (reportSize >= min_len) {
                raw_x = ReadCoord(&report[xo], spec.coord_bits);
                raw_y = ReadCoord(&report[yo], spec.coord_bits);
                if (reportSize >= 8) pressure = ReadLE16(&report[6]);
                is_pressed = (report[1] & 0x01) != 0;
                barrel = (report[1] & 0x02) != 0;
            }
        } else {
            if (xo >= 1 && yo >= 1 && reportSize >= (std::max)(xo + 1, yo + 1)) {
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

    const int max_x_lim = spec.max_x > 0 ? spec.max_x : ((1 << spec.coord_bits) - 1);
    const int max_y_lim = spec.max_y > 0 ? spec.max_y : ((1 << spec.coord_bits) - 1);
    out.raw_x = std::clamp(raw_x, 0, max_x_lim);
    out.raw_y = std::clamp(raw_y, 0, max_y_lim);
    out.pressure = pressure;
    out.flags = (is_pressed ? 1 : 0) | (in_range ? 2 : 0) | (barrel ? 4 : 0) | (raw_hw_tip ? 8 : 0);
    return DecodeReason::Ok;
}
