#include "cli.h"
#include <windows.h>
#include <cstdio>
#include <string_view>
#include <io.h>
#include <fcntl.h>

void AttachConsoleIO() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD type = (hOut != INVALID_HANDLE_VALUE && hOut != nullptr) ? GetFileType(hOut) : FILE_TYPE_UNKNOWN;
    if (type == FILE_TYPE_PIPE || type == FILE_TYPE_DISK) {
        int fd = _open_osfhandle(reinterpret_cast<intptr_t>(hOut), _O_TEXT);
        if (fd >= 0) {
            FILE* fp = _fdopen(fd, "w");
            if (fp) {
                *stdout = *fp;
                setvbuf(stdout, NULL, _IONBF, 0);
            }
        }
        return;
    }
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* fp = nullptr;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
    }
}

void PrintHelp() {
    printf("osu!Point - Advanced Tablet Driver\n"
           "Usage:\n"
           "  --debug          Enable debug mode and verbose trace logs\n"
           "  --list-devices   Show all detected devices\n"
           "  --calibrate      Start calibration mode\n"
           "  --dump-reports   <file> Dump raw reports to binary file\n"
#if defined(ENABLE_SELFTEST) && ENABLE_SELFTEST
           "  --selftest       Run self tests\n"
#endif
           "  --probe          Dump HID descriptor caps and 50 raw packets\n");
}

CliOptions ParseCommandLineArgs(const char* cmdLine) {
    CliOptions opts{};
    if (!cmdLine) return opts;
    const std::string_view cmd(cmdLine);

    if (cmd.find("--help") != std::string_view::npos) {
        opts.show_help = true;
        opts.should_exit = true;
        opts.exit_code = 0;
        return opts;
    }

    if (cmd.find("--debug") != std::string_view::npos) {
        opts.debug = true;
    }

    if (size_t pos = cmd.find("--dump-reports"); pos != std::string_view::npos) {
        std::string_view sub = cmd.substr(pos + 14);
        while (!sub.empty() && sub.front() == ' ') sub.remove_prefix(1);
        size_t end = sub.find(' ');
        if (!sub.empty()) {
            opts.dump_reports_file = std::string(end == std::string_view::npos ? sub : sub.substr(0, end));
        }
    }

    if (cmd.find("--calibrate") != std::string_view::npos) {
        opts.calibrate = true;
    }

#if defined(ENABLE_SELFTEST) && ENABLE_SELFTEST
    if (cmd.find("--selftest") != std::string_view::npos) {
        opts.selftest = true;
        opts.should_exit = true;
        opts.exit_code = 0;
    }
#endif

    if (cmd.find("--probe") != std::string_view::npos) {
        opts.probe = true;
        opts.should_exit = true;
        opts.exit_code = 0;
    }

    if (cmd.find("--list-devices") != std::string_view::npos) {
        opts.list_devices = true;
        opts.should_exit = true;
        opts.exit_code = 0;
    }

    return opts;
}
