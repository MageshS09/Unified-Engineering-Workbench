// Headless reproduction of everything in results/ (grows with each phase).
// Usage: workbench_cli [results_dir]   (default: results/ next to the executable's parent)
#include <windows.h>

#include <filesystem>
#include <iostream>

#include "fea_study.h"
#include "fileio.h"
#include "sysinfo.h"

namespace fs = std::filesystem;

namespace {

fs::path exeDirectory() {
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(std::wstring(buf, n)).parent_path();
}

// Packaged layout: <root>/workbench_cli.exe -> <root>/results. Build layout: <root>/build/ -> <root>/results.
fs::path defaultResultsDir() {
    const fs::path dir = exeDirectory();
    if (dir.filename() == "build") return dir.parent_path() / "results";
    return dir / "results";
}

void writeFeaResults(const fs::path& out) {
    const wb::RodParams base;  // task defaults: L=1, A=1e-4, k=10, q=1e4, 100 C -> 20 C
    const wb::FeaStudy study = wb::runStudy(base, {10, 20, 40, 80});
    for (const auto& sol : study.solutions) {
        wb::RodParams p = base;
        p.elements = sol.elements;
        wb::writeFileAtomic((out / ("fea_N" + std::to_string(sol.elements) + ".csv")).string(), wb::solutionCsv(p, sol));
    }
    wb::writeFileAtomic((out / "fea_convergence.csv").string(), wb::convergenceCsv(study.rows));
    wb::writeFileAtomic((out / "fea_temperature.svg").string(), wb::temperatureSvg(study));
    wb::writeFileAtomic((out / "fea_convergence.svg").string(), wb::convergenceSvg(study));
    std::cout << "FEA: wrote fea_N*.csv, fea_convergence.csv, fea_temperature.svg, fea_convergence.svg\n";
    for (const auto& r : study.rows)
        std::cout << "  N=" << r.elements << " nodal=" << r.error.max_nodal_C << " midpoint=" << r.error.max_midpoint_C
                  << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const fs::path out = argc > 1 ? fs::path(argv[1]) : defaultResultsDir();
        const wb::BuildInfo info = wb::buildInfo();
        std::cout << "workbench_cli: " << info.compiler << " [" << info.flags << "]\nresults dir: " << out.string() << "\n";
        writeFeaResults(out);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "workbench_cli failed: " << e.what() << "\n";
        return 1;
    }
}
