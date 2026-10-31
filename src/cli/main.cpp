// Headless reproduction of everything in results/ (grows with each phase).
// Usage: workbench_cli [results_dir]   (default: results/ next to the executable's parent)
#include <windows.h>

#include <filesystem>
#include <iostream>

#include "backprop.h"
#include "batch.h"
#include "fea_study.h"
#include "fileio.h"
#include "svg_plot.h"
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

// Step 0 = initial parameters, step 1 = the single hand-checked update, steps 2..101 = 100 subsequent updates.
void writeBackpropResults(const fs::path& out) {
    const wb::BackpropSetup setup;
    const wb::NeuronParams start;
    const auto checks = wb::gradientCheck(setup, start);
    const std::vector<double> loss = wb::trainLoss(setup, start, 101);
    wb::writeFileAtomic((out / "backprop_gradcheck.csv").string(), wb::gradCheckCsv(checks));
    wb::writeFileAtomic((out / "backprop_loss.csv").string(), wb::lossCsv(loss));

    wb::PlotSpec spec;
    spec.title = "Backpropagation: loss J vs training step (lr = 0.1)";
    spec.x_label = "training step (step 1 = first update)";
    spec.y_label = "loss J (dimensionless)";
    wb::PlotSeries ser{"J", {}, {}, "#1f77b4", false, false};
    for (size_t i = 0; i < loss.size(); ++i) {
        ser.x.push_back(static_cast<double>(i));
        ser.y.push_back(loss[i]);
    }
    spec.series.push_back(ser);
    wb::writeFileAtomic((out / "backprop_loss.svg").string(), wb::renderSvg(spec));
    std::cout << "Backprop: wrote backprop_gradcheck.csv, backprop_loss.csv, backprop_loss.svg (final loss "
              << loss.back() << ")" << std::endl;
    for (const auto& r : checks)
        std::cout << "  d" << r.name << ": |analytic-numeric| = " << r.abs_error << (r.pass ? " ok" : " FAIL")
                  << std::endl;
}

void writePerfResults(const fs::path& out) {
    const wb::PerfReport rep = wb::runBenchmark(wb::PerfConfig{});
    wb::writeFileAtomic((out / "perf.csv").string(), wb::perfCsv(rep));
    wb::writeFileAtomic((out / "perf_speedup.svg").string(), wb::perfSpeedupSvg(rep));
    wb::writeFileAtomic((out / "sysinfo.txt").string(), wb::perfSummaryText(rep));
    std::cout << wb::perfSummaryText(rep);
    for (const auto& r : rep.rows)
        std::cout << "  " << r.mode << " p=" << r.workers << " median=" << r.median_s << " s speedup=" << r.speedup
                  << " eff=" << r.efficiency << std::endl;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const fs::path out = argc > 1 ? fs::path(argv[1]) : defaultResultsDir();
        const wb::BuildInfo info = wb::buildInfo();
        std::cout << "workbench_cli: " << info.compiler << " [" << info.flags << "]\nresults dir: " << out.string() << "\n";
        writeFeaResults(out);
        writeBackpropResults(out);
        writePerfResults(out);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "workbench_cli failed: " << e.what() << "\n";
        return 1;
    }
}
