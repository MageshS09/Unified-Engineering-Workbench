#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "doctest.h"
#include "fea1d.h"
#include "fea_study.h"
#include "fileio.h"
#include "jobs.h"
#include "svg_plot.h"

namespace {
wb::RodParams withN(int n) {
    wb::RodParams p;
    p.elements = n;
    return p;
}
}  // namespace

TEST_CASE("midpoint error matches h^2 q / (8k) for N = 10, 20, 40, 80") {
    const int ns[] = {10, 20, 40, 80};
    const double expected[] = {1.25, 0.3125, 0.078125, 0.01953125};
    for (int i = 0; i < 4; ++i) {
        const wb::RodParams p = withN(ns[i]);
        const wb::ErrorMetrics m = wb::computeErrors(p, wb::solveRod(p));
        CHECK(std::fabs(m.max_midpoint_C - expected[i]) <= 1e-6 * expected[i]);
        CHECK(m.max_nodal_C < 1e-9);  // linear elements are nodally exact for this 1D problem
    }
}

TEST_CASE("per-element midpoint errors are consistent with the maximum") {
    const wb::RodParams p = withN(10);
    const wb::FeaSolution s = wb::solveRod(p);
    const wb::MidpointErrors m = wb::midpointErrors(p, s);
    REQUIRE(m.x_m.size() == 10);
    CHECK(m.x_m.front() == doctest::Approx(0.05));
    CHECK(*std::max_element(m.err_C.begin(), m.err_C.end()) == doctest::Approx(wb::computeErrors(p, s).max_midpoint_C));
    // The error of a linear interpolant of a parabola is the same in every element: q h^2 / (8 k).
    for (double e : m.err_C) CHECK(e == doctest::Approx(1.25).epsilon(1e-9));
}

TEST_CASE("observed convergence order is 2") {
    const wb::FeaStudy s = wb::runStudy(wb::RodParams{}, {10, 20, 40, 80});
    CHECK(std::isnan(s.rows[0].observed_order));
    for (size_t i = 1; i < s.rows.size(); ++i) CHECK(s.rows[i].observed_order == doctest::Approx(2.0).epsilon(1e-6));
}

TEST_CASE("maximum temperature is about 188.2 C near x = 0.42 m") {
    const wb::FeaSolution s = wb::solveRod(withN(100));  // node at x = 0.42 exists
    const auto it = std::max_element(s.t_C.begin(), s.t_C.end());
    CHECK(*it == doctest::Approx(188.2).epsilon(1e-6));
    CHECK(s.x_m[it - s.t_C.begin()] == doctest::Approx(0.42).epsilon(1e-9));
}

TEST_CASE("boundary conditions are imposed exactly at both ends") {
    const wb::FeaSolution s = wb::solveRod(withN(10));
    CHECK(s.t_C.front() == 100.0);
    CHECK(s.t_C.back() == 20.0);
}

TEST_CASE("single element has no interior unknowns") {
    const wb::FeaSolution s = wb::solveRod(withN(1));
    REQUIRE(s.t_C.size() == 2);
    CHECK(s.t_C[0] == 100.0);
    CHECK(s.t_C[1] == 20.0);
}

TEST_CASE("pure conduction without generation is linear") {
    wb::RodParams p = withN(7);
    p.q_W_m3 = 0.0;
    const wb::FeaSolution s = wb::solveRod(p);
    for (size_t i = 0; i < s.x_m.size(); ++i) CHECK(s.t_C[i] == doctest::Approx(100.0 - 80.0 * s.x_m[i]));
}

TEST_CASE("invalid inputs are rejected with clear messages") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    wb::RodParams p;
    p.elements = 0;
    CHECK_THROWS_WITH_AS(wb::solveRod(p), doctest::Contains("at least 1"), std::invalid_argument);
    p = {};
    p.length_m = 0.0;
    CHECK_THROWS_WITH_AS(wb::solveRod(p), doctest::Contains("Length L"), std::invalid_argument);
    p = {};
    p.area_m2 = -1e-4;
    CHECK_THROWS_WITH_AS(wb::solveRod(p), doctest::Contains("Area A"), std::invalid_argument);
    p = {};
    p.k_W_mK = 0.0;
    CHECK_THROWS_WITH_AS(wb::solveRod(p), doctest::Contains("Conductivity k"), std::invalid_argument);
    p = {};
    p.q_W_m3 = nan;
    CHECK_THROWS_WITH_AS(wb::solveRod(p), doctest::Contains("Heat generation"), std::invalid_argument);
    p = {};
    p.t_left_C = std::numeric_limits<double>::infinity();
    CHECK_THROWS_AS(wb::solveRod(p), std::invalid_argument);
    CHECK_THROWS_AS(wb::runStudy(wb::RodParams{}, {}), std::invalid_argument);
    CHECK_THROWS_AS(wb::runStudy(wb::RodParams{}, {10, 0}), std::invalid_argument);
}

TEST_CASE("CSV output has the documented columns") {
    const wb::RodParams p = withN(2);
    const std::string csv = wb::solutionCsv(p, wb::solveRod(p));
    CHECK(csv.rfind("x_m,T_fe_C,T_exact_C,abs_err_C\n", 0) == 0);
    const wb::FeaStudy s = wb::runStudy(wb::RodParams{}, {10, 20});
    CHECK(wb::convergenceCsv(s.rows).rfind("N,h_m,max_nodal_err_C,max_midpoint_err_C,observed_order\n", 0) == 0);
}

TEST_CASE("SVG plots carry labelled axes with units") {
    const wb::FeaStudy s = wb::runStudy(wb::RodParams{}, {10, 20, 40, 80});
    const std::string t = wb::temperatureSvg(s), c = wb::convergenceSvg(s);
    CHECK(t.find("x (m)") != std::string::npos);
    CHECK(t.find("T (\xC2\xB0" "C)") != std::string::npos);
    CHECK(c.find("h (m)") != std::string::npos);
    CHECK(c.find("max midpoint error") != std::string::npos);
    wb::PlotSpec empty;
    CHECK_THROWS_AS(wb::renderSvg(empty), std::invalid_argument);
}

TEST_CASE("jobs: completion commits a result, cancellation commits none") {
    wb::JobManager jm;
    const std::string ok = jm.submit([](wb::JobContext& ctx) {
        ctx.setProgress(0.5);
        return nlohmann::json{{"v", 42}};
    });
    const std::string slow = jm.submit([](wb::JobContext& ctx) -> nlohmann::json {
        for (;;) {
            ctx.throwIfCancelled();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
    const std::string bad = jm.submit([](wb::JobContext&) -> nlohmann::json { throw std::runtime_error("boom"); });
    CHECK(jm.cancel(slow));
    CHECK_FALSE(jm.cancel("nope"));

    auto waitDone = [&](const std::string& id) {
        wb::JobSnapshot snap;
        for (int i = 0; i < 1000; ++i) {
            REQUIRE(jm.snapshot(id, snap));
            if (snap.state != wb::JobState::Running) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return snap;
    };
    const wb::JobSnapshot a = waitDone(ok), b = waitDone(slow), c = waitDone(bad);
    CHECK(a.state == wb::JobState::Done);
    CHECK(a.result["v"] == 42);
    CHECK(b.state == wb::JobState::Cancelled);
    CHECK(b.result.is_null());
    CHECK(c.state == wb::JobState::Failed);
    CHECK(c.error == "boom");
}

TEST_CASE("atomic file write replaces content and leaves no temp file") {
    const std::string path = "wb_test_atomic.txt";
    wb::writeFileAtomic(path, "one");
    wb::writeFileAtomic(path, "two");
    CHECK(std::filesystem::file_size(path) == 3);
    CHECK_FALSE(std::filesystem::exists(path + ".tmp"));
    std::filesystem::remove(path);
}
