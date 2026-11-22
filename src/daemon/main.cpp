#include "osupoint/app_options.h"

int main(int argc, char** argv) {
    osupoint::AppOptions options;
    const int result = osupoint::ParseOptions(argc, argv, options);
    if (result) return result < 0 ? 2 : 0;
    return osupoint::RunHeadless(options);
}
