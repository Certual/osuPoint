#include "osupoint/motion_filters.h"

namespace osupoint {

// Explicit anchor symbol to verify motion_filters translation unit linkage
const char* GetMotionFiltersVersion() noexcept {
    return "0.7.0-linux-physical-mm-filters";
}

} // namespace osupoint
