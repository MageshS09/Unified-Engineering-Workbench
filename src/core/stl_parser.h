// Task B: ASCII STL import and validation. Units are not stored in STL; the workbench assumes metres.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace wb {

struct BoundingBox {
    std::array<double, 3> min{0, 0, 0};
    std::array<double, 3> max{0, 0, 0};
};

struct StlMesh {
    std::string name;
    std::vector<double> vertices;         // unique vertices, xyz interleaved
    std::vector<std::uint32_t> triangles;  // 3 vertex indices per triangle
    BoundingBox bbox;
    std::size_t triangleCount() const { return triangles.size() / 3; }
    std::size_t vertexCount() const { return vertices.size() / 3; }
};

// Throws wb::ParseError (message includes the line number) for anything that is not a well-formed
// ASCII STL: binary STL, empty file, missing/misspelled keywords, wrong vertex count, non-finite numbers.
StlMesh parseAsciiStl(std::string_view text);

// Deterministic synthetic ASCII STL (n triangles) for tests and parser benchmarks.
std::string syntheticStlText(int triangles);

}  // namespace wb
