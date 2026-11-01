// Compute-related HTTP routes: FEA, backprop, performance benchmark, job control.
#include <cmath>

#include "api.h"
#include "api_common.h"
#include "backprop.h"
#include "batch.h"
#include "fea_study.h"
#include "jobs.h"
#include "sysinfo.h"

namespace api {

namespace {

wb::RodParams rodFromJson(const json& j) {
    wb::RodParams p;
    p.length_m = numberField(j, "length_m", p.length_m);
    p.area_m2 = numberField(j, "area_m2", p.area_m2);
    p.k_W_mK = numberField(j, "k_W_mK", p.k_W_mK);
    p.q_W_m3 = numberField(j, "q_W_m3", p.q_W_m3);
    p.t_left_C = numberField(j, "t_left_C", p.t_left_C);
    p.t_right_C = numberField(j, "t_right_C", p.t_right_C);
    return p;
}

std::vector<int> meshesFromJson(const json& j) {
    std::vector<int> ns;
    if (!j.contains("meshes")) return {10, 20, 40, 80};
    if (!j["meshes"].is_array()) throw std::invalid_argument("Field 'meshes' must be a list of integers.");
    for (const auto& v : j["meshes"]) {
        if (!v.is_number_integer()) throw std::invalid_argument("Each mesh size must be a whole number.");
        ns.push_back(v.get<int>());
    }
    return ns;
}

json feaResultJson(const wb::FeaStudy& study) {
    json solutions = json::array(), rows = json::array();
    json csv = {{"convergence", wb::convergenceCsv(study.rows)}};
    for (const auto& s : study.solutions) {
        wb::RodParams q = study.params;
        q.elements = s.elements;
        json exact = json::array(), err = json::array();
        for (size_t i = 0; i < s.x_m.size(); ++i) {
            exact.push_back(wb::exactTemperature(q, s.x_m[i]));
            err.push_back(std::fabs(s.t_C[i] - exact.back().get<double>()));
        }
        const wb::MidpointErrors mid = wb::midpointErrors(q, s);
        solutions.push_back({{"N", s.elements}, {"h_m", s.h_m}, {"x_m", s.x_m}, {"T_fe_C", s.t_C},
                             {"T_exact_C", exact}, {"abs_err_C", err},
                             {"x_mid_m", mid.x_m}, {"midpoint_err_C", mid.err_C}});
        csv["N" + std::to_string(s.elements)] = wb::solutionCsv(q, s);
    }
    for (const auto& r : study.rows) {
        rows.push_back({{"N", r.elements}, {"h_m", r.h_m}, {"max_nodal_err_C", r.error.max_nodal_C},
                        {"max_midpoint_err_C", r.error.max_midpoint_C},
                        {"observed_order", std::isfinite(r.observed_order) ? json(r.observed_order) : json(nullptr)}});
    }
    return {{"solutions", solutions}, {"convergence", rows}, {"csv", csv}};
}

void registerFeaRoutes(httplib::Server& svr, wb::JobManager& jobs) {
    svr.Post("/api/fea/solve", [&jobs](const httplib::Request& req, httplib::Response& res) {
        try {
            const json body = json::parse(req.body.empty() ? "{}" : req.body);
            const wb::RodParams params = rodFromJson(body);
            const std::vector<int> meshes = meshesFromJson(body);
            wb::RodParams probe = params;
            probe.elements = 1;
            wb::validate(probe);  // fail fast, before starting a job
            wb::validateMeshList(meshes);
            const std::string id = jobs.submit([params, meshes](wb::JobContext& ctx) {
                const wb::FeaStudy study = wb::runStudy(params, meshes, [&ctx](int done, int total) {
                    ctx.throwIfCancelled();
                    ctx.setProgress(static_cast<double>(done) / total);
                });
                return feaResultJson(study);
            });
            res.set_content(json{{"job_id", id}}.dump(), "application/json");
        } catch (const json::exception&) {
            sendError(res, 400, "Request body is not valid JSON. Reload the page and try again.");
        } catch (const std::invalid_argument& e) {
            sendError(res, 400, e.what());
        }
    });
}

json backpropJson() {
    const wb::BackpropSetup setup;
    const wb::NeuronParams start;
    const wb::Forward f = wb::forwardPass(setup, start);
    const wb::Gradients g = wb::analyticGradients(setup, start);
    const wb::NeuronParams next = wb::gradientStep(setup, start, g);
    const std::vector<double> loss = wb::trainLoss(setup, start, 101);
    const auto checkRows = wb::gradientCheck(setup, start);
    json checks = json::array();
    for (const auto& r : checkRows)
        checks.push_back({{"parameter", r.name}, {"analytic", r.analytic}, {"numeric", r.numeric},
                          {"abs_error", r.abs_error}, {"pass", r.pass}});
    return {{"forward", {{"z", f.z}, {"h", f.h}, {"y_hat", f.y_hat}, {"loss", f.loss}}},
            {"gradients", {{"w", g.dw}, {"b", g.db}, {"v", g.dv}, {"c", g.dc}}},
            {"updated", {{"w", next.w}, {"b", next.b}, {"v", next.v}, {"c", next.c}}},
            {"gradcheck", checks},
            {"loss", loss},
            {"csv", {{"loss", wb::lossCsv(loss)}, {"gradcheck", wb::gradCheckCsv(checkRows)}}}};
}

void registerBackpropRoute(httplib::Server& svr) {
    svr.Get("/api/backprop", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(backpropJson().dump(), "application/json");
    });
}

json perfResultJson(const wb::PerfReport& r) {
    json rows = json::array();
    for (const auto& row : r.rows)
        rows.push_back({{"mode", row.mode}, {"workers", row.workers}, {"times_s", row.times_s},
                        {"median_s", row.median_s}, {"speedup", row.speedup}, {"efficiency", row.efficiency},
                        {"speedup_vs_serial", row.speedup_vs_serial}, {"max_abs_diff_C", row.max_abs_diff_C}});
    return {{"rows", rows},
            {"system", {{"cpu", r.system.cpu_name}, {"physical_cores", r.system.physical_cores},
                        {"logical_cores", r.system.logical_cores}, {"ram_bytes", r.system.ram_bytes}}},
            {"build", {{"compiler", r.build.compiler}, {"flags", r.build.flags}, {"type", r.build.build_type}}},
            {"max_abs_diff_C", r.max_abs_diff_C}, {"agreement_ok", r.agreement_ok},
            {"speedup_assessable", r.speedup_assessable}, {"note", r.note},
            {"csv", {{"perf", wb::perfCsv(r)}, {"summary", wb::perfSummaryText(r)}}}};
}

void registerPerfRoutes(httplib::Server& svr, wb::JobManager& jobs) {
    svr.Get("/api/sysinfo", [](const httplib::Request&, httplib::Response& res) {
        const wb::SystemInfo s = wb::querySystemInfo();
        res.set_content(json{{"cpu", s.cpu_name}, {"physical_cores", s.physical_cores},
                             {"logical_cores", s.logical_cores}, {"ram_bytes", s.ram_bytes}}.dump(),
                        "application/json");
    });
    svr.Post("/api/perf/run", [&jobs](const httplib::Request& req, httplib::Response& res) {
        try {
            const json body = json::parse(req.body.empty() ? "{}" : req.body);
            wb::PerfConfig cfg;
            if (body.contains("seed")) {
                if (!body["seed"].is_number_unsigned()) throw std::invalid_argument("Seed must be a non-negative whole number.");
                cfg.batch.seed = body["seed"].get<std::uint64_t>();
            }
            cfg.batch.cases = intField(body, "cases", cfg.batch.cases);
            cfg.batch.mesh = intField(body, "mesh", cfg.batch.mesh);
            cfg.max_workers = intField(body, "max_workers", cfg.max_workers);
            wb::validate(cfg);  // fail fast, before starting a job
            const std::string id = jobs.submit([cfg](wb::JobContext& ctx) {
                const wb::PerfReport rep = wb::runBenchmark(
                    cfg,
                    [&ctx](double f) {
                        ctx.setProgress(f);
                        ctx.throwIfCancelled();
                    },
                    [&ctx] { return ctx.cancelled(); });
                return perfResultJson(rep);
            });
            res.set_content(json{{"job_id", id}}.dump(), "application/json");
        } catch (const json::exception&) {
            sendError(res, 400, "Request body is not valid JSON. Reload the page and try again.");
        } catch (const std::invalid_argument& e) {
            sendError(res, 400, e.what());
        }
    });
}

void registerJobRoutes(httplib::Server& svr, wb::JobManager& jobs) {
    svr.Get("/api/jobs/:id", [&jobs](const httplib::Request& req, httplib::Response& res) {
        wb::JobSnapshot snap;
        if (!jobs.snapshot(req.path_params.at("id"), snap)) return sendError(res, 404, "Unknown job id.");
        json j = {{"state", wb::JobManager::stateName(snap.state)}, {"progress", snap.progress}};
        if (snap.state == wb::JobState::Failed) j["error"] = snap.error;
        if (snap.state == wb::JobState::Done) j["result"] = snap.result;
        res.set_content(j.dump(), "application/json");
    });
    svr.Post("/api/jobs/:id/cancel", [&jobs](const httplib::Request& req, httplib::Response& res) {
        if (!jobs.cancel(req.path_params.at("id"))) return sendError(res, 404, "Unknown job id.");
        res.set_content("{}", "application/json");
    });
}

}  // namespace

void registerComputeRoutes(httplib::Server& svr, wb::JobManager& jobs) {
    registerFeaRoutes(svr, jobs);
    registerBackpropRoute(svr);
    registerPerfRoutes(svr, jobs);
    registerJobRoutes(svr, jobs);
}

}  // namespace api
