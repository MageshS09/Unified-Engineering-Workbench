#include "fea1d.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace wb {

namespace {

void requirePositiveFinite(double v, const char* name, const char* unit) {
    if (!std::isfinite(v) || v <= 0.0) {
        throw std::invalid_argument(std::string(name) + " must be a finite number greater than 0 " + unit +
                                    ". Enter a positive value.");
    }
}

void requireFinite(double v, const char* name) {
    if (!std::isfinite(v)) {
        throw std::invalid_argument(std::string(name) + " must be a finite number. Enter a numeric value.");
    }
}

// Symmetric tridiagonal system: diag[i], off[i] couples node i and i+1.
struct Tridiagonal {
    std::vector<double> diag, off, rhs;
};

// Element-by-element assembly: Ke = (kA/h)[[1,-1],[-1,1]], Fe = (qAh/2)[1,1].
Tridiagonal assemble(const RodParams& p, double h) {
    const int n = p.elements;
    Tridiagonal sys{std::vector<double>(n + 1, 0.0), std::vector<double>(n, 0.0), std::vector<double>(n + 1, 0.0)};
    const double kOverH = p.k_W_mK * p.area_m2 / h;
    const double fe = p.q_W_m3 * p.area_m2 * h / 2.0;
    for (int e = 0; e < n; ++e) {
        sys.diag[e] += kOverH;
        sys.diag[e + 1] += kOverH;
        sys.off[e] += -kOverH;
        sys.rhs[e] += fe;
        sys.rhs[e + 1] += fe;
    }
    return sys;
}

// Thomas algorithm for a symmetric tridiagonal system; the solution overwrites b.
void thomasSolve(std::vector<double> d, const std::vector<double>& o, std::vector<double>& b) {
    const size_t n = d.size();
    for (size_t i = 1; i < n; ++i) {
        const double w = o[i - 1] / d[i - 1];
        d[i] -= w * o[i - 1];
        b[i] -= w * b[i - 1];
    }
    b[n - 1] /= d[n - 1];
    for (size_t i = n - 1; i-- > 0;) b[i] = (b[i] - o[i] * b[i + 1]) / d[i];
}

// Dirichlet BCs by elimination: move K*T_known to the load vector and solve only the
// interior unknowns. This keeps the reduced matrix symmetric positive definite.
void solveInterior(const Tridiagonal& sys, const RodParams& p, std::vector<double>& t) {
    const int n = p.elements;
    t.assign(n + 1, 0.0);
    t[0] = p.t_left_C;
    t[n] = p.t_right_C;
    if (n < 2) return;  // no interior nodes

    const int m = n - 1;  // unknown nodes 1..n-1
    std::vector<double> d(sys.diag.begin() + 1, sys.diag.begin() + n);
    std::vector<double> o(sys.off.begin() + 1, sys.off.begin() + n - 1);
    std::vector<double> b(sys.rhs.begin() + 1, sys.rhs.begin() + n);
    b[0] -= sys.off[0] * p.t_left_C;
    b[m - 1] -= sys.off[n - 1] * p.t_right_C;
    thomasSolve(d, o, b);
    std::copy(b.begin(), b.end(), t.begin() + 1);
}

}  // namespace

void validate(const RodParams& p) {
    if (p.elements < 1) throw std::invalid_argument("Number of elements N must be at least 1. Enter an integer >= 1.");
    if (p.elements > 5000000) throw std::invalid_argument("Number of elements N is too large (max 5,000,000). Reduce N.");
    requirePositiveFinite(p.length_m, "Length L", "(m)");
    requirePositiveFinite(p.area_m2, "Area A", "(m^2)");
    requirePositiveFinite(p.k_W_mK, "Conductivity k", "(W/(m K))");
    requireFinite(p.q_W_m3, "Heat generation q");
    requireFinite(p.t_left_C, "T(0)");
    requireFinite(p.t_right_C, "T(L)");
}

void validateMeshList(const std::vector<int>& ns) {
    if (ns.empty()) throw std::invalid_argument("Mesh list is empty. Enter element counts such as 10,20,40,80.");
    if (ns.size() > 12) throw std::invalid_argument("Too many meshes (max 12). Shorten the list.");
    for (int n : ns) {
        if (n < 1 || n > 1000000)
            throw std::invalid_argument("Each mesh size must be an integer between 1 and 1,000,000.");
    }
}

FeaSolution solveRod(const RodParams& p) {
    validate(p);
    FeaSolution s;
    s.elements = p.elements;
    s.h_m = p.length_m / p.elements;
    s.x_m.resize(p.elements + 1);
    for (int i = 0; i <= p.elements; ++i) s.x_m[i] = s.h_m * i;
    s.x_m.back() = p.length_m;
    solveInterior(assemble(p, s.h_m), p, s.t_C);
    return s;
}

double exactTemperature(const RodParams& p, double x) {
    const double L = p.length_m;
    return p.t_left_C + (p.t_right_C - p.t_left_C) * x / L + p.q_W_m3 / (2.0 * p.k_W_mK) * x * (L - x);
}

ErrorMetrics computeErrors(const RodParams& p, const FeaSolution& s) {
    ErrorMetrics m;
    for (size_t i = 0; i < s.x_m.size(); ++i)
        m.max_nodal_C = std::max(m.max_nodal_C, std::fabs(s.t_C[i] - exactTemperature(p, s.x_m[i])));
    for (size_t i = 0; i + 1 < s.x_m.size(); ++i) {
        const double xm = 0.5 * (s.x_m[i] + s.x_m[i + 1]);
        const double fe = 0.5 * (s.t_C[i] + s.t_C[i + 1]);  // linear shape functions evaluated at xi = 0
        m.max_midpoint_C = std::max(m.max_midpoint_C, std::fabs(fe - exactTemperature(p, xm)));
    }
    return m;
}

ConvergenceRow makeConvergenceRow(const RodParams& p, const FeaSolution& s) {
    ConvergenceRow r;
    r.elements = s.elements;
    r.h_m = s.h_m;
    r.error = computeErrors(p, s);
    return r;
}

void fillObservedOrder(std::vector<ConvergenceRow>& rows) {
    for (size_t i = 1; i < rows.size(); ++i) {
        const double e0 = rows[i - 1].error.max_midpoint_C, e1 = rows[i].error.max_midpoint_C;
        if (e0 > 0.0 && e1 > 0.0 && rows[i].h_m != rows[i - 1].h_m)
            rows[i].observed_order = std::log(e0 / e1) / std::log(rows[i - 1].h_m / rows[i].h_m);
    }
}

std::string solutionCsv(const RodParams& p, const FeaSolution& s) {
    std::string out = "x_m,T_fe_C,T_exact_C,abs_err_C\n";
    char line[160];
    for (size_t i = 0; i < s.x_m.size(); ++i) {
        const double ex = exactTemperature(p, s.x_m[i]);
        std::snprintf(line, sizeof line, "%.12g,%.12g,%.12g,%.6e\n", s.x_m[i], s.t_C[i], ex, std::fabs(s.t_C[i] - ex));
        out += line;
    }
    return out;
}

std::string convergenceCsv(const std::vector<ConvergenceRow>& rows) {
    std::string out = "N,h_m,max_nodal_err_C,max_midpoint_err_C,observed_order\n";
    char line[200];
    for (const auto& r : rows) {
        char order[32] = "";
        if (std::isfinite(r.observed_order)) std::snprintf(order, sizeof order, "%.6f", r.observed_order);
        std::snprintf(line, sizeof line, "%d,%.12g,%.6e,%.12g,%s\n", r.elements, r.h_m, r.error.max_nodal_C,
                      r.error.max_midpoint_C, order);
        out += line;
    }
    return out;
}

}  // namespace wb
