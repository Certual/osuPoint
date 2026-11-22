#pragma once

#include <cstdint>
#include <linux/uinput.h>
#include <linux/input.h>

class UinputEmitter {
public:
    UinputEmitter() noexcept;
    ~UinputEmitter() noexcept;

    UinputEmitter(const UinputEmitter&) = delete;
    UinputEmitter& operator=(const UinputEmitter&) = delete;
    UinputEmitter(UinputEmitter&&) noexcept;
    UinputEmitter& operator=(UinputEmitter&&) noexcept;

    bool Initialize(bool mouse_mode = true, const char* dev_name = "osuPoint Virtual Tablet", uint16_t pressure_max = 8191,
                    int32_t resolution_x = 1000, int32_t resolution_y = 1000) noexcept;
    bool InitializeForTesting(int fd, bool mouse_mode = true) noexcept;
    void Destroy() noexcept;

    bool EmitMouseSample(int32_t x, int32_t y, bool left_down, bool right_down) noexcept;
    bool EmitStylusSample(int32_t x, int32_t y, uint16_t pressure, bool in_range, bool tip_down, bool barrel_down) noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept { return m_fd >= 0; }
    [[nodiscard]] int GetFd() const noexcept { return m_fd; }
    [[nodiscard]] bool IsMouseMode() const noexcept { return m_mouse_mode; }
    [[nodiscard]] const char* GetSysName() const noexcept { return m_sysname; }

private:
    int m_fd = -1;
    bool m_mouse_mode = true;
    char m_sysname[64] = {0};

    int32_t m_last_x = -1;
    int32_t m_last_y = -1;
    bool m_last_left = false;
    bool m_last_right = false;
    bool m_last_in_range = false;
    bool m_last_tip = false;
    bool m_last_barrel = false;
};
