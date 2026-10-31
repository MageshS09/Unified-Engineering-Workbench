// Headless reproduction of everything in results/ (grows with each phase).
#include <iostream>

#include "sysinfo.h"

int main() {
    const wb::BuildInfo info = wb::buildInfo();
    std::cout << "workbench_cli: " << info.compiler << " [" << info.flags << "]\n";
    return 0;
}
