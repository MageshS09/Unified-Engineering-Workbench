#include "csv_nodes.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <unordered_map>

#include "parse_util.h"

namespace wb {

namespace {

constexpr const char* kHeader = "node_id,x_m,y_m,z_m,temperature_C";
constexpr size_t kMaxReportedErrors = 10;
const char* const kColumns[5] = {"node_id", "x_m", "y_m", "z_m", "temperature_C"};

// Splits `line` on commas; stores the first 6 fields and returns the true field count.
size_t splitFields(std::string_view line, std::string_view (&f)[6]) {
    size_t n = 0;
    for (;;) {
        const size_t c = line.find(',');
        if (n < 6) f[n] = trimSpaces(line.substr(0, c));
        ++n;
        if (c == std::string_view::npos) return n;
        line.remove_prefix(c + 1);
    }
}

class RowCollector {
public:
    void add(int line, const std::string& msg) {
        if (errors_.size() < kMaxReportedErrors) errors_.push_back("line " + std::to_string(line) + ": " + msg);
        else ++suppressed_;
    }
    bool any() const { return !errors_.empty(); }
    [[noreturn]] void throwAll() const {
        std::string m = "The CSV has errors and was rejected:\n";
        for (const auto& e : errors_) m += "  " + e + "\n";
        if (suppressed_) m += "  ... and " + std::to_string(suppressed_) + " more error(s).\n";
        m += "Fix these rows (expected columns: " + std::string(kHeader) + ") and load the file again.";
        throw ParseError(m);
    }

private:
    std::vector<std::string> errors_;
    size_t suppressed_ = 0;
};

void checkHeader(std::string_view line, int lineNo) {
    std::string_view f[6];
    const size_t n = splitFields(line, f);
    bool ok = n == 5;
    for (size_t i = 0; ok && i < 5; ++i) ok = f[i] == kColumns[i];
    if (!ok)
        throw ParseError("CSV error on line " + std::to_string(lineNo) + ": the header is '" + std::string(trimSpaces(line)) +
                         "' but it must be exactly '" + kHeader + "'. Rename the columns in your export (units: metres and degrees Celsius).");
}

// Parses one data row into `t`; returns false (after recording errors) if the row is unusable.
bool parseRow(std::string_view line, int lineNo, NodeTable& t, std::unordered_map<std::int64_t, int>& seen,
              RowCollector& errors) {
    std::string_view f[6];
    const size_t n = splitFields(line, f);
    if (n != 5) {
        errors.add(lineNo, "expected 5 columns but found " + std::to_string(n) + ".");
        return false;
    }
    std::int64_t id = 0;
    if (!parseInt64(f[0], id)) {
        errors.add(lineNo, "node_id " + quoted(f[0]) + " is not a whole number.");
        return false;
    }
    double v[4];
    for (int c = 0; c < 4; ++c) {
        const NumberStatus st = parseDouble(f[c + 1], v[c]);
        if (st == NumberStatus::Ok) continue;
        errors.add(lineNo, std::string(kColumns[c + 1]) + " " + quoted(f[c + 1]) +
                               (st == NumberStatus::NonFinite ? " is not finite (NaN/Inf are rejected)." : " is not a valid number."));
        return false;
    }
    const auto ins = seen.emplace(id, lineNo);
    if (!ins.second) {
        errors.add(lineNo, "duplicate node_id " + std::to_string(id) + " (first used on line " + std::to_string(ins.first->second) + ").");
        return false;
    }
    t.id.push_back(id);
    t.x_m.push_back(v[0]);
    t.y_m.push_back(v[1]);
    t.z_m.push_back(v[2]);
    t.temperature_C.push_back(v[3]);
    return true;
}

void finalize(NodeTable& t) {
    auto mm = [](const std::vector<double>& v) { return std::minmax_element(v.begin(), v.end()); };
    const auto xs = mm(t.x_m), ys = mm(t.y_m), zs = mm(t.z_m), ts = mm(t.temperature_C);
    t.bbox.min = {*xs.first, *ys.first, *zs.first};
    t.bbox.max = {*xs.second, *ys.second, *zs.second};
    t.t_min_C = *ts.first;
    t.t_max_C = *ts.second;
}

}  // namespace

NodeTable parseNodeCsv(std::string_view text) {
    if (text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);  // UTF-8 BOM from Excel
    NodeTable table;
    RowCollector errors;
    std::unordered_map<std::int64_t, int> seen;
    bool headerSeen = false;
    int lineNo = 0;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view line = trimSpaces(text.substr(pos, end - pos));
        pos = end + 1;
        ++lineNo;
        if (line.empty() || line.front() == '#') continue;
        if (!headerSeen) {
            checkHeader(line, lineNo);
            headerSeen = true;
            continue;
        }
        parseRow(line, lineNo, table, seen, errors);
    }
    if (!headerSeen)
        throw ParseError("The CSV file is empty (no header or data). Export nodal temperatures with the header '" +
                         std::string(kHeader) + "'.");
    if (errors.any()) errors.throwAll();
    if (table.size() == 0)
        throw ParseError("The CSV has the correct header but no data rows. Export at least one node.");
    finalize(table);
    return table;
}

std::string syntheticGridCsv(int nx, int ny, int nz) {
    std::string out =
        "# SYNTHETIC DATA - NOT an ANSYS export. Generated by workbench_cli from a formula (see data/PROVENANCE.md).\n"
        "# T = 20 + 80*exp(-((x-0.2)^2+(y-0.5)^2+(z-0.5)^2)/0.08)  [degC], coordinates in metres, unit cube grid.\n";
    out += kHeader;
    out += '\n';
    char buf[160];
    long long id = 1;
    auto coord = [](int i, int n) { return n > 1 ? static_cast<double>(i) / (n - 1) : 0.0; };
    for (int k = 0; k < nz; ++k)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {
                const double x = coord(i, nx), y = coord(j, ny), z = coord(k, nz);
                const double r2 = (x - 0.2) * (x - 0.2) + (y - 0.5) * (y - 0.5) + (z - 0.5) * (z - 0.5);
                std::snprintf(buf, sizeof buf, "%lld,%.6g,%.6g,%.6g,%.6g\n", id++, x, y, z, 20.0 + 80.0 * std::exp(-r2 / 0.08));
                out += buf;
            }
    return out;
}

}  // namespace wb
