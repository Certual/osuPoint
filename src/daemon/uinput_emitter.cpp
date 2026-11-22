#include "osupoint/uinput_emitter.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <algorithm>

static inline struct input_event MakeInputEvent(uint16_t type, uint16_t code, int32_t value) noexcept {
    struct input_event ev{};
    ev.type = type;
    ev.code = code;
    ev.value = value;
    return ev;
}

UinputEmitter::UinputEmitter() noexcept = default;

UinputEmitter::~UinputEmitter() noexcept {
    Destroy();
}

UinputEmitter::UinputEmitter(UinputEmitter&& other) noexcept
    : m_fd(other.m_fd)
    , m_mouse_mode(other.m_mouse_mode)
    , m_last_x(other.m_last_x)
    , m_last_y(other.m_last_y)
    , m_last_left(other.m_last_left)
    , m_last_right(other.m_last_right)
    , m_last_in_range(other.m_last_in_range)
    , m_last_tip(other.m_last_tip)
    , m_last_barrel(other.m_last_barrel)
{
    std::memcpy(m_sysname, other.m_sysname, sizeof(m_sysname));
    other.m_fd = -1;
    other.m_sysname[0] = '\0';
}

UinputEmitter& UinputEmitter::operator=(UinputEmitter&& other) noexcept {
    if (this != &other) {
        Destroy();
        m_fd = other.m_fd;
        m_mouse_mode = other.m_mouse_mode;
        std::memcpy(m_sysname, other.m_sysname, sizeof(m_sysname));
        m_last_x = other.m_last_x;
        m_last_y = other.m_last_y;
        m_last_left = other.m_last_left;
        m_last_right = other.m_last_right;
        m_last_in_range = other.m_last_in_range;
        m_last_tip = other.m_last_tip;
        m_last_barrel = other.m_last_barrel;

        other.m_fd = -1;
        other.m_sysname[0] = '\0';
    }
    return *this;
}

bool UinputEmitter::Initialize(bool mouse_mode, const char* dev_name, uint16_t pressure_max, int32_t resolution_x, int32_t resolution_y) noexcept {
    Destroy();
    m_mouse_mode = mouse_mode;

    m_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (m_fd < 0) {
        m_fd = open("/dev/input/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
        if (m_fd < 0) {
            return false;
        }
    }

    unsigned int version = 0;
    if (ioctl(m_fd, UI_GET_VERSION, &version) == 0) {
        if (version < 4) {
            errno = ENOTSUP;
            close(m_fd);
            m_fd = -1;
            return false;
        }
    }

    if (ioctl(m_fd, UI_SET_EVBIT, EV_SYN) < 0 ||
        ioctl(m_fd, UI_SET_EVBIT, EV_KEY) < 0 ||
        ioctl(m_fd, UI_SET_EVBIT, EV_ABS) < 0)
    {
        close(m_fd);
        m_fd = -1;
        return false;
    }

    if (ioctl(m_fd, UI_SET_ABSBIT, ABS_X) < 0 ||
        ioctl(m_fd, UI_SET_ABSBIT, ABS_Y) < 0)
    {
        close(m_fd);
        m_fd = -1;
        return false;
    }

    if (m_mouse_mode) {
        ioctl(m_fd, UI_SET_PROPBIT, INPUT_PROP_POINTER);
        ioctl(m_fd, UI_SET_KEYBIT, BTN_LEFT);
        ioctl(m_fd, UI_SET_KEYBIT, BTN_RIGHT);
        ioctl(m_fd, UI_SET_KEYBIT, BTN_MIDDLE);
    } else {
        ioctl(m_fd, UI_SET_PROPBIT, INPUT_PROP_POINTER);
        ioctl(m_fd, UI_SET_ABSBIT, ABS_PRESSURE);
        ioctl(m_fd, UI_SET_KEYBIT, BTN_TOOL_PEN);
        ioctl(m_fd, UI_SET_KEYBIT, BTN_TOUCH);
        ioctl(m_fd, UI_SET_KEYBIT, BTN_STYLUS);
        ioctl(m_fd, UI_SET_KEYBIT, BTN_STYLUS2);
    }

    struct uinput_abs_setup abs_x{};
    abs_x.code = ABS_X;
    abs_x.absinfo.minimum = 0;
    abs_x.absinfo.maximum = 65535;
    abs_x.absinfo.fuzz = 0;
    abs_x.absinfo.flat = 0;
    abs_x.absinfo.resolution = std::max(1, resolution_x);
    if (ioctl(m_fd, UI_ABS_SETUP, &abs_x) < 0) {
        close(m_fd);
        m_fd = -1;
        return false;
    }

    struct uinput_abs_setup abs_y{};
    abs_y.code = ABS_Y;
    abs_y.absinfo.minimum = 0;
    abs_y.absinfo.maximum = 65535;
    abs_y.absinfo.fuzz = 0;
    abs_y.absinfo.flat = 0;
    abs_y.absinfo.resolution = std::max(1, resolution_y);
    if (ioctl(m_fd, UI_ABS_SETUP, &abs_y) < 0) {
        close(m_fd);
        m_fd = -1;
        return false;
    }

    if (!m_mouse_mode) {
        struct uinput_abs_setup abs_p{};
        abs_p.code = ABS_PRESSURE;
        abs_p.absinfo.minimum = 0;
        abs_p.absinfo.maximum = std::max<uint16_t>(1, pressure_max);
        abs_p.absinfo.fuzz = 0;
        abs_p.absinfo.flat = 0;
        abs_p.absinfo.resolution = 0;
        if (ioctl(m_fd, UI_ABS_SETUP, &abs_p) < 0) {
            close(m_fd);
            m_fd = -1;
            return false;
        }
    }

    struct uinput_setup usetup{};
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor = 0x544E;  // 'TN'
    usetup.id.product = 0x504F; // 'PO'
    usetup.id.version = 1;
    std::strncpy(usetup.name, dev_name ? dev_name : "osuPoint Virtual Tablet", UINPUT_MAX_NAME_SIZE - 1);
    usetup.name[UINPUT_MAX_NAME_SIZE - 1] = '\0';

    if (ioctl(m_fd, UI_DEV_SETUP, &usetup) < 0) {
        close(m_fd);
        m_fd = -1;
        return false;
    }

    if (ioctl(m_fd, UI_DEV_CREATE) < 0) {
        close(m_fd);
        m_fd = -1;
        return false;
    }

    m_sysname[0] = '\0';
    if (ioctl(m_fd, UI_GET_SYSNAME(sizeof(m_sysname)), m_sysname) < 0) {
        std::snprintf(m_sysname, sizeof(m_sysname), "unknown");
    }

    m_last_x = -1;
    m_last_y = -1;
    m_last_left = false;
    m_last_right = false;
    m_last_in_range = false;
    m_last_tip = false;
    m_last_barrel = false;

    return true;
}

void UinputEmitter::Destroy() noexcept {
    if (m_fd >= 0) {
        ioctl(m_fd, UI_DEV_DESTROY);
        close(m_fd);
        m_fd = -1;
    }
    m_sysname[0] = '\0';
}

bool UinputEmitter::InitializeForTesting(int fd, bool mouse_mode) noexcept {
    Destroy();
    m_fd = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    m_mouse_mode = mouse_mode;
    m_last_x = m_last_y = -1;
    m_last_left = m_last_right = m_last_in_range = m_last_tip = m_last_barrel = false;
    return m_fd >= 0;
}

bool UinputEmitter::EmitMouseSample(int32_t x, int32_t y, bool left_down, bool right_down) noexcept {
    if (m_fd < 0) { errno = ENODEV; return false; }

    struct input_event evs[5];
    int count = 0;

    evs[count++] = MakeInputEvent(EV_ABS, ABS_X, x);
    evs[count++] = MakeInputEvent(EV_ABS, ABS_Y, y);

    if (left_down != m_last_left || m_last_x < 0) {
        evs[count++] = MakeInputEvent(EV_KEY, BTN_LEFT, left_down ? 1 : 0);
    }
    if (right_down != m_last_right || m_last_x < 0) {
        evs[count++] = MakeInputEvent(EV_KEY, BTN_RIGHT, right_down ? 1 : 0);
    }
    evs[count++] = MakeInputEvent(EV_SYN, SYN_REPORT, 0);

    const size_t bytesToWrite = count * sizeof(struct input_event);
    const ssize_t written = write(m_fd, evs, bytesToWrite);
    if (written == static_cast<ssize_t>(bytesToWrite)) {
        m_last_x = x;
        m_last_y = y;
        m_last_left = left_down;
        m_last_right = right_down;
        return true;
    }
    if (written >= 0) errno = EIO;
    return false;
}

bool UinputEmitter::EmitStylusSample(int32_t x, int32_t y, uint16_t pressure, bool in_range, bool tip_down, bool barrel_down) noexcept {
    if (m_fd < 0) { errno = ENODEV; return false; }

    struct input_event evs[7];
    int count = 0;

    evs[count++] = MakeInputEvent(EV_ABS, ABS_X, x);
    evs[count++] = MakeInputEvent(EV_ABS, ABS_Y, y);
    evs[count++] = MakeInputEvent(EV_ABS, ABS_PRESSURE, static_cast<int32_t>(pressure));

    if (in_range != m_last_in_range || m_last_x < 0) {
        evs[count++] = MakeInputEvent(EV_KEY, BTN_TOOL_PEN, in_range ? 1 : 0);
    }
    if (tip_down != m_last_tip || m_last_x < 0) {
        evs[count++] = MakeInputEvent(EV_KEY, BTN_TOUCH, tip_down ? 1 : 0);
    }
    if (barrel_down != m_last_barrel || m_last_x < 0) {
        evs[count++] = MakeInputEvent(EV_KEY, BTN_STYLUS, barrel_down ? 1 : 0);
    }
    evs[count++] = MakeInputEvent(EV_SYN, SYN_REPORT, 0);

    const size_t bytesToWrite = count * sizeof(struct input_event);
    const ssize_t written = write(m_fd, evs, bytesToWrite);
    if (written == static_cast<ssize_t>(bytesToWrite)) {
        m_last_x = x;
        m_last_y = y;
        m_last_in_range = in_range;
        m_last_tip = tip_down;
        m_last_barrel = barrel_down;
        return true;
    }
    if (written >= 0) errno = EIO;
    return false;
}
