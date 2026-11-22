#include "osupoint/hidraw_reader.h"
#include "osupoint/rt_scheduler.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/hidraw.h>
#include <linux/input.h>
#include <dirent.h>
#include <cstring>
#include <cstdio>
#include <cerrno>

HidrawReader::HidrawReader() noexcept = default;

HidrawReader::~HidrawReader() noexcept {
    Close();
}

bool HidrawReader::FindMatchingEvdev(const char* hidraw_node_name, uint16_t vid, uint16_t pid, char* out_evdev, size_t out_size) noexcept {
    if (!hidraw_node_name || !out_evdev || out_size == 0) return false;
    out_evdev[0] = '\0';

    // Strategy 1: Check sysfs hierarchy under /sys/class/hidraw/<name>/device/input/
    char sysfs_input_dir[256];
    std::snprintf(sysfs_input_dir, sizeof(sysfs_input_dir), "/sys/class/hidraw/%s/device/input", hidraw_node_name);

    DIR* dir = opendir(sysfs_input_dir);
    if (dir) {
        struct dirent* ent = nullptr;
        while ((ent = readdir(dir)) != nullptr) {
            if (std::strncmp(ent->d_name, "input", 5) == 0) {
                char input_sub_dir[300];
                std::snprintf(input_sub_dir, sizeof(input_sub_dir), "%s/%s", sysfs_input_dir, ent->d_name);
                DIR* sub_dir = opendir(input_sub_dir);
                if (sub_dir) {
                    struct dirent* sub_ent = nullptr;
                    while ((sub_ent = readdir(sub_dir)) != nullptr) {
                        if (std::strncmp(sub_ent->d_name, "event", 5) == 0) {
                            std::snprintf(out_evdev, out_size, "/dev/input/%s", sub_ent->d_name);
                            closedir(sub_dir);
                            closedir(dir);
                            return true;
                        }
                    }
                    closedir(sub_dir);
                }
            }
        }
        closedir(dir);
    }

    // Strategy 2: Probe /dev/input/event0 to /dev/input/event31 matching VID/PID
    for (int i = 0; i < 32; ++i) {
        char ev_path[64];
        std::snprintf(ev_path, sizeof(ev_path), "/dev/input/event%d", i);
        int fd = open(ev_path, O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            struct input_id id{};
            if (ioctl(fd, EVIOCGID, &id) == 0) {
                if (id.vendor == vid && id.product == pid) {
                    std::snprintf(out_evdev, out_size, "%s", ev_path);
                    close(fd);
                    return true;
                }
            }
            close(fd);
        }
    }

    return false;
}

bool HidrawReader::DiscoverTablet(
    DiscoveredTablet& out_tablet,
    const char* explicit_device,
    uint16_t filter_vid,
    uint16_t filter_pid) noexcept
{
    out_tablet.found = false;

    if (explicit_device && explicit_device[0] != '\0') {
        int fd = open(explicit_device, O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            return false;
        }

        struct hidraw_devinfo info{};
        if (ioctl(fd, HIDIOCGRAWINFO, &info) < 0) {
            close(fd);
            return false;
        }

        char name[128] = {0};
        ioctl(fd, HIDIOCGRAWNAME(sizeof(name)), name);
        close(fd);

        std::snprintf(out_tablet.hidraw_path, sizeof(out_tablet.hidraw_path), "%s", explicit_device);
        std::snprintf(out_tablet.device_name, sizeof(out_tablet.device_name), "%s", name);
        out_tablet.vid = static_cast<uint16_t>(info.vendor);
        out_tablet.pid = static_cast<uint16_t>(info.product);
        out_tablet.bustype = static_cast<uint32_t>(info.bustype);

        if (!FindBuiltinTablet(out_tablet.vid, out_tablet.pid, out_tablet.spec)) {
            // Default generic fallback
            std::snprintf(out_tablet.spec.name, sizeof(out_tablet.spec.name), "%s", name);
            out_tablet.spec.vid = out_tablet.vid;
            out_tablet.spec.pid = out_tablet.pid;
        }

        const char* base_name = std::strrchr(explicit_device, '/');
        const char* node_name = base_name ? (base_name + 1) : explicit_device;
        FindMatchingEvdev(node_name, out_tablet.vid, out_tablet.pid, out_tablet.evdev_path, sizeof(out_tablet.evdev_path));

        out_tablet.found = true;
        return true;
    }

    // Auto-discovery: probe /dev/hidraw0 to /dev/hidraw31
    for (int i = 0; i < 32; ++i) {
        char path[64];
        std::snprintf(path, sizeof(path), "/dev/hidraw%d", i);

        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            continue;
        }

        struct hidraw_devinfo info{};
        if (ioctl(fd, HIDIOCGRAWINFO, &info) < 0) {
            close(fd);
            continue;
        }

        const uint16_t vid = static_cast<uint16_t>(info.vendor);
        const uint16_t pid = static_cast<uint16_t>(info.product);

        if (filter_vid != 0 && vid != filter_vid) {
            close(fd);
            continue;
        }
        if (filter_pid != 0 && pid != filter_pid) {
            close(fd);
            continue;
        }

        if (filter_vid == 0 && filter_pid == 0) {
            if (!VendorID::IsKnownTabletVendor(vid)) {
                close(fd);
                continue;
            }
        }

        char name[128] = {0};
        ioctl(fd, HIDIOCGRAWNAME(sizeof(name)), name);
        close(fd);

        std::snprintf(out_tablet.hidraw_path, sizeof(out_tablet.hidraw_path), "%s", path);
        std::snprintf(out_tablet.device_name, sizeof(out_tablet.device_name), "%s", name);
        out_tablet.vid = vid;
        out_tablet.pid = pid;
        out_tablet.bustype = static_cast<uint32_t>(info.bustype);

        if (!FindBuiltinTablet(vid, pid, out_tablet.spec)) {
            std::snprintf(out_tablet.spec.name, sizeof(out_tablet.spec.name), "%s", name);
            out_tablet.spec.vid = vid;
            out_tablet.spec.pid = pid;
        }

        char node_name[32];
        std::snprintf(node_name, sizeof(node_name), "hidraw%d", i);
        FindMatchingEvdev(node_name, vid, pid, out_tablet.evdev_path, sizeof(out_tablet.evdev_path));

        out_tablet.found = true;
        return true;
    }

    return false;
}

static bool ParseHexStringToBytes(const char* str, uint8_t* out, size_t max_out, size_t& out_len) noexcept {
    out_len = 0;
    if (!str || !*str) return false;
    const char* p = str;
    while (*p && out_len < max_out) {
        while (*p == ' ' || *p == '\t' || *p == ',') ++p;
        if (!*p) break;
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
        char* end = nullptr;
        unsigned long val = std::strtoul(p, &end, 16);
        if (end == p) break;
        out[out_len++] = static_cast<uint8_t>(val);
        p = end;
    }
    return out_len > 0;
}

bool HidrawReader::Open(const DiscoveredTablet& tablet) noexcept {
    Close();
    m_init_error = 0;
    m_tablet = tablet;

    // Open O_RDWR so we can send feature reports and output reports (fallback O_RDONLY)
    int fd = open(tablet.hidraw_path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        fd = open(tablet.hidraw_path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    }
    if (fd < 0) {
        return false;
    }
    m_hidraw_fd.store(fd, std::memory_order_release);

    // 1. Send feature report initialization if specified or if Wacom
    if (tablet.spec.init_feature[0] != '\0') {
        uint8_t feat_buf[64]{};
        size_t feat_len = 0;
        if (ParseHexStringToBytes(tablet.spec.init_feature, feat_buf, sizeof(feat_buf), feat_len)) {
            if (ioctl(fd, HIDIOCSFEATURE(feat_len), feat_buf) < 0) m_init_error = errno;
        }
    } else if (VendorID::IsWacom(tablet.vid)) {
        // Wacom CTL-472, CTL-480, CTL-490, etc. require feature report [0x02, 0x02] to stream digitizer packets
        uint8_t wacom_init[2] = { 0x02, 0x02 };
        if (ioctl(fd, HIDIOCSFEATURE(2), wacom_init) < 0) m_init_error = errno;
    }

    // 2. Send output report initialization if specified
    if (tablet.spec.init_output[0] != '\0') {
        uint8_t out_buf[64]{};
        size_t out_len = 0;
        if (ParseHexStringToBytes(tablet.spec.init_output, out_buf, sizeof(out_buf), out_len)) {
            if (ioctl(fd, HIDIOCSOUTPUT(out_len), out_buf) < 0) {
                if (write(fd, out_buf, out_len) != static_cast<ssize_t>(out_len)) m_init_error = errno;
            }
        }
    }

    // Allow tablet firmware to settle into raw streaming mode
    usleep(15000);

    // Suppress desktop cursor duplication via EVIOCGRAB on matching evdev
    if (tablet.evdev_path[0] != '\0') {
        m_evdev_grab_fd = open(tablet.evdev_path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (m_evdev_grab_fd >= 0) {
            m_evdev_grabbed = ioctl(m_evdev_grab_fd, EVIOCGRAB, 1) == 0;
            if (!m_evdev_grabbed) m_init_error = errno;
        }
    }

    return true;
}

void HidrawReader::Close() noexcept {
    if (m_evdev_grab_fd >= 0) {
        ioctl(m_evdev_grab_fd, EVIOCGRAB, 0);
        close(m_evdev_grab_fd);
        m_evdev_grab_fd = -1;
    }
    m_evdev_grabbed = false;
    int fd = m_hidraw_fd.exchange(-1, std::memory_order_acq_rel);
    if (fd >= 0) {
        close(fd);
    }
}
