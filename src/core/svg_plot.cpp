#include "svg_plot.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace wb {

namespace {

constexpr double kW = 820, kH = 520, kLeft = 90, kRight = 30, kTop = 50, kBottom = 70;

struct Axis {
    double lo, hi;
    bool log;
    std::vector<double> ticks;
    double map(double v, double p0, double p1) const {  // data -> pixel
        const double a = log ? std::log10(v) : v;
        const double l = log ? std::log10(lo) : lo, h = log ? std::log10(hi) : hi;
        return p0 + (a - l) / (h - l) * (p1 - p0);
    }
};

std::string fmt(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%g", v);
    return b;
}

std::string escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '&') o += "&amp;";
        else if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else o += c;
    }
    return o;
}

Axis makeAxis(double lo, double hi, bool log) {
    Axis a{lo, hi, log, {}};
    if (log) {
        if (lo <= 0.0) throw std::invalid_argument("Log axis needs strictly positive data.");
        a.lo = std::pow(10.0, std::floor(std::log10(lo)));
        a.hi = std::pow(10.0, std::ceil(std::log10(hi)));
        if (a.hi <= a.lo) a.hi = a.lo * 10.0;
        for (double v = a.lo; v <= a.hi * 1.0000001; v *= 10.0) a.ticks.push_back(v);
        return a;
    }
    if (hi <= lo) {
        lo -= 0.5;
        hi += 0.5;
    }
    const double raw = (hi - lo) / 6.0, mag = std::pow(10.0, std::floor(std::log10(raw)));
    const double r = raw / mag, step = (r < 1.5 ? 1 : r < 3.5 ? 2 : r < 7.5 ? 5 : 10) * mag;
    a.lo = std::floor(lo / step) * step;
    a.hi = std::ceil(hi / step) * step;
    for (double v = a.lo; v <= a.hi + step * 0.5; v += step) a.ticks.push_back(std::fabs(v) < step * 1e-9 ? 0.0 : v);
    return a;
}

void dataRange(const PlotSpec& s, bool xAxis, double& lo, double& hi) {
    lo = 1e300;
    hi = -1e300;
    for (const auto& ser : s.series)
        for (double v : (xAxis ? ser.x : ser.y))
            if (std::isfinite(v)) {
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
    if (lo > hi) throw std::invalid_argument("Plot has no finite data to draw.");
}

void drawGrid(std::string& o, const Axis& ax, const Axis& ay, double px0, double px1, double py0, double py1) {
    char b[400];
    for (double t : ax.ticks) {
        const double x = ax.map(t, px0, px1);
        std::snprintf(b, sizeof b,
                      "<line x1=\"%.1f\" y1=\"%g\" x2=\"%.1f\" y2=\"%g\" stroke=\"#ddd\"/>\n"
                      "<text x=\"%.1f\" y=\"%g\" text-anchor=\"middle\">%s</text>\n",
                      x, py0, x, py1, x, py0 + 18, fmt(t).c_str());
        o += b;
    }
    for (double t : ay.ticks) {
        const double y = ay.map(t, py0, py1);
        std::snprintf(b, sizeof b,
                      "<line x1=\"%g\" y1=\"%.1f\" x2=\"%g\" y2=\"%.1f\" stroke=\"#ddd\"/>\n"
                      "<text x=\"%g\" y=\"%.1f\" text-anchor=\"end\">%s</text>\n",
                      px0, y, px1, y, px0 - 8, y + 4, fmt(t).c_str());
        o += b;
    }
}

void drawSeries(std::string& o, const PlotSeries& s, const Axis& ax, const Axis& ay, double px0, double px1, double py0,
                double py1) {
    char b[400];
    std::string pts, dots;
    for (size_t i = 0; i < s.x.size() && i < s.y.size(); ++i) {
        if (!std::isfinite(s.x[i]) || !std::isfinite(s.y[i])) continue;
        const double X = ax.map(s.x[i], px0, px1), Y = ay.map(s.y[i], py0, py1);
        std::snprintf(b, sizeof b, "%.2f,%.2f ", X, Y);
        pts += b;
        if (s.markers) {
            std::snprintf(b, sizeof b, "<circle cx=\"%.2f\" cy=\"%.2f\" r=\"4\" fill=\"%s\"/>\n", X, Y, s.color.c_str());
            dots += b;
        }
    }
    std::snprintf(b, sizeof b, "<polyline fill=\"none\" stroke=\"%s\" stroke-width=\"2\"%s points=\"", s.color.c_str(),
                  s.dashed ? " stroke-dasharray=\"6 4\"" : "");
    o += b + pts + "\"/>\n" + dots;
}

}  // namespace

std::string renderSvg(const PlotSpec& spec) {
    double xl, xh, yl, yh;
    dataRange(spec, true, xl, xh);
    dataRange(spec, false, yl, yh);
    const Axis ax = makeAxis(xl, xh, spec.log_x), ay = makeAxis(yl, yh, spec.log_y);
    const double px0 = kLeft, px1 = kW - kRight, py0 = kH - kBottom, py1 = kTop;  // pixel y grows downwards

    std::string o;
    char b[500];
    std::snprintf(b, sizeof b,
                  "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%g\" height=\"%g\" viewBox=\"0 0 %g %g\" "
                  "font-family=\"Arial, sans-serif\" font-size=\"13\">\n"
                  "<rect width=\"100%%\" height=\"100%%\" fill=\"white\"/>\n",
                  kW, kH, kW, kH);
    o += b;
    std::snprintf(b, sizeof b, "<text x=\"%g\" y=\"28\" font-size=\"17\" text-anchor=\"middle\">%s</text>\n", kW / 2,
                  escape(spec.title).c_str());
    o += b;
    drawGrid(o, ax, ay, px0, px1, py0, py1);
    std::snprintf(b, sizeof b, "<rect x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\" fill=\"none\" stroke=\"black\"/>\n", px0,
                  py1, px1 - px0, py0 - py1);
    o += b;
    std::snprintf(b, sizeof b, "<text x=\"%g\" y=\"%g\" text-anchor=\"middle\" font-size=\"15\">%s</text>\n",
                  (px0 + px1) / 2, kH - 20, escape(spec.x_label).c_str());
    o += b;
    std::snprintf(b, sizeof b,
                  "<text transform=\"translate(22 %g) rotate(-90)\" text-anchor=\"middle\" font-size=\"15\">%s</text>\n",
                  (py0 + py1) / 2, escape(spec.y_label).c_str());
    o += b;

    double legendY = py1 + 16;
    for (const auto& s : spec.series) {
        drawSeries(o, s, ax, ay, px0, px1, py0, py1);
        std::snprintf(b, sizeof b,
                      "<line x1=\"%g\" y1=\"%g\" x2=\"%g\" y2=\"%g\" stroke=\"%s\" stroke-width=\"2\"%s/>\n"
                      "<text x=\"%g\" y=\"%g\">%s</text>\n",
                      px1 - 170, legendY, px1 - 145, legendY, s.color.c_str(), s.dashed ? " stroke-dasharray=\"6 4\"" : "",
                      px1 - 138, legendY + 4, escape(s.name).c_str());
        o += b;
        legendY += 18;
    }
    return o + "</svg>\n";
}

}  // namespace wb
