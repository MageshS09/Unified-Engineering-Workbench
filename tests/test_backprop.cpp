#include <cmath>

#include "backprop.h"
#include "doctest.h"

// Reference values come from an independent Python (math.tanh) calculation of the same formulas.
// Note: the build plan quoted J~1.1825425, dJ/dv~-0.7106815, dJ/dw~-1.2094656; those are off in the 6th-7th digit.
namespace {
const wb::BackpropSetup kSetup;
const wb::NeuronParams kStart;
}  // namespace

TEST_CASE("forward pass matches the hand calculation") {
    const wb::Forward f = wb::forwardPass(kSetup, kStart);
    CHECK(f.z == doctest::Approx(0.5));
    CHECK(f.h == doctest::Approx(0.46211715726000974).epsilon(1e-12));
    CHECK(f.y_hat == doctest::Approx(0.46211715726000974).epsilon(1e-12));
    CHECK(f.loss == doctest::Approx(1.1825418189970167).epsilon(1e-12));
}

TEST_CASE("analytic gradients match the hand calculation") {
    const wb::Gradients g = wb::analyticGradients(kSetup, kStart);
    CHECK(g.dc == doctest::Approx(-1.5378828427399902).epsilon(1e-12));
    CHECK(g.dv == doctest::Approx(-0.7106820474859469).epsilon(1e-12));
    CHECK(g.dw == doctest::Approx(-1.2094644752400612).epsilon(1e-12));
    CHECK(g.db == doctest::Approx(-1.2094644752400612).epsilon(1e-12));  // x = 1, so dw == db
}

TEST_CASE("one simultaneous update uses gradients at the original parameters") {
    const wb::NeuronParams n = wb::gradientStep(kSetup, kStart, wb::analyticGradients(kSetup, kStart));
    CHECK(n.w == doctest::Approx(0.6209464475240061).epsilon(1e-12));
    CHECK(n.b == doctest::Approx(0.12094644752400613).epsilon(1e-12));
    CHECK(n.v == doctest::Approx(1.0710682047485947).epsilon(1e-12));
    CHECK(n.c == doctest::Approx(0.15378828427399904).epsilon(1e-12));
}

TEST_CASE("central finite differences agree with analytic gradients within 1e-5") {
    const auto rows = wb::gradientCheck(kSetup, kStart);
    REQUIRE(rows.size() == 4);
    for (const auto& r : rows) {
        INFO("parameter " << r.name);
        CHECK(r.abs_error <= 1e-5);
        CHECK(r.pass);
    }
}

TEST_CASE("a deliberately wrong gradient is caught by the check") {
    wb::BackpropSetup tight = kSetup;
    tight.fd_tolerance = 1e-12;  // numeric noise (~1e-10) must now fail the tolerance
    bool anyFail = false;
    for (const auto& r : wb::gradientCheck(tight, kStart)) anyFail = anyFail || !r.pass;
    CHECK(anyFail);
    const wb::Gradients wrong{-1.2094645 * 2, 0, 0, 0};
    const wb::Gradients num = wb::numericGradients(kSetup, kStart);
    CHECK(std::fabs(wrong.dw - num.dw) > 1e-5);
}

TEST_CASE("training reduces the loss monotonically for 100 steps") {
    const wb::NeuronParams once = wb::gradientStep(kSetup, kStart, wb::analyticGradients(kSetup, kStart));
    const std::vector<double> loss = wb::trainLoss(kSetup, once, 100);
    REQUIRE(loss.size() == 101);
    for (size_t i = 1; i < loss.size(); ++i) CHECK(loss[i] < loss[i - 1]);
    CHECK(loss.back() < loss.front() * 0.5);
}

TEST_CASE("backprop CSV headers") {
    CHECK(wb::lossCsv({1.0, 0.5}) == "step,loss\n0,1\n1,0.5\n");
    CHECK(wb::gradCheckCsv(wb::gradientCheck(kSetup, kStart)).rfind("parameter,analytic,", 0) == 0);
}
