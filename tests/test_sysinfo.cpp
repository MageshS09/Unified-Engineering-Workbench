#include "doctest.h"
#include "sysinfo.h"

TEST_CASE("build info exposes compiler and flags") {
    const wb::BuildInfo info = wb::buildInfo();
    CHECK_FALSE(info.compiler.empty());
    CHECK_FALSE(info.flags.empty());
}
