#include <cmath>
#include <string>

#include "csv_nodes.h"
#include "doctest.h"
#include "fileio.h"
#include "parse_util.h"
#include "stl_parser.h"

namespace {

std::string readData(const char* dir, const char* name) {
    return wb::readTextFile(std::string(dir) + "/" + name, wb::kMaxInputFileBytes);
}
std::string bad(const char* name) { return readData(WB_TEST_DATA_DIR, name); }

// Asserts that parsing `text` throws a ParseError whose message contains `needle`.
#define CHECK_STL_REJECTED(file, needle) \
    CHECK_THROWS_WITH_AS(wb::parseAsciiStl(bad(file)), doctest::Contains(needle), wb::ParseError)
#define CHECK_CSV_REJECTED(file, needle) \
    CHECK_THROWS_WITH_AS(wb::parseNodeCsv(bad(file)), doctest::Contains(needle), wb::ParseError)

}  // namespace

TEST_CASE("sample cube.stl parses: 12 triangles, 8 vertices, unit bounding box") {
    const wb::StlMesh m = wb::parseAsciiStl(readData(WB_SAMPLE_DATA_DIR, "cube.stl"));
    CHECK(m.triangleCount() == 12);
    CHECK(m.vertexCount() == 8);
    CHECK(m.name == "cube_1m");
    for (int a = 0; a < 3; ++a) {
        CHECK(m.bbox.min[a] == 0.0);
        CHECK(m.bbox.max[a] == 1.0);
    }
    for (auto idx : m.triangles) CHECK(idx < m.vertexCount());
}

TEST_CASE("STL: malformed files are rejected with line numbers and guidance") {
    CHECK_STL_REJECTED("empty.stl", "empty");
    CHECK_STL_REJECTED("whitespace_only.stl", "empty");
    CHECK_STL_REJECTED("no_triangles.stl", "no triangles");
    CHECK_STL_REJECTED("not_stl.stl", "line 1");
    CHECK_STL_REJECTED("binary.stl", "binary STL");
    CHECK_STL_REJECTED("missing_endloop.stl", "line 7");
    CHECK_STL_REJECTED("two_vertices.stl", "has 2 vertices");
    CHECK_STL_REJECTED("four_vertices.stl", "more than 3 vertices");
    CHECK_STL_REJECTED("nan_vertex.stl", "not a finite number");
    CHECK_STL_REJECTED("inf_vertex.stl", "not a finite number");
    CHECK_STL_REJECTED("bad_number.stl", "not a valid number");
    CHECK_STL_REJECTED("short_vertex.stl", "3 coordinates");
    CHECK_STL_REJECTED("missing_endsolid.stl", "ends unexpectedly");
    CHECK_STL_REJECTED("truncated.stl", "truncated");
    CHECK_STL_REJECTED("misspelled_keyword.stl", "'endlop'");
}

TEST_CASE("STL: error messages name the offending line") {
    try {
        wb::parseAsciiStl(bad("nan_vertex.stl"));
        FAIL("expected ParseError");
    } catch (const wb::ParseError& e) {
        CHECK(std::string(e.what()).find("line 5") != std::string::npos);
    }
}

TEST_CASE("STL: keywords are case-insensitive and CRLF/indentation are tolerated") {
    const std::string s =
        "SOLID t\r\n FACET NORMAL 0 0 1\r\n  OUTER LOOP\r\n   VERTEX 0 0 0\r\n   VERTEX 1e0 0 0\r\n   VERTEX +0 1.5 0\r\n"
        "  ENDLOOP\r\n ENDFACET\r\nENDSOLID t\r\n";
    const wb::StlMesh m = wb::parseAsciiStl(s);
    CHECK(m.triangleCount() == 1);
    CHECK(m.bbox.max[1] == 1.5);
}

TEST_CASE("STL: shared vertices are merged") {
    const wb::StlMesh m = wb::parseAsciiStl(wb::syntheticStlText(2000));
    CHECK(m.triangleCount() == 2000);
    CHECK(m.vertexCount() < 6000);
}

TEST_CASE("CSV: synthetic grid is accepted and statistics are right") {
    const wb::NodeTable t = wb::parseNodeCsv(wb::syntheticGridCsv(11, 11, 11));
    CHECK(t.size() == 1331);
    CHECK(t.bbox.min[0] == 0.0);
    CHECK(t.bbox.max[2] == 1.0);
    CHECK(t.t_min_C >= 20.0);
    CHECK(t.t_max_C <= 100.0);
    CHECK(t.t_max_C > 99.0);  // hot spot sits near (0.2, 0.5, 0.5), a grid node
}

TEST_CASE("CSV: sample file in data/ is labelled synthetic and parses") {
    const std::string text = readData(WB_SAMPLE_DATA_DIR, "synthetic_temperature.csv");
    CHECK(text.find("SYNTHETIC") != std::string::npos);
    CHECK(text.find("NOT an ANSYS export") != std::string::npos);
    CHECK(wb::parseNodeCsv(text).size() == 1331);
}

TEST_CASE("CSV: BOM, CRLF, header whitespace and blank lines are tolerated") {
    const wb::NodeTable t = wb::parseNodeCsv(bad("valid_crlf_bom.csv"));
    REQUIRE(t.size() == 2);
    CHECK(t.temperature_C[1] == 25.5);
    CHECK(t.id[0] == 1);
}

TEST_CASE("CSV: malformed files are rejected with line numbers and guidance") {
    CHECK_CSV_REJECTED("empty.csv", "empty");
    CHECK_CSV_REJECTED("header_only.csv", "no data rows");
    CHECK_CSV_REJECTED("wrong_header.csv", "'id,x,y,z,T'");
    CHECK_CSV_REJECTED("wrong_columns.csv", "line 3: expected 5 columns but found 4");
    CHECK_CSV_REJECTED("non_numeric.csv", "line 3: x_m 'abc' is not a valid number");
    CHECK_CSV_REJECTED("nan_value.csv", "line 2: temperature_C 'NaN' is not finite");
    CHECK_CSV_REJECTED("inf_value.csv", "line 3: x_m 'Inf' is not finite");
    CHECK_CSV_REJECTED("duplicate_id.csv", "line 4: duplicate node_id 1 (first used on line 2)");
    CHECK_CSV_REJECTED("non_integer_id.csv", "node_id '1.5' is not a whole number");
}

TEST_CASE("CSV: many bad rows are summarised, not listed without limit") {
    try {
        wb::parseNodeCsv(bad("many_errors.csv"));
        FAIL("expected ParseError");
    } catch (const wb::ParseError& e) {
        const std::string m = e.what();
        CHECK(m.find("line 2:") != std::string::npos);
        CHECK(m.find("line 11:") != std::string::npos);
        CHECK(m.find("line 12:") == std::string::npos);
        CHECK(m.find("5 more error") != std::string::npos);
    }
}

TEST_CASE("file size limit is enforced with a clear message") {
    CHECK_NOTHROW(wb::checkFileSize(100, 100));
    CHECK_THROWS_WITH_AS(wb::checkFileSize(101, 100), doctest::Contains("limit"), wb::ParseError);
    CHECK_THROWS_WITH_AS(wb::checkFileSize(wb::kMaxInputFileBytes + 1), doctest::Contains("200 MB"), wb::ParseError);
    CHECK_THROWS_WITH_AS(wb::readTextFile(std::string(WB_TEST_DATA_DIR) + "/binary.stl", 10), doctest::Contains("limit"),
                         wb::ParseError);
    CHECK_THROWS_WITH_AS(wb::readTextFile("no_such_file.stl", 100), doctest::Contains("Cannot open"), wb::ParseError);
}
