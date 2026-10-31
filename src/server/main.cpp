// Launch.exe: local HTTP transport for the C++ core. No numerical logic lives here.
#include <windows.h>
#include <shellapi.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

#include "backprop.h"
#include "fea_study.h"
#include "httplib.h"
#include "jobs.h"
#include "json.hpp"
#include "sysinfo.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr int kHeartbeatTimeoutSec = 60;

std::atomic<long long> g_last_heartbeat_ms{0};

long long steadyNowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// All paths are resolved from the executable location, never the CWD.
fs::path exeDirectory() {
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(std::wstring(buf, n)).parent_path();
}

class Logger {
public:
    explicit Logger(const fs::path& file) {
        std::error_code ec;
        fs::create_directories(file.parent_path(), ec);
        out_.open(file, std::ios::app);
    }
    void log(const std::string& msg) {
        std::lock_guard<std::mutex> lock(mu_);
        out_ << msg << '\n';
        out_.flush();
    }

private:
    std::ofstream out_;
    std::mutex mu_;
};

// Milliseconds between process creation and now (called once the server listens).
double msSinceProcessStart() {
    FILETIME create, exit_t, kernel, user, now;
    GetProcessTimes(GetCurrentProcess(), &create, &exit_t, &kernel, &user);
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER c, n;
    c.LowPart = create.dwLowDateTime;
    c.HighPart = create.dwHighDateTime;
    n.LowPart = now.dwLowDateTime;
    n.HighPart = now.dwHighDateTime;
    return static_cast<double>(n.QuadPart - c.QuadPart) / 10000.0;  // 100 ns ticks
}

fs::path findWebRoot(const fs::path& exe_dir) {
    for (const fs::path& candidate : {exe_dir / "app" / "web", exe_dir / ".." / "app" / "web"}) {
        if (fs::exists(candidate / "index.html")) return fs::weakly_canonical(candidate);
    }
    return {};
}

void startHeartbeatWatchdog(httplib::Server& svr, Logger& log) {
    std::thread([&svr, &log] {
        while (svr.is_running()) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            const long long idle = steadyNowMs() - g_last_heartbeat_ms.load();
            if (idle > kHeartbeatTimeoutSec * 1000LL) {
                log.log("no heartbeat for 60 s, shutting down");
                svr.stop();
            }
        }
    }).detach();
}


void sendError(httplib::Response& res, int status, const std::string& message) {
    res.status = status;
    res.set_content(json{{"error", message}}.dump(), "application/json");
}

double numberField(const json& j, const char* key, double fallback) {
    if (!j.contains(key)) return fallback;
    if (!j[key].is_number()) throw std::invalid_argument(std::string("Field '") + key + "' must be a number.");
    return j[key].get<double>();
}

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
        solutions.push_back({{"N", s.elements}, {"h_m", s.h_m}, {"x_m", s.x_m}, {"T_fe_C", s.t_C},
                             {"T_exact_C", exact}, {"abs_err_C", err}});
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

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    const fs::path exe_dir = exeDirectory();
    Logger log(exe_dir / "logs" / "workbench.log");
    log.log("starting Launch.exe");

    const fs::path web_root = findWebRoot(exe_dir);
    if (web_root.empty()) {
        log.log("ERROR: app/web/index.html not found next to the executable");
        MessageBoxW(nullptr, L"The app/web folder is missing next to Launch.exe.\nRe-extract the ZIP completely and try again.",
                    L"Unified Engineering Workbench", MB_ICONERROR);
        return 1;
    }

    httplib::Server svr;
    wb::JobManager jobs;
    svr.set_mount_point("/", web_root.string());
    g_last_heartbeat_ms = steadyNowMs();
    double launch_to_ready_ms = 0.0;

    svr.Get("/api/health", [&](const httplib::Request&, httplib::Response& res) {
        const wb::BuildInfo info = wb::buildInfo();
        json j = {{"status", "ready"},
                  {"launch_to_ready_ms", launch_to_ready_ms},
                  {"compiler", info.compiler},
                  {"flags", info.flags}};
        res.set_content(j.dump(), "application/json");
    });
    svr.Post("/api/heartbeat", [&](const httplib::Request&, httplib::Response& res) {
        g_last_heartbeat_ms = steadyNowMs();
        res.set_content("{}", "application/json");
    });
    svr.Post("/api/shutdown", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"status\":\"shutting down\"}", "application/json");
        std::thread([&svr] {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            svr.stop();
        }).detach();
    });

    registerFeaRoutes(svr, jobs);
    registerBackpropRoute(svr);
    registerJobRoutes(svr, jobs);

    // Loopback only: the workbench is never exposed to the network.
    const int port = svr.bind_to_any_port("127.0.0.1");
    if (port < 0) {
        log.log("ERROR: could not bind a loopback port");
        MessageBoxW(nullptr, L"Could not open a local network port.\nCheck that security software is not blocking Launch.exe.",
                    L"Unified Engineering Workbench", MB_ICONERROR);
        return 1;
    }

    std::thread server_thread([&svr] { svr.listen_after_bind(); });
    svr.wait_until_ready();
    launch_to_ready_ms = msSinceProcessStart();
    log.log("listening on 127.0.0.1:" + std::to_string(port) + ", launch-to-ready " +
            std::to_string(launch_to_ready_ms) + " ms");

    const std::wstring url = L"http://127.0.0.1:" + std::to_wstring(port) + L"/";
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);

    startHeartbeatWatchdog(svr, log);
    server_thread.join();
    log.log("server stopped");
    return 0;
}
