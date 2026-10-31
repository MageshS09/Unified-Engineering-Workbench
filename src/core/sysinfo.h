// Build and host information shown in the Performance view and reports.
#pragma once
#include <cstdint>
#include <string>

namespace wb {

struct BuildInfo {
    std::string compiler;  // e.g. "g++ 16.2.0"
    std::string flags;     // compile flags passed at configure time
    std::string build_type;
};

struct SystemInfo {
    std::string cpu_name;
    int physical_cores = 0;
    int logical_cores = 0;
    std::uint64_t ram_bytes = 0;
};

BuildInfo buildInfo();
SystemInfo querySystemInfo();

}  // namespace wb
