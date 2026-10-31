#include "backprop.h"

#include <cmath>
#include <cstdio>

namespace wb {

Forward forwardPass(const BackpropSetup& s, const NeuronParams& p) {
    Forward f;
    f.z = p.w * s.x + p.b;
    f.h = std::tanh(f.z);
    f.y_hat = p.v * f.h + p.c;
    const double e = f.y_hat - s.y;
    f.loss = 0.5 * e * e;
    return f;
}

// Chain rule, from the loss back to each parameter:
//   dJ/dy_hat = e = y_hat - y
//   dJ/dc = e,  dJ/dv = e h,  dJ/dh = e v
//   dJ/dz = dJ/dh * (1 - h^2)   [d tanh(z)/dz = 1 - tanh^2(z)]
//   dJ/dw = dJ/dz * x,  dJ/db = dJ/dz
Gradients analyticGradients(const BackpropSetup& s, const NeuronParams& p) {
    const Forward f = forwardPass(s, p);
    const double e = f.y_hat - s.y;
    const double dz = e * p.v * (1.0 - f.h * f.h);
    return {dz * s.x, dz, e * f.h, e};
}

Gradients numericGradients(const BackpropSetup& s, const NeuronParams& p) {
    const double eps = s.fd_epsilon;
    auto central = [&](double NeuronParams::*field) {
        NeuronParams hi = p, lo = p;
        hi.*field += eps;
        lo.*field -= eps;
        return (forwardPass(s, hi).loss - forwardPass(s, lo).loss) / (2.0 * eps);
    };
    return {central(&NeuronParams::w), central(&NeuronParams::b), central(&NeuronParams::v), central(&NeuronParams::c)};
}

std::vector<GradCheckRow> gradientCheck(const BackpropSetup& s, const NeuronParams& p) {
    const Gradients a = analyticGradients(s, p), n = numericGradients(s, p);
    auto row = [&](const char* name, double an, double nu) {
        const double err = std::fabs(an - nu);
        return GradCheckRow{name, an, nu, err, err <= s.fd_tolerance};
    };
    return {row("w", a.dw, n.dw), row("b", a.db, n.db), row("v", a.dv, n.dv), row("c", a.dc, n.dc)};
}

NeuronParams gradientStep(const BackpropSetup& s, const NeuronParams& p, const Gradients& g) {
    const double lr = s.learning_rate;
    return {p.w - lr * g.dw, p.b - lr * g.db, p.v - lr * g.dv, p.c - lr * g.dc};
}

std::vector<double> trainLoss(const BackpropSetup& s, NeuronParams p, int steps) {
    std::vector<double> loss{forwardPass(s, p).loss};
    for (int i = 0; i < steps; ++i) {
        p = gradientStep(s, p, analyticGradients(s, p));
        loss.push_back(forwardPass(s, p).loss);
    }
    return loss;
}

std::string gradCheckCsv(const std::vector<GradCheckRow>& rows) {
    std::string out = "parameter,analytic,numeric_central_diff,abs_error,pass\n";
    char line[200];
    for (const auto& r : rows) {
        std::snprintf(line, sizeof line, "%s,%.12g,%.12g,%.3e,%s\n", r.name.c_str(), r.analytic, r.numeric, r.abs_error,
                      r.pass ? "true" : "false");
        out += line;
    }
    return out;
}

std::string lossCsv(const std::vector<double>& loss) {
    std::string out = "step,loss\n";
    char line[64];
    for (size_t i = 0; i < loss.size(); ++i) {
        std::snprintf(line, sizeof line, "%zu,%.12g\n", i, loss[i]);
        out += line;
    }
    return out;
}

}  // namespace wb
