// Headless reproduction of everything in results/ (grows with each phase).
// Usage: workbench_cli [results_dir]   (default: results/ next to the executable's parent)
#include <windows.h>

#include <chrono>
#include <filesystem>
#include <iostream>

#include "backprop.h"
#include "batch.h"
#include "csv_nodes.h"
#include "fea_study.h"
#include "fileio.h"
#include "stl_parser.h"
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

// Wall-clock time of one parse call (input text is already in memory, so no file I/O is timed).
template <typename Fn>
double timeParse(Fn&& parse) {
    const auto t0 = std::chrono::steady_clock::now();
    parse();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

void benchmarkLine(std::string& csv, const char* name, size_t bytes, size_t items, double seconds) {
    char line[200];
    std::snprintf(line, sizeof line, "%s,%.1f,%zu,%.3f,%.0f\n", name, bytes / 1048576.0, items, seconds, items / seconds);
    csv += line;
    std::cout << "  " << name << ": " << bytes / 1048576.0 << " MB, " << items << " items, " << seconds << " s" << std::endl;
}

void writeGeometryResults(const fs::path& out, const fs::path& dataDir) {
    // Sample data for the Geometry view: a formula-generated, clearly labelled SYNTHETIC grid.
    wb::writeFileAtomic((dataDir / "synthetic_temperature.csv").string(), wb::syntheticGridCsv(11, 11, 11));
    std::cout << "Geometry: wrote data/synthetic_temperature.csv (SYNTHETIC, 1331 nodes)" << std::endl;

    std::string csv = "case,size_MB,items,parse_s,items_per_s\n";
    const std::string stl = wb::syntheticStlText(200000);
    benchmarkLine(csv, "STL 200k triangles", stl.size(), 200000, timeParse([&] { wb::parseAsciiStl(stl); }));
    for (int nz : {100, 300}) {
        const std::string text = wb::syntheticGridCsv(100, 100, nz);
        const size_t nodes = 10000u * nz;
        benchmarkLine(csv, nz == 100 ? "CSV 1M nodes" : "CSV 3M nodes", text.size(), nodes,
                      timeParse([&] { wb::parseNodeCsv(text); }));
    }
    wb::writeFileAtomic((out / "parse_benchmark.csv").string(), csv);
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

    // Independent repeats of the whole benchmark (run 1 is the one in perf.csv) show the run-to-run
    // variability instead of hiding it behind a single lucky or unlucky measurement.
    std::string repeat = "run,workers,median_s,speedup_T1_over_Tp,efficiency\n";
    char line[160];
    for (int run = 1; run <= 3; ++run) {
        const wb::PerfReport r = run == 1 ? rep : wb::runBenchmark(wb::PerfConfig{});
        for (const auto& row : r.rows) {
            if (row.mode != "parallel") continue;
            std::snprintf(line, sizeof line, "%d,%d,%.6f,%.3f,%.3f\n", run, row.workers, row.median_s, row.speedup, row.efficiency);
            repeat += line;
        }
    }
    wb::writeFileAtomic((out / "perf_repeat.csv").string(), repeat);
    std::cout << "Perf: wrote perf_repeat.csv (3 independent runs)" << std::endl;
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
        writeGeometryResults(out, out.parent_path() / "data");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "workbench_cli failed: " << e.what() << "\n";
        return 1;
    }
}
