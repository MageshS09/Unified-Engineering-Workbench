// Geometry routes: ASCII STL and nodal-temperature CSV upload, packaged samples, node lookup.
// Parsing/validation is done by wb_core; this file only converts to and from JSON.
#include <memory>
#include <mutex>

#include "api.h"
#include "api_common.h"
#include "csv_nodes.h"
#include "fileio.h"
#include "parse_util.h"
#include "stl_parser.h"

namespace api {

namespace {

constexpr size_t kMaxDisplayPoints = 200000;  // Plotly stays interactive at this size

// The most recently loaded node table, kept so single nodes can be inspected without re-uploading.
struct LastNodes {
    std::mutex mu;
    std::shared_ptr<const wb::NodeTable> table;
} g_last_nodes;

json bboxJson(const wb::BoundingBox& b) {
    return {{"min", b.min}, {"max", b.max}};
}

json stlJson(const wb::StlMesh& m, const std::string& source) {
    std::vector<double> x, y, z;
    x.reserve(m.vertexCount());
    y.reserve(m.vertexCount());
    z.reserve(m.vertexCount());
    for (size_t i = 0; i < m.vertexCount(); ++i) {
        x.push_back(m.vertices[3 * i]);
        y.push_back(m.vertices[3 * i + 1]);
        z.push_back(m.vertices[3 * i + 2]);
    }
    std::vector<std::uint32_t> a, b, c;
    for (size_t t = 0; t < m.triangleCount(); ++t) {
        a.push_back(m.triangles[3 * t]);
        b.push_back(m.triangles[3 * t + 1]);
        c.push_back(m.triangles[3 * t + 2]);
    }
    return {{"source", source}, {"name", m.name}, {"units", "m (assumed; STL stores no units)"},
            {"triangle_count", m.triangleCount()}, {"vertex_count", m.vertexCount()}, {"bbox", bboxJson(m.bbox)},
            {"x", x}, {"y", y}, {"z", z}, {"i", a}, {"j", b}, {"k", c}};
}

json csvJson(const wb::NodeTable& t, const std::string& source) {
    const size_t stride = (t.size() + kMaxDisplayPoints - 1) / kMaxDisplayPoints;
    std::vector<std::int64_t> id;
    std::vector<double> x, y, z, temp;
    for (size_t i = 0; i < t.size(); i += stride) {
        id.push_back(t.id[i]);
        x.push_back(t.x_m[i]);
        y.push_back(t.y_m[i]);
        z.push_back(t.z_m[i]);
        temp.push_back(t.temperature_C[i]);
    }
    return {{"source", source}, {"units", {{"x_m", "m"}, {"temperature_C", "degC"}}}, {"node_count", t.size()},
            {"displayed_count", id.size()}, {"display_stride", stride}, {"bbox", bboxJson(t.bbox)},
            {"t_min_C", t.t_min_C}, {"t_max_C", t.t_max_C},
            {"node_id", id}, {"x", x}, {"y", y}, {"z", z}, {"T", temp}};
}

json handleStl(const std::string& text, const std::string& source) {
    return stlJson(wb::parseAsciiStl(text), source);
}

json handleCsv(const std::string& text, const std::string& source) {
    auto table = std::make_shared<const wb::NodeTable>(wb::parseNodeCsv(text));
    {
        std::lock_guard<std::mutex> lock(g_last_nodes.mu);
        g_last_nodes.table = table;
    }
    return csvJson(*table, source);
}

// Runs `fn`, mapping parser/IO failures to a 400 response with the message for the user.
template <typename Fn>
void respond(httplib::Response& res, Fn&& fn) {
    try {
        res.set_content(fn().dump(), "application/json");
    } catch (const wb::ParseError& e) {
        sendError(res, 400, e.what());
    } catch (const std::exception& e) {
        sendError(res, 500, std::string("Unexpected error: ") + e.what());
    }
}

}  // namespace

void registerGeometryRoutes(httplib::Server& svr, const std::filesystem::path& data_dir) {
    svr.Post("/api/geometry/stl", [](const httplib::Request& req, httplib::Response& res) {
        respond(res, [&] {
            wb::checkFileSize(req.body.size());
            const std::string name = req.has_header("X-Filename") ? req.get_header_value("X-Filename") : "uploaded.stl";
            return handleStl(req.body, name);
        });
    });
    svr.Post("/api/geometry/csv", [](const httplib::Request& req, httplib::Response& res) {
        respond(res, [&] {
            wb::checkFileSize(req.body.size());
            const std::string name = req.has_header("X-Filename") ? req.get_header_value("X-Filename") : "uploaded.csv";
            return handleCsv(req.body, name);
        });
    });
    svr.Get("/api/geometry/sample/stl", [data_dir](const httplib::Request&, httplib::Response& res) {
        respond(res, [&] {
            return handleStl(wb::readTextFile((data_dir / "cube.stl").string(), wb::kMaxInputFileBytes), "data/cube.stl");
        });
    });
    svr.Get("/api/geometry/sample/csv", [data_dir](const httplib::Request&, httplib::Response& res) {
        respond(res, [&] {
            return handleCsv(wb::readTextFile((data_dir / "synthetic_temperature.csv").string(), wb::kMaxInputFileBytes),
                             "data/synthetic_temperature.csv (SYNTHETIC)");
        });
    });
    svr.Get("/api/geometry/node/:id", [](const httplib::Request& req, httplib::Response& res) {
        std::shared_ptr<const wb::NodeTable> t;
        {
            std::lock_guard<std::mutex> lock(g_last_nodes.mu);
            t = g_last_nodes.table;
        }
        if (!t) return sendError(res, 404, "No node CSV is loaded yet. Load a CSV first.");
        std::int64_t id = 0;
        if (!wb::parseInt64(req.path_params.at("id"), id)) return sendError(res, 400, "node_id must be a whole number.");
        for (size_t i = 0; i < t->size(); ++i) {
            if (t->id[i] != id) continue;
            res.set_content(json{{"node_id", id}, {"x_m", t->x_m[i]}, {"y_m", t->y_m[i]}, {"z_m", t->z_m[i]},
                                 {"temperature_C", t->temperature_C[i]}}.dump(),
                            "application/json");
            return;
        }
        sendError(res, 404, "No node with node_id " + std::to_string(id) + " in the loaded CSV.");
    });
}

}  // namespace api
