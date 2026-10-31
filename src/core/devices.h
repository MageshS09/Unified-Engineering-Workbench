// Task E: SIMULATED device adapters (MCU board, FPGA board, JTAG debug adapter).
// Nothing here talks to hardware. Every device, capability report and telemetry message carries
// simulated = true and the label "SIMULATED".
#pragma once
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include "json.hpp"

namespace wb {

enum class DeviceKind { Mcu, Fpga, JtagAdapter };
enum class ConnectionState { Disconnected, Connected, Failed };

const char* kindName(DeviceKind k);
const char* stateName(ConnectionState s);

struct DeviceInfo {
    std::string id;           // stable key used by the API ("mcu0", "fpga0", "jtag0")
    DeviceKind kind;
    std::string display_name;  // always starts with "[SIMULATED]"
    std::string board_class;   // what real hardware this mock stands in for (an assumption, not a detected part)
    bool simulated = true;
};

// Error codes: "timeout" (recoverable, retry), "invalid_state" (wrong call order), "not_found".
class DeviceError : public std::runtime_error {
public:
    DeviceError(std::string code, const std::string& message, bool recoverable)
        : std::runtime_error(message), code_(std::move(code)), recoverable_(recoverable) {}
    const std::string& code() const { return code_; }
    bool recoverable() const { return recoverable_; }

private:
    std::string code_;
    bool recoverable_;
};

// Shared fault injector: the next `remaining` connect attempts time out, then the fault clears.
class FaultInjector {
public:
    void arm(int timeouts);
    void clear();
    bool consume();  // true if this attempt should time out
    int remaining() const;

private:
    mutable std::mutex mu_;
    int remaining_ = 0;
};

class IDevice {
public:
    virtual ~IDevice() = default;
    virtual DeviceInfo discover() const = 0;
    virtual nlohmann::json capabilities() const = 0;
    virtual void connect() = 0;     // throws DeviceError
    virtual void disconnect() = 0;  // throws DeviceError if not connected/failed
    virtual nlohmann::json readTelemetry() = 0;  // one message; throws DeviceError if not connected
    virtual ConnectionState state() const = 0;
};

std::unique_ptr<IDevice> makeMockMcu(std::shared_ptr<FaultInjector> faults);
std::unique_ptr<IDevice> makeMockFpga(std::shared_ptr<FaultInjector> faults);
std::unique_ptr<IDevice> makeMockJtagAdapter(std::shared_ptr<FaultInjector> faults);

struct RetryPolicy {
    int max_attempts = 4;
    int initial_backoff_ms = 100;
    int backoff_factor = 2;
};

struct ConnectAttempt {
    int number = 0;
    bool ok = false;
    std::string message;
    int backoff_ms_after = 0;  // wait before the next attempt (0 if none)
};

struct ConnectOutcome {
    bool connected = false;
    std::vector<ConnectAttempt> attempts;
};

using SleepFn = std::function<void(int ms)>;

// Thread-safe owner of the three simulated devices.
class DeviceManager {
public:
    DeviceManager();

    std::vector<DeviceInfo> discoverAll() const;
    ConnectionState stateOf(const std::string& id) const;
    nlohmann::json capabilitiesOf(const std::string& id) const;

    void connect(const std::string& id);  // single attempt
    // Retries recoverable failures with exponential backoff; the sleeps happen outside the lock.
    ConnectOutcome connectWithRetry(const std::string& id, const RetryPolicy& policy = {}, const SleepFn& sleep = {});
    void disconnect(const std::string& id);
    nlohmann::json readTelemetry(const std::string& id);

    // Fault demo: the next `timeouts` connect attempts time out (0 disables).
    void simulateTimeout(bool on, int timeouts = 2);
    int pendingTimeouts() const { return faults_->remaining(); }

private:
    IDevice& find(const std::string& id) const;  // caller holds mu_

    std::shared_ptr<FaultInjector> faults_;
    std::vector<std::unique_ptr<IDevice>> devices_;
    mutable std::mutex mu_;
};

}  // namespace wb
