#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

#include "batch.h"
#include "doctest.h"
#include "sysinfo.h"
#include "thread_pool.h"

namespace {
wb::BatchConfig small(int cases = 40, int mesh = 200) {
    wb::BatchConfig c;
    c.cases = cases;
    c.mesh = mesh;
    return c;
}
}  // namespace

TEST_CASE("batch generator is reproducible and within the documented ranges") {
    const auto a = wb::generateBatch(small()), b = wb::generateBatch(small());
    REQUIRE(a.size() == 40);
    for (size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].k_W_mK == b[i].k_W_mK);
        CHECK(a[i].q_W_m3 == b[i].q_W_m3);
        CHECK((a[i].k_W_mK >= 1.0 && a[i].k_W_mK <= 100.0));
        CHECK((a[i].q_W_m3 >= 1e3 && a[i].q_W_m3 <= 1e5));
    }
    wb::BatchConfig other = small();
    other.seed = 43;
    CHECK(wb::generateBatch(other)[0].k_W_mK != a[0].k_W_mK);
}

TEST_CASE("invalid batch configurations are rejected") {
    CHECK_THROWS_AS(wb::generateBatch(small(0)), std::invalid_argument);
    CHECK_THROWS_AS(wb::generateBatch(small(10, 0)), std::invalid_argument);
    CHECK_THROWS_WITH_AS(wb::generateBatch(small(100000, 200000)), doctest::Contains("too large"), std::invalid_argument);
    wb::PerfConfig p;
    p.max_workers = 0;
    CHECK_THROWS_AS(wb::validate(p), std::invalid_argument);
}

TEST_CASE("parallel results equal serial results within 1e-8 C, in input order") {
    const wb::BatchConfig cfg = small(60, 300);
    const auto cases = wb::generateBatch(cfg);
    wb::BatchResults serial, par;
    REQUIRE(wb::runSerial(cfg, cases, serial));
    for (int workers : {1, 2, 4}) {
        REQUIRE(wb::runParallel(cfg, cases, workers, par));
        CHECK(wb::maxAbsDifference(serial, par) <= 1e-8);
    }
    // Slot i must hold the solution of case i (not just any permutation).
    wb::RodParams p;
    p.elements = cfg.mesh;
    p.k_W_mK = cases[17].k_W_mK;
    p.q_W_m3 = cases[17].q_W_m3;
    CHECK(wb::solveRod(p).t_C == par[17]);
}

TEST_CASE("parallelFor runs every index exactly once") {
    std::vector<std::atomic<int>> hits(1000);
    for (auto& h : hits) h = 0;
    CHECK(wb::parallelFor(hits.size(), 4, [&](size_t i) { hits[i]++; }));
    for (auto& h : hits) CHECK(h.load() == 1);
}

TEST_CASE("parallelFor propagates a worker exception and honours a stop request") {
    CHECK_THROWS_AS(wb::parallelFor(100, 3, [](size_t i) { if (i == 5) throw std::runtime_error("x"); }), std::runtime_error);
    std::atomic<int> ran{0};
    const bool completed = wb::parallelFor(100000, 2, [&](size_t) { ran++; }, [&] { return ran.load() >= 10; });
    CHECK_FALSE(completed);
    CHECK(ran.load() < 100000);
}

TEST_CASE("cancelled batch run reports incomplete") {
    const wb::BatchConfig cfg = small(50, 100);
    wb::BatchResults out;
    CHECK_FALSE(wb::runParallel(cfg, wb::generateBatch(cfg), 2, out, [] { return true; }));
    CHECK_FALSE(wb::runSerial(cfg, wb::generateBatch(cfg), out, [] { return true; }));
}

TEST_CASE("worker counts are capped at the logical CPUs") {
    CHECK(wb::workerCounts(4, 8) == std::vector<int>{1, 2, 3, 4});
    CHECK(wb::workerCounts(4, 2) == std::vector<int>{1, 2});
    CHECK(wb::workerCounts(4, 1) == std::vector<int>{1, 2});  // parallel path still exercised
}

TEST_CASE("system info is sane") {
    const wb::SystemInfo s = wb::querySystemInfo();
    CHECK(s.logical_cores >= 1);
    CHECK(s.physical_cores >= 1);
    CHECK(s.physical_cores <= s.logical_cores);
    CHECK(s.ram_bytes > 0);
    CHECK_FALSE(s.cpu_name.empty());
}

TEST_CASE("benchmark report is complete and agrees with serial") {
    wb::PerfConfig cfg;
    cfg.batch = small(30, 200);
    cfg.max_workers = 2;
    cfg.repetitions = 3;
    int calls = 0;
    const wb::PerfReport r = wb::runBenchmark(cfg, [&](double) { ++calls; });
    REQUIRE(r.rows.size() >= 3);  // serial + p = 1, 2
    CHECK(r.rows[0].mode == "serial");
    for (const auto& row : r.rows) {
        CHECK(row.times_s.size() == 3);
        CHECK(row.median_s > 0.0);
    }
    CHECK(r.rows[1].speedup == doctest::Approx(1.0));
    CHECK(r.agreement_ok);
    CHECK(calls == (1 + 3) * static_cast<int>(r.rows.size()));
    CHECK(wb::perfCsv(r).rfind("mode,workers,", 0) == 0);
    CHECK(wb::perfSummaryText(r).find("PASS") != std::string::npos);
}

TEST_CASE("benchmark cancellation aborts through the progress callback") {
    wb::PerfConfig cfg;
    cfg.batch = small(30, 200);
    struct Stop {};
    CHECK_THROWS_AS(wb::runBenchmark(cfg, [](double) { throw Stop{}; }), Stop);
}
