#include "fea_study.h"

#include <algorithm>

#include "svg_plot.h"

namespace wb {

FeaStudy runStudy(const RodParams& p, const std::vector<int>& meshes, const std::function<void(int, int)>& onStep) {
    validateMeshList(meshes);
    FeaStudy study;
    study.params = p;
    const int total = static_cast<int>(meshes.size());
    for (int i = 0; i < total; ++i) {
        RodParams q = p;
        q.elements = meshes[i];
        study.solutions.push_back(solveRod(q));
        study.rows.push_back(makeConvergenceRow(q, study.solutions.back()));
        if (onStep) onStep(i + 1, total);
    }
    fillObservedOrder(study.rows);
    return study;
}

std::string temperatureSvg(const FeaStudy& s) {
    static const char* colors[] = {"#d62728", "#2ca02c", "#9467bd", "#ff7f0e", "#8c564b", "#17becf"};
    PlotSpec spec;
    spec.title = "1D rod: FE temperature vs exact";
    spec.x_label = "x (m)";
    spec.y_label = "T (\xC2\xB0" "C)";
    PlotSeries exact{"exact", {}, {}, "#000000", false, true};
    const int pts = 200;
    for (int i = 0; i <= pts; ++i) {
        const double x = s.params.length_m * i / pts;
        exact.x.push_back(x);
        exact.y.push_back(exactTemperature(s.params, x));
    }
    spec.series.push_back(exact);
    size_t c = 0;
    for (const auto& sol : s.solutions) {
        spec.series.push_back({"N = " + std::to_string(sol.elements), sol.x_m, sol.t_C, colors[c++ % 6], sol.elements <= 20, false});
    }
    return renderSvg(spec);
}

std::string convergenceSvg(const FeaStudy& s) {
    PlotSpec spec;
    spec.title = "Mesh convergence (element midpoints)";
    spec.x_label = "h (m)";
    spec.y_label = "max midpoint error (\xC2\xB0" "C)";
    spec.log_x = spec.log_y = true;
    PlotSeries ser{"max midpoint error", {}, {}, "#1f77b4", true, false};
    for (const auto& r : s.rows) {
        if (r.error.max_midpoint_C <= 0.0) continue;  // cannot be drawn on a log axis
        ser.x.push_back(r.h_m);
        ser.y.push_back(r.error.max_midpoint_C);
    }
    spec.series.push_back(ser);
    return renderSvg(spec);
}

}  // namespace wb
