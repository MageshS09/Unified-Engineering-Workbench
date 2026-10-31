// Task F: one-neuron regression, hand-derived backpropagation (no automatic differentiation).
//   z = w x + b,  h = tanh(z),  y_hat = v h + c,  J = 0.5 (y_hat - y)^2
#pragma once
#include <string>
#include <vector>

namespace wb {

struct BackpropSetup {
    double x = 1.0, y = 2.0;
    double learning_rate = 0.1;
    double fd_epsilon = 1e-6;
    double fd_tolerance = 1e-5;
};

struct NeuronParams {
    double w = 0.5, b = 0.0, v = 1.0, c = 0.0;
};

struct Forward {
    double z, h, y_hat, loss;
};

struct Gradients {
    double dw, db, dv, dc;
};

struct GradCheckRow {
    std::string name;
    double analytic, numeric, abs_error;
    bool pass;
};

Forward forwardPass(const BackpropSetup& s, const NeuronParams& p);
Gradients analyticGradients(const BackpropSetup& s, const NeuronParams& p);
Gradients numericGradients(const BackpropSetup& s, const NeuronParams& p);  // central differences
std::vector<GradCheckRow> gradientCheck(const BackpropSetup& s, const NeuronParams& p);

// Simultaneous update: every gradient is taken at the OLD parameters.
NeuronParams gradientStep(const BackpropSetup& s, const NeuronParams& p, const Gradients& g);

// Loss at the initial parameters (index 0), then after each of `steps` updates.
std::vector<double> trainLoss(const BackpropSetup& s, NeuronParams start, int steps);

std::string gradCheckCsv(const std::vector<GradCheckRow>& rows);
std::string lossCsv(const std::vector<double>& loss);  // columns: step,loss

}  // namespace wb
