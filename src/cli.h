#pragma once
#include <string>

struct CliOptions {
    bool debug = false;
    bool calibrate = false;
    bool list_devices = false;
    bool selftest = false;
    bool probe = false;
    bool show_help = false;
    std::string dump_reports_file;
    bool should_exit = false;
    int exit_code = 0;
};

void AttachConsoleIO();
void PrintHelp();
CliOptions ParseCommandLineArgs(const char* cmdLine);
