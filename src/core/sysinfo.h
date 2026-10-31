// Build/host information shown in the Performance view and reports.
#pragma once
#include <string>

namespace wb {

struct BuildInfo {
    std::string compiler;   // e.g. "g++ 16.2.0"
    std::string flags;      // compile flags passed at configure time
    std::string build_type;
};

BuildInfo buildInfo();

}  // namespace wb
