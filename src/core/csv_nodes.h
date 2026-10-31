// Task B: nodal temperature CSV (ANSYS-style export). Required header:
//   node_id,x_m,y_m,z_m,temperature_C
// Lines starting with '#' are comments (used to label synthetic data); blank lines are ignored.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "stl_parser.h"  // BoundingBox

namespace wb {

struct NodeTable {
    std::vector<std::int64_t> id;
    std::vector<double> x_m, y_m, z_m, temperature_C;
    BoundingBox bbox;
    double t_min_C = 0.0, t_max_C = 0.0;
    std::size_t size() const { return id.size(); }
};

// Throws wb::ParseError. Row errors are collected (up to 10, each with its line number) and reported together.
NodeTable parseNodeCsv(std::string_view text);

// Regular nx*ny*nz grid over the unit cube with a smooth Gaussian hot spot; header comment says SYNTHETIC.
std::string syntheticGridCsv(int nx, int ny, int nz);

}  // namespace wb
