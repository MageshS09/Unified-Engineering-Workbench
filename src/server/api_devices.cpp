// Hardware-view routes for the SIMULATED devices (see wb_core/devices.h). JSON transport only.
#include <memory>

#include "api.h"
#include "api_common.h"
#include "devices.h"

namespace api {

namespace {

int statusFor(const wb::DeviceError& e) {
    if (e.code() == "timeout") return 504;
    if (e.code() == "not_found") return 404;
    return 409;  // invalid_state: the call does not fit the device's current state
}

void sendDeviceError(httplib::Response& res, const wb::DeviceError& e) {
    res.status = statusFor(e);
    res.set_content(json{{"error", e.what()}, {"code", e.code()}, {"recoverable", e.recoverable()}}.dump(),
                    "application/json");
}

json attemptJson(const wb::ConnectAttempt& a) {
    return {{"attempt", a.number}, {"ok", a.ok}, {"message", a.message}, {"backoff_ms_after", a.backoff_ms_after}};
}

json listJson(const wb::DeviceManager& m) {
    json devices = json::array();
    for (const auto& d : m.discoverAll())
        devices.push_back({{"id", d.id}, {"kind", wb::kindName(d.kind)}, {"name", d.display_name},
                           {"board_class", d.board_class}, {"simulated", d.simulated},
                           {"state", wb::stateName(m.stateOf(d.id))}});
    return {{"label", "SIMULATED"}, {"notice", "All devices are simulated by the C++ backend; no hardware is connected."},
            {"devices", devices}, {"pending_timeouts", m.pendingTimeouts()}};
}

// Runs fn and maps DeviceError to an HTTP error response.
template <typename Fn>
void guarded(httplib::Response& res, Fn&& fn) {
    try {
        res.set_content(fn().dump(), "application/json");
    } catch (const wb::DeviceError& e) {
        sendDeviceError(res, e);
    }
}

}  // namespace

void registerDeviceRoutes(httplib::Server& svr) {
    auto mgr = std::make_shared<wb::DeviceManager>();

    svr.Get("/api/devices", [mgr](const httplib::Request&, httplib::Response& res) { res.set_content(listJson(*mgr).dump(), "application/json"); });

    svr.Get("/api/devices/:id/capabilities", [mgr](const httplib::Request& req, httplib::Response& res) {
        guarded(res, [&] { return mgr->capabilitiesOf(req.path_params.at("id")); });
    });

    svr.Post("/api/devices/:id/connect", [mgr](const httplib::Request& req, httplib::Response& res) {
        guarded(res, [&]() -> json {
            const std::string id = req.path_params.at("id");
            const json body = json::parse(req.body.empty() ? "{}" : req.body, nullptr, false);
            const bool retry = body.is_object() && body.value("retry", false);
            if (!retry) {
                mgr->connect(id);
                return {{"connected", true}, {"state", wb::stateName(mgr->stateOf(id))}, {"attempts", json::array()}};
            }
            const wb::ConnectOutcome out = mgr->connectWithRetry(id);
            json attempts = json::array();
            for (const auto& a : out.attempts) attempts.push_back(attemptJson(a));
            return {{"connected", out.connected}, {"state", wb::stateName(mgr->stateOf(id))}, {"attempts", attempts}};
        });
    });

    svr.Post("/api/devices/:id/disconnect", [mgr](const httplib::Request& req, httplib::Response& res) {
        guarded(res, [&] {
            const std::string id = req.path_params.at("id");
            mgr->disconnect(id);
            return json{{"state", wb::stateName(mgr->stateOf(id))}};
        });
    });

    svr.Get("/api/devices/:id/telemetry", [mgr](const httplib::Request& req, httplib::Response& res) {
        guarded(res, [&] { return mgr->readTelemetry(req.path_params.at("id")); });
    });

    svr.Post("/api/devices/fault", [mgr](const httplib::Request& req, httplib::Response& res) {
        const json body = json::parse(req.body.empty() ? "{}" : req.body, nullptr, false);
        if (!body.is_object() || !body.contains("simulate_timeout") || !body["simulate_timeout"].is_boolean())
            return sendError(res, 400, "Send {\"simulate_timeout\": true|false}.");
        mgr->simulateTimeout(body["simulate_timeout"].get<bool>());
        res.set_content(listJson(*mgr).dump(), "application/json");
    });
}

}  // namespace api
