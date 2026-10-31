#include "stl_parser.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <limits>
#include <unordered_map>

#include "parse_util.h"

namespace wb {

namespace {

// Line-oriented reader over the whole text; tracks 1-based line numbers for error messages.
class Cursor {
public:
    explicit Cursor(std::string_view text) : text_(text) {}
    int line() const { return line_; }

    // Next line that is not blank; false at end of input.
    bool nextNonBlank(std::string_view& out) {
        while (pos_ < text_.size()) {
            size_t end = text_.find('\n', pos_);
            if (end == std::string_view::npos) end = text_.size();
            std::string_view l = trimSpaces(text_.substr(pos_, end - pos_));
            pos_ = end + 1;
            ++line_;
            if (!l.empty()) {
                out = l;
                return true;
            }
        }
        return false;
    }

private:
    std::string_view text_;
    size_t pos_ = 0;
    int line_ = 0;
};

std::string_view nextToken(std::string_view& s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    size_t j = i;
    while (j < s.size() && s[j] != ' ' && s[j] != '\t') ++j;
    const std::string_view tok = s.substr(i, j - i);
    s.remove_prefix(j);
    return tok;
}

bool equalsNoCase(std::string_view a, const char* b) {
    const size_t n = std::strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != b[i]) return false;
    return true;
}

[[noreturn]] void fail(int line, const std::string& what) {
    throw ParseError("STL error on line " + std::to_string(line) + ": " + what);
}

// An ASCII STL can start with "solid" just like a binary one, so also test the binary size formula
// (80-byte header + uint32 count + 50 bytes per triangle) and NUL bytes.
void rejectBinary(std::string_view text) {
    const char* msg =
        "This looks like a binary STL file. Only ASCII STL is supported: re-export it as ASCII "
        "(\"solid ... endsolid\" text) from your CAD tool.";
    if (text.size() >= 84) {
        std::uint32_t n = 0;
        std::memcpy(&n, text.data() + 80, sizeof n);
        if (84ull + 50ull * n == text.size()) throw ParseError(msg);
    }
    const std::string_view head = text.substr(0, 1024);
    if (head.find('\0') != std::string_view::npos) throw ParseError(msg);
}

double readNumber(std::string_view tok, int line, const char* what) {
    double v = 0;
    switch (parseDouble(tok, v)) {
        case NumberStatus::Ok: return v;
        case NumberStatus::NonFinite: fail(line, std::string(what) + " " + quoted(tok) + " is not a finite number (NaN/Inf are rejected).");
        default: fail(line, std::string(what) + " " + quoted(tok) + " is not a valid number.");
    }
}

struct VertexKey {
    std::array<double, 3> v;
    bool operator==(const VertexKey& o) const { return v == o.v; }
};
struct VertexHash {
    size_t operator()(const VertexKey& k) const {
        size_t h = 1469598103934665603ull;
        for (double d : k.v) {
            std::uint64_t bits;
            d += 0.0;  // folds -0.0 into +0.0
            std::memcpy(&bits, &d, sizeof bits);
            h = (h ^ bits) * 1099511628211ull;
        }
        return h;
    }
};

// Collects triangles and merges identical vertices so the mesh payload stays small.
class MeshBuilder {
public:
    void addTriangle(const std::array<std::array<double, 3>, 3>& tri) {
        for (const auto& p : tri) {
            auto it = index_.find(VertexKey{p});
            if (it == index_.end()) {
                it = index_.emplace(VertexKey{p}, static_cast<std::uint32_t>(mesh.vertices.size() / 3)).first;
                mesh.vertices.insert(mesh.vertices.end(), p.begin(), p.end());
                for (int a = 0; a < 3; ++a) {
                    lo_[a] = std::min(lo_[a], p[a]);
                    hi_[a] = std::max(hi_[a], p[a]);
                }
            }
            mesh.triangles.push_back(it->second);
        }
    }
    StlMesh finish() {
        mesh.bbox.min = lo_;
        mesh.bbox.max = hi_;
        return std::move(mesh);
    }
    StlMesh mesh;

private:
    std::unordered_map<VertexKey, std::uint32_t, VertexHash> index_;
    std::array<double, 3> lo_{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
                              std::numeric_limits<double>::max()};
    std::array<double, 3> hi_{std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest(),
                              std::numeric_limits<double>::lowest()};
};

std::string_view requireLine(Cursor& cur, const char* expected) {
    std::string_view line;
    if (!cur.nextNonBlank(line))
        throw ParseError(std::string("STL error: the file ends unexpectedly after line ") + std::to_string(cur.line()) +
                         " (expected '" + expected + "'). The file may be truncated; re-export it.");
    return line;
}

void expectKeyword(std::string_view& line, const char* keyword, int lineNo) {
    const std::string_view tok = nextToken(line);
    if (!equalsNoCase(tok, keyword))
        fail(lineNo, std::string("expected '") + keyword + "' but found " + quoted(tok) + ".");
}

void expectNoMore(std::string_view line, int lineNo) {
    const std::string_view extra = nextToken(line);
    if (!extra.empty()) fail(lineNo, "unexpected extra text " + quoted(extra) + ".");
}

void readNormal(std::string_view line, int lineNo) {
    expectKeyword(line, "normal", lineNo);
    for (const char* axis : {"normal x", "normal y", "normal z"}) {
        const std::string_view tok = nextToken(line);
        if (tok.empty()) fail(lineNo, "facet normal needs 3 numbers.");
        readNumber(tok, lineNo, axis);
    }
    expectNoMore(line, lineNo);
}

std::array<double, 3> readVertex(std::string_view line, int lineNo) {
    std::array<double, 3> p{};
    static const char* names[] = {"vertex x", "vertex y", "vertex z"};
    for (int a = 0; a < 3; ++a) {
        const std::string_view tok = nextToken(line);
        if (tok.empty()) fail(lineNo, "a vertex needs exactly 3 coordinates (x y z).");
        p[a] = readNumber(tok, lineNo, names[a]);
    }
    expectNoMore(line, lineNo);
    return p;
}

// Reads "facet normal.. / outer loop / 3 x vertex / endloop / endfacet"; `line` is the facet line.
void readFacet(Cursor& cur, std::string_view line, MeshBuilder& builder) {
    const int facetLine = cur.line();
    nextToken(line);  // "facet"
    readNormal(line, facetLine);

    std::string_view l = requireLine(cur, "outer loop");
    int lineNo = cur.line();
    expectKeyword(l, "outer", lineNo);
    expectKeyword(l, "loop", lineNo);

    std::array<std::array<double, 3>, 3> tri{};
    int count = 0;
    for (;;) {
        l = requireLine(cur, "vertex or endloop");
        lineNo = cur.line();
        const std::string_view tok = nextToken(l);
        if (equalsNoCase(tok, "vertex")) {
            if (count == 3) fail(lineNo, "facet starting on line " + std::to_string(facetLine) + " has more than 3 vertices; STL triangles need exactly 3.");
            tri[count++] = readVertex(l, lineNo);
        } else if (equalsNoCase(tok, "endloop")) {
            if (count != 3)
                fail(lineNo, "facet starting on line " + std::to_string(facetLine) + " has " + std::to_string(count) +
                                 " vertices; STL triangles need exactly 3.");
            break;
        } else {
            fail(lineNo, "expected 'vertex' or 'endloop' but found " + quoted(tok) + ".");
        }
    }
    l = requireLine(cur, "endfacet");
    lineNo = cur.line();
    expectKeyword(l, "endfacet", lineNo);
    builder.addTriangle(tri);
}

}  // namespace

StlMesh parseAsciiStl(std::string_view text) {
    rejectBinary(text);
    Cursor cur(text);
    std::string_view line;
    if (!cur.nextNonBlank(line))
        throw ParseError("The STL file is empty (no content or only whitespace). Choose a file that contains at least one facet.");

    std::string_view rest = line;
    if (!equalsNoCase(nextToken(rest), "solid"))
        fail(cur.line(), "an ASCII STL must start with 'solid' but this line begins with " + quoted(nextToken(line)) +
                             ". Check that this is an STL file.");
    MeshBuilder builder;
    builder.mesh.name = std::string(trimSpaces(rest));

    for (;;) {
        line = requireLine(cur, "facet or endsolid");
        std::string_view tail = line;
        const std::string_view tok = nextToken(tail);
        if (equalsNoCase(tok, "endsolid")) break;
        if (!equalsNoCase(tok, "facet")) fail(cur.line(), "expected 'facet' or 'endsolid' but found " + quoted(tok) + ".");
        readFacet(cur, line, builder);
    }
    if (cur.nextNonBlank(line)) fail(cur.line(), "unexpected content after 'endsolid'; only one solid per file is supported.");
    if (builder.mesh.triangles.empty())
        throw ParseError("The STL file is valid but contains no triangles (0 facets). Export a model with a surface mesh.");
    return builder.finish();
}

std::string syntheticStlText(int triangles) {
    std::string out = "solid synthetic\n";
    out.reserve(static_cast<size_t>(triangles) * 200 + 32);
    char buf[320];
    for (int t = 0; t < triangles; ++t) {
        const double x = t % 1000, y = t / 1000;
        std::snprintf(buf, sizeof buf,
                      "facet normal 0 0 1\n outer loop\n  vertex %g %g 0\n  vertex %g %g 0\n  vertex %g %g 0.5\n"
                      " endloop\nendfacet\n",
                      x, y, x + 1, y, x, y + 1);
        out += buf;
    }
    return out + "endsolid synthetic\n";
}

}  // namespace wb
