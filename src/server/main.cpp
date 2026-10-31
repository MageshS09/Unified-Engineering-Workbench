// Launch.exe: local HTTP transport for the C++ core. No numerical logic lives here.
#include <windows.h>
#include <shellapi.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

#include "httplib.h"
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
