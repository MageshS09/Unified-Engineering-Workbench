#include "sysinfo.h"

#ifndef WB_BUILD_FLAGS
#define WB_BUILD_FLAGS "unknown"
#endif
#ifndef WB_BUILD_TYPE
#define WB_BUILD_TYPE "unknown"
#endif

namespace wb {

BuildInfo buildInfo() {
    BuildInfo info;
#if defined(__GNUC__)
    info.compiler = std::string("g++ ") + __VERSION__;
#else
    info.compiler = "unknown";
#endif
    info.flags = WB_BUILD_FLAGS;
    info.build_type = WB_BUILD_TYPE;
    return info;
}

}  // namespace wb
