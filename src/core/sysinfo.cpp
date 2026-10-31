#include "sysinfo.h"

#include <windows.h>

#include <algorithm>
#include <bitset>
#include <thread>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64)
#include <cpuid.h>
#endif

#ifndef WB_BUILD_FLAGS
#define WB_BUILD_FLAGS "unknown"
#endif
#ifndef WB_BUILD_TYPE
#define WB_BUILD_TYPE "unknown"
#endif

namespace wb {

namespace {

// CPU brand string from CPUID leaves 0x80000002..4 (empty if unavailable).
std::string cpuBrand() {
#if defined(__x86_64__) || defined(_M_X64)
    unsigned int regs[12] = {};
    if (!__get_cpuid(0x80000000u, &regs[0], &regs[1], &regs[2], &regs[3]) || regs[0] < 0x80000004u) return {};
    for (unsigned int i = 0; i < 3; ++i)
        __get_cpuid(0x80000002u + i, &regs[i * 4], &regs[i * 4 + 1], &regs[i * 4 + 2], &regs[i * 4 + 3]);
    std::string s(reinterpret_cast<const char*>(regs), sizeof regs);
    s.resize(s.find('\0') == std::string::npos ? s.size() : s.find('\0'));
    const size_t first = s.find_first_not_of(' ');
    return first == std::string::npos ? std::string() : s.substr(first);
#else
    return {};
#endif
}

// Walks GetLogicalProcessorInformationEx(RelationProcessorCore): one record per physical core,
// whose group masks give its logical processors.
void countCores(int& physical, int& logical) {
    physical = logical = 0;
    DWORD bytes = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
    if (bytes == 0) return;
    std::vector<char> buf(bytes);
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore,
                                          reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data()), &bytes))
        return;
    for (DWORD off = 0; off < bytes;) {
        const auto* rec = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data() + off);
        ++physical;
        for (WORD g = 0; g < rec->Processor.GroupCount; ++g)
            logical += static_cast<int>(std::bitset<sizeof(KAFFINITY) * 8>(rec->Processor.GroupMask[g].Mask).count());
        off += rec->Size;
    }
}

}  // namespace

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

SystemInfo querySystemInfo() {
    SystemInfo s;
    s.cpu_name = cpuBrand();
    if (s.cpu_name.empty()) s.cpu_name = "unknown CPU";
    countCores(s.physical_cores, s.logical_cores);
    if (s.logical_cores < 1) s.logical_cores = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    if (s.physical_cores < 1) s.physical_cores = s.logical_cores;
    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof mem;
    if (GlobalMemoryStatusEx(&mem)) s.ram_bytes = mem.ullTotalPhys;
    return s;
}

}  // namespace wb
