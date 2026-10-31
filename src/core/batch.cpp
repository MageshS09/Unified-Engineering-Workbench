#include "batch.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <stdexcept>

#include "svg_plot.h"
#include "thread_pool.h"

namespace wb {

namespace {

constexpr double kAgreementToleranceC = 1e-8;
constexpr double kMaxResultBytes = 2.0e9;  // keep the stored temperature fields within ~2 GB

RodParams caseParams(const BatchConfig& cfg, const BatchCase& c) {
    RodParams p;  // L, A and boundary temperatures stay at the task values
    p.k_W_mK = c.k_W_mK;
    p.q_W_m3 = c.q_W_m3;
    p.elements = cfg.mesh;
    return p;
}

double medianOf(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

template <typename Fn>
double timeSeconds(Fn&& fn, bool& completed) {
    const auto t0 = std::chrono::steady_clock::now();
    completed = fn();
    const auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(t1 - t0).count();
}

}  // namespace

void validate(const BatchConfig& cfg) {
    if (cfg.cases < 1 || cfg.cases > 100000)
        throw std::invalid_argument("Case count must be between 1 and 100,000. Enter a whole number in that range.");
    if (cfg.mesh < 1 || cfg.mesh > 200000)
        throw std::invalid_argument("Mesh size must be between 1 and 200,000 elements per case.");
    if (static_cast<double>(cfg.cases) * (cfg.mesh + 1) * sizeof(double) > kMaxResultBytes)
        throw std::invalid_argument("Cases x mesh is too large to hold in memory (limit about 2 GB). Reduce cases or mesh size.");
}

void validate(const PerfConfig& cfg) {
    validate(cfg.batch);
    if (cfg.max_workers < 1 || cfg.max_workers > 64)
        throw std::invalid_argument("Max workers must be between 1 and 64.");
    if (cfg.repetitions < 1 || cfg.repetitions > 50)
        throw std::invalid_argument("Repetitions must be between 1 and 50.");
    if (cfg.warmups < 0 || cfg.warmups > 10) throw std::invalid_argument("Warm-up runs must be between 0 and 10.");
}

std::vector<BatchCase> generateBatch(const BatchConfig& cfg) {
    validate(cfg);
    std::mt19937_64 rng(cfg.seed);
    std::uniform_real_distribution<double> kDist(1.0, 100.0), qDist(1e3, 1e5);
    std::vector<BatchCase> cases(static_cast<size_t>(cfg.cases));
    for (auto& c : cases) {
        c.k_W_mK = kDist(rng);  // separate statements: argument evaluation order is unspecified
        c.q_W_m3 = qDist(rng);
    }
    return cases;
}

bool runSerial(const BatchConfig& cfg, const std::vector<BatchCase>& cases, BatchResults& out, const CancelFn& cancelled) {
    out.assign(cases.size(), {});
    for (size_t i = 0; i < cases.size(); ++i) {
        if (cancelled && cancelled()) return false;
        out[i] = solveRod(caseParams(cfg, cases[i])).t_C;
    }
    return true;
}

bool runParallel(const BatchConfig& cfg, const std::vector<BatchCase>& cases, int workers, BatchResults& out,
                 const CancelFn& cancelled) {
    out.assign(cases.size(), {});  // pre-sized: workers only write their own slot
    return parallelFor(
        cases.size(), workers, [&](size_t i) { out[i] = solveRod(caseParams(cfg, cases[i])).t_C; }, cancelled);
}

double maxAbsDifference(const BatchResults& a, const BatchResults& b) {
    if (a.size() != b.size()) throw std::invalid_argument("Result sets have different case counts.");
    double worst = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].size() != b[i].size()) throw std::invalid_argument("Result sets have different mesh sizes.");
        for (size_t j = 0; j < a[i].size(); ++j) worst = std::max(worst, std::fabs(a[i][j] - b[i][j]));
    }
    return worst;
}

std::vector<int> workerCounts(int max_workers, int logical_cpus) {
    const int cap = std::max(1, std::min(max_workers, logical_cpus));
    std::vector<int> counts;
    for (int p = 1; p <= cap; ++p) counts.push_back(p);
    if (logical_cpus <= 1 && max_workers >= 2) counts.push_back(2);
    return counts;
}

PerfReport runBenchmark(const PerfConfig& cfg, const std::function<void(double)>& onProgress, const CancelFn& cancelled) {
    validate(cfg);
    PerfReport rep;
    rep.config = cfg;
    rep.system = querySystemInfo();
    rep.build = buildInfo();
    const std::vector<BatchCase> cases = generateBatch(cfg.batch);  // outside every timer
    const std::vector<int> counts = workerCounts(cfg.max_workers, rep.system.logical_cores);
    const int runsPerMode = cfg.warmups + cfg.repetitions;
    const int totalRuns = runsPerMode * static_cast<int>(counts.size() + 1);
    int done = 0;

    // Times one mode: warm-ups first (discarded), then the measured repetitions.
    auto measure = [&](PerfRow& row, const std::function<bool()>& run) {
        for (int r = 0; r < runsPerMode; ++r) {
            bool completed = false;
            const double t = timeSeconds(run, completed);
            if (!completed) {
                if (onProgress) onProgress(static_cast<double>(done) / totalRuns);  // lets a job turn this into "cancelled"
                throw std::runtime_error("Benchmark cancelled.");
            }
            if (r >= cfg.warmups) row.times_s.push_back(t);
            if (onProgress) onProgress(static_cast<double>(++done) / totalRuns);
        }
        row.median_s = medianOf(row.times_s);
    };

    BatchResults serialOut, out;
    PerfRow serial;
    serial.mode = "serial";
    measure(serial, [&] { return runSerial(cfg.batch, cases, serialOut, cancelled); });
    rep.rows.push_back(serial);

    for (int p : counts) {
        PerfRow row;
        row.mode = "parallel";
        row.workers = p;
        measure(row, [&] { return runParallel(cfg.batch, cases, p, out, cancelled); });
        row.max_abs_diff_C = maxAbsDifference(serialOut, out);  // last measured repetition vs serial
        rep.rows.push_back(row);
    }

    const double t1 = rep.rows[1].median_s;  // parallel path with one worker
    for (size_t i = 1; i < rep.rows.size(); ++i) {
        PerfRow& r = rep.rows[i];
        r.speedup = t1 / r.median_s;
        r.efficiency = r.speedup / r.workers;
        r.speedup_vs_serial = rep.rows[0].median_s / r.median_s;
        rep.max_abs_diff_C = std::max(rep.max_abs_diff_C, r.max_abs_diff_C);
    }
    rep.agreement_ok = rep.max_abs_diff_C <= kAgreementToleranceC;
    rep.speedup_assessable = rep.system.logical_cores > 1;
    if (!rep.speedup_assessable)
        rep.note = "Only one logical CPU is available: the parallel path was exercised, but speedup cannot be assessed.";
    return rep;
}

std::string perfCsv(const PerfReport& r) {
    std::string out = "mode,workers,t1_s,t2_s,t3_s,t4_s,t5_s,median_s,speedup_T1_over_Tp,efficiency,speedup_vs_serial,max_abs_diff_C\n";
    char buf[512];
    for (const auto& row : r.rows) {
        std::string times;
        for (size_t i = 0; i < 5; ++i) {
            if (i < row.times_s.size()) {
                std::snprintf(buf, sizeof buf, "%.6f", row.times_s[i]);
                times += buf;
            }
            times += ',';
        }
        const bool par = row.mode == "parallel";
        std::snprintf(buf, sizeof buf, "%s,%d,%s%.6f,%s,%s,%s,%.3e\n", row.mode.c_str(), row.workers, times.c_str(),
                      row.median_s, par ? std::to_string(row.speedup).c_str() : "",
                      par ? std::to_string(row.efficiency).c_str() : "",
                      par ? std::to_string(row.speedup_vs_serial).c_str() : "", row.max_abs_diff_C);
        out += buf;
    }
    return out;
}

std::string perfSummaryText(const PerfReport& r) {
    const auto& c = r.config;
    char buf[256];
    std::string s = "Performance run summary (generated by workbench_cli / Launch.exe)\n";
    s += "CPU: " + r.system.cpu_name + "\n";
    std::snprintf(buf, sizeof buf, "Physical cores: %d\nLogical processors: %d\nRAM: %.2f GiB\n", r.system.physical_cores,
                  r.system.logical_cores, r.system.ram_bytes / (1024.0 * 1024.0 * 1024.0));
    s += buf;
    s += "Compiler: " + r.build.compiler + "\nFlags: " + r.build.flags + " (" + r.build.build_type + ")\n";
    std::snprintf(buf, sizeof buf,
                  "Workload: %d independent cases, %d elements each, seed %llu, k in [1,100], q in [1e3,1e5]\n"
                  "Warm-up runs: %d, measured repetitions: %d (median reported)\n"
                  "Timing: std::chrono::steady_clock around the compute call only\n",
                  c.batch.cases, c.batch.mesh, static_cast<unsigned long long>(c.batch.seed), c.warmups, c.repetitions);
    s += buf;
    std::snprintf(buf, sizeof buf, "Max |T_serial - T_parallel| over all cases and nodes: %.3e C (%s, limit 1e-8)\n",
                  r.max_abs_diff_C, r.agreement_ok ? "PASS" : "FAIL");
    s += buf;
    if (!r.note.empty()) s += "NOTE: " + r.note + "\n";
    return s;
}

std::string perfSpeedupSvg(const PerfReport& r) {
    PlotSpec spec;
    spec.title = "Batch solve speedup T1/Tp";
    spec.x_label = "workers p (threads)";
    spec.y_label = "speedup T1/Tp (dimensionless)";
    PlotSeries measured{"measured", {}, {}, "#d62728", true, false};
    PlotSeries ideal{"ideal (= p)", {}, {}, "#000000", false, true};
    for (const auto& row : r.rows) {
        if (row.mode != "parallel") continue;
        measured.x.push_back(row.workers);
        measured.y.push_back(row.speedup);
        ideal.x.push_back(row.workers);
        ideal.y.push_back(row.workers);
    }
    spec.series = {ideal, measured};
    if (!r.speedup_assessable) spec.title += " (1 logical CPU: speedup not assessable)";
    return renderSvg(spec);
}

}  // namespace wb
