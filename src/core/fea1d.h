// Task C: steady 1D heat conduction  -d/dx(kA dT/dx) = qA  on a uniform rod,
// two-node linear elements, Dirichlet temperature at both ends.
#pragma once
#include <limits>
#include <string>
#include <vector>

namespace wb {

struct RodParams {
    double length_m = 1.0;
    double area_m2 = 1e-4;
    double k_W_mK = 10.0;
    double q_W_m3 = 10000.0;
    double t_left_C = 100.0;
    double t_right_C = 20.0;
    int elements = 10;
};

struct FeaSolution {
    int elements = 0;
    double h_m = 0.0;
    std::vector<double> x_m;  // N+1 node positions
    std::vector<double> t_C;  // N+1 nodal temperatures
};

struct ErrorMetrics {
    double max_nodal_C = 0.0;     // |T_i - T_exact(x_i)|
    double max_midpoint_C = 0.0;  // |(T_i+T_{i+1})/2 - T_exact(x_mid)|, FE interpolant
};

struct ConvergenceRow {
    int elements = 0;
    double h_m = 0.0;
    ErrorMetrics error;
    double observed_order = std::numeric_limits<double>::quiet_NaN();  // NaN for the first row
};

// Throws std::invalid_argument with an actionable message.
void validate(const RodParams& p);

// Throws std::invalid_argument for an unusable mesh list.
void validateMeshList(const std::vector<int>& ns);

FeaSolution solveRod(const RodParams& p);
double exactTemperature(const RodParams& p, double x_m);
// Error of the FE interpolant at each element midpoint (one entry per element).
struct MidpointErrors {
    std::vector<double> x_m;
    std::vector<double> err_C;
};
MidpointErrors midpointErrors(const RodParams& p, const FeaSolution& s);
ErrorMetrics computeErrors(const RodParams& p, const FeaSolution& s);
ConvergenceRow makeConvergenceRow(const RodParams& p, const FeaSolution& s);

// Fills observed_order from consecutive rows (log-ratio of midpoint errors vs h).
void fillObservedOrder(std::vector<ConvergenceRow>& rows);

// CSV text (header + rows).
std::string solutionCsv(const RodParams& p, const FeaSolution& s);
std::string convergenceCsv(const std::vector<ConvergenceRow>& rows);

}  // namespace wb
