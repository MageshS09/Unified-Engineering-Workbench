// Task D: batch of independent 1D thermal solves, serial vs CPU-parallel, plus the benchmark.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "fea1d.h"
#include "sysinfo.h"

namespace wb {

struct BatchConfig {
    std::uint64_t seed = 42;
    int cases = 400;
    int mesh = 2000;  // elements per case
};

struct BatchCase {
    double k_W_mK;
    double q_W_m3;
};

using BatchResults = std::vector<std::vector<double>>;  // [case][node] temperature in C
using CancelFn = std::function<bool()>;

// Throws std::invalid_argument with an actionable message.
void validate(const BatchConfig& cfg);

// Fixed-seed std::mt19937_64; k ~ U[1,100] W/(m K), q ~ U[1e3,1e5] W/m^3. Same on every run.
std::vector<BatchCase> generateBatch(const BatchConfig& cfg);

// Both runners use the identical solver and write result i to slot i (deterministic order).
// Return false if cancelled before finishing (results are then incomplete and must be discarded).
bool runSerial(const BatchConfig& cfg, const std::vector<BatchCase>& cases, BatchResults& out,
               const CancelFn& cancelled = {});
bool runParallel(const BatchConfig& cfg, const std::vector<BatchCase>& cases, int workers, BatchResults& out,
                 const CancelFn& cancelled = {});

double maxAbsDifference(const BatchResults& a, const BatchResults& b);

struct PerfConfig {
    BatchConfig batch;
    int max_workers = 4;
    int warmups = 1;
    int repetitions = 5;
};

struct PerfRow {
    std::string mode;  // "serial" or "parallel"
    int workers = 1;
    std::vector<double> times_s;  // measured repetitions
    double median_s = 0.0;
    double speedup = 0.0;           // T(1 worker) / T(p); 0 for the serial row
    double efficiency = 0.0;        // speedup / p
    double speedup_vs_serial = 0.0; // pure-serial time / T(p)
    double max_abs_diff_C = 0.0;    // vs the serial results
};

struct PerfReport {
    PerfConfig config;
    SystemInfo system;
    BuildInfo build;
    std::vector<PerfRow> rows;  // serial first, then parallel p = 1..P
    double max_abs_diff_C = 0.0;
    bool agreement_ok = false;  // max_abs_diff_C <= 1e-8
    bool speedup_assessable = true;
    std::string note;
};

void validate(const PerfConfig& cfg);

// Worker counts to measure: 1..min(max_workers, logical CPUs). With one logical CPU the list is {1, 2}
// so the parallel path is still exercised (oversubscribed; speedup then cannot be assessed).
std::vector<int> workerCounts(int max_workers, int logical_cpus);

// Only the compute calls are timed (std::chrono::steady_clock); case generation, verification and
// formatting are outside the timers. `onProgress(fraction)` is called after every run and may throw
// to abort; `cancelled` is polled between solves.
PerfReport runBenchmark(const PerfConfig& cfg, const std::function<void(double)>& onProgress = {},
                        const CancelFn& cancelled = {});

std::string perfCsv(const PerfReport& r);
std::string perfSummaryText(const PerfReport& r);  // contents of results/sysinfo.txt
std::string perfSpeedupSvg(const PerfReport& r);

}  // namespace wb
