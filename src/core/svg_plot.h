// Tiny dependency-free SVG line/marker plot with labelled axes and units.
#pragma once
#include <string>
#include <vector>

namespace wb {

struct PlotSeries {
    std::string name;
    std::vector<double> x, y;
    std::string color = "#1f77b4";
    bool markers = false;
    bool dashed = false;
};

struct PlotSpec {
    std::string title, x_label, y_label;  // labels include units, e.g. "T (°C)"
    bool log_x = false, log_y = false;
    std::vector<PlotSeries> series;
};

// Throws std::invalid_argument if there is no plottable data (or non-positive data on a log axis).
std::string renderSvg(const PlotSpec& spec);

}  // namespace wb
