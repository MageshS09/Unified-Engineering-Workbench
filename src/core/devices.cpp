#include "devices.h"

#include <chrono>
#include <cmath>
#include <thread>

namespace wb {

using nlohmann::json;

const char* kindName(DeviceKind k) {
    switch (k) {
        case DeviceKind::Mcu: return "microcontroller";
        case DeviceKind::Fpga: return "fpga";
        case DeviceKind::JtagAdapter: return "jtag_adapter";
    }
    return "unknown";
}

const char* stateName(ConnectionState s) {
    switch (s) {
        case ConnectionState::Disconnected: return "disconnected";
        case ConnectionState::Connected: return "connected";
        case ConnectionState::Failed: return "failed";
    }
    return "unknown";
}

void FaultInjector::arm(int timeouts) {
    std::lock_guard<std::mutex> lock(mu_);
    remaining_ = timeouts < 0 ? 0 : timeouts;
}
void FaultInjector::clear() { arm(0); }
bool FaultInjector::consume() {
    std::lock_guard<std::mutex> lock(mu_);
    if (remaining_ <= 0) return false;
    --remaining_;
    return true;
}
int FaultInjector::remaining() const {
    std::lock_guard<std::mutex> lock(mu_);
    return remaining_;
}

namespace {

// Shared connection state machine: Disconnected -> Connected, any failure -> Failed (retry allowed).
class MockDevice : public IDevice {
public:
    MockDevice(DeviceInfo info, std::shared_ptr<FaultInjector> faults) : info_(std::move(info)), faults_(std::move(faults)) {}

    DeviceInfo discover() const override { return info_; }
    ConnectionState state() const override { return state_; }

    void connect() override {
        if (state_ == ConnectionState::Connected)
            throw DeviceError("invalid_state", info_.display_name + " is already connected. Disconnect it first.", false);
        if (faults_ && faults_->consume()) {
            state_ = ConnectionState::Failed;
            throw DeviceError("timeout",
                              "SIMULATED timeout: " + info_.display_name + " did not answer within 500 ms. This is recoverable: retry (with backoff).",
                              true);
        }
        state_ = ConnectionState::Connected;
        seq_ = 0;
    }

    void disconnect() override {
        if (state_ == ConnectionState::Disconnected)
            throw DeviceError("invalid_state", info_.display_name + " is not connected.", false);
        state_ = ConnectionState::Disconnected;  // also resets a Failed connection
    }

    json readTelemetry() override {
        if (state_ != ConnectionState::Connected)
            throw DeviceError("invalid_state", info_.display_name + " is not connected. Connect it before reading telemetry.", false);
        json msg = {{"label", "SIMULATED"}, {"simulated", true}, {"device_id", info_.id},
                    {"kind", kindName(info_.kind)}, {"seq", seq_}, {"uptime_ms", seq_ * 1000}};
        msg["values"] = values(seq_);
        ++seq_;
        return msg;
    }

protected:
    virtual json values(long long seq) const = 0;
    json base(const char* summary, json extra) const {
        extra["label"] = "SIMULATED";
        extra["simulated"] = true;
        extra["summary"] = summary;
        return extra;
    }
    DeviceInfo info_;

private:
    std::shared_ptr<FaultInjector> faults_;
    ConnectionState state_ = ConnectionState::Disconnected;
    long long seq_ = 0;
};

// Small deterministic wobble so successive telemetry messages differ.
double wobble(long long seq, double amplitude) { return amplitude * std::sin(0.7 * static_cast<double>(seq)); }

class MockMcu : public MockDevice {
public:
    explicit MockMcu(std::shared_ptr<FaultInjector> f)
        : MockDevice({"mcu0", DeviceKind::Mcu, "[SIMULATED] Microcontroller board", "STM32 Nucleo-class board (assumed), 3.3 V logic", true},
                     std::move(f)) {}
    json capabilities() const override {
        return base("Simulated microcontroller capabilities",
                    {{"core", "Arm Cortex-M class (assumed)"}, {"io_voltage_V", 3.3},
                     {"host_link", "USB virtual COM port, 115200 8N1 (assumed)"},
                     {"peripherals", {"GPIO", "12-bit ADC", "UART", "SPI", "I2C"}},
                     {"debug_interface", "SWD / JTAG header (needs an external debug adapter or on-board probe)"},
                     {"telemetry", {"supply_V", "core_temp_C", "adc0_counts", "uptime_ms"}}});
    }

protected:
    json values(long long s) const override {
        return {{"supply_V", 3.30 + wobble(s, 0.01)}, {"core_temp_C", 31.0 + wobble(s, 0.5)},
                {"adc0_counts", 2048 + static_cast<int>(std::lround(wobble(s, 40)))}};
    }
};

class MockFpga : public MockDevice {
public:
    explicit MockFpga(std::shared_ptr<FaultInjector> f)
        : MockDevice({"fpga0", DeviceKind::Fpga, "[SIMULATED] FPGA board", "Artix-7 development-board class (assumed), 3.3 V I/O bank", true},
                     std::move(f)) {}
    json capabilities() const override {
        return base("Simulated FPGA board capabilities",
                    {{"family", "Xilinx Artix-7 class (assumed)"}, {"io_voltage_V", 3.3},
                     {"core_rails_V", {{"VCCINT", 1.0}, {"VCCAUX", 1.8}}},
                     {"host_link", "USB-UART bridge (assumed); configuration over JTAG"},
                     {"configuration", "JTAG (dedicated TCK/TMS/TDI/TDO pins) - real programming is NOT implemented"},
                     {"telemetry", {"vccint_V", "vccaux_V", "vcco_V", "die_temp_C", "config_done"}}});
    }

protected:
    json values(long long s) const override {
        return {{"vccint_V", 1.00 + wobble(s, 0.005)}, {"vccaux_V", 1.80 + wobble(s, 0.01)},
                {"vcco_V", 3.30 + wobble(s, 0.01)}, {"die_temp_C", 42.0 + wobble(s, 1.0)}, {"config_done", true}};
    }
};

class MockJtagAdapter : public MockDevice {
public:
    explicit MockJtagAdapter(std::shared_ptr<FaultInjector> f)
        : MockDevice({"jtag0", DeviceKind::JtagAdapter, "[SIMULATED] JTAG debug adapter", "FT2232H / J-Link-class USB debug probe (assumed), VTref-sensed I/O", true},
                     std::move(f)) {}
    json capabilities() const override {
        return base("Simulated JTAG debug adapter capabilities",
                    {{"role", "debug interface (IEEE 1149.1 TAP master), NOT a compute board"},
                     {"compute", false}, {"runs_user_application", false},
                     {"io_voltage", "follows target VTref (adapter never supplies the target)"},
                     {"level_translation", "buffers powered from VTref (a bare FT2232H has 3.3 V I/O cells and does not track VTref itself)"},
                     {"signals", {{"TCK", "adapter output"}, {"TMS", "adapter output"}, {"TDI", "adapter output"},
                                  {"TDO", "target output"}, {"TRST_n", "adapter output, optional"},
                                  {"VTref", "target output, adapter input"}}},
                     {"tck_max_Hz", 30000000},
                     {"host_link", "USB 2.0 high speed (assumed)"},
                     {"telemetry", {"vtref_V", "tck_Hz", "target_detected"}}});
    }

protected:
    json values(long long s) const override {
        return {{"vtref_V", 3.30 + wobble(s, 0.01)}, {"tck_Hz", 10000000}, {"target_detected", true}};
    }
};

}  // namespace

std::unique_ptr<IDevice> makeMockMcu(std::shared_ptr<FaultInjector> f) { return std::make_unique<MockMcu>(std::move(f)); }
std::unique_ptr<IDevice> makeMockFpga(std::shared_ptr<FaultInjector> f) { return std::make_unique<MockFpga>(std::move(f)); }
std::unique_ptr<IDevice> makeMockJtagAdapter(std::shared_ptr<FaultInjector> f) {
    return std::make_unique<MockJtagAdapter>(std::move(f));
}

DeviceManager::DeviceManager() : faults_(std::make_shared<FaultInjector>()) {
    devices_.push_back(makeMockMcu(faults_));
    devices_.push_back(makeMockFpga(faults_));
    devices_.push_back(makeMockJtagAdapter(faults_));
}

IDevice& DeviceManager::find(const std::string& id) const {
    for (const auto& d : devices_)
        if (d->discover().id == id) return *d;
    throw DeviceError("not_found", "Unknown device id '" + id + "'. Use one of: mcu0, fpga0, jtag0.", false);
}

std::vector<DeviceInfo> DeviceManager::discoverAll() const {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<DeviceInfo> out;
    for (const auto& d : devices_) out.push_back(d->discover());
    return out;
}

ConnectionState DeviceManager::stateOf(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mu_);
    return find(id).state();
}

json DeviceManager::capabilitiesOf(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mu_);
    return find(id).capabilities();
}

void DeviceManager::connect(const std::string& id) {
    std::lock_guard<std::mutex> lock(mu_);
    find(id).connect();
}

void DeviceManager::disconnect(const std::string& id) {
    std::lock_guard<std::mutex> lock(mu_);
    find(id).disconnect();
}

json DeviceManager::readTelemetry(const std::string& id) {
    std::lock_guard<std::mutex> lock(mu_);
    return find(id).readTelemetry();
}

void DeviceManager::simulateTimeout(bool on, int timeouts) {
    if (on) faults_->arm(timeouts);
    else faults_->clear();
}

ConnectOutcome DeviceManager::connectWithRetry(const std::string& id, const RetryPolicy& policy, const SleepFn& sleep) {
    ConnectOutcome out;
    int backoff = policy.initial_backoff_ms;
    for (int n = 1; n <= policy.max_attempts; ++n) {
        ConnectAttempt a;
        a.number = n;
        try {
            connect(id);
            a.ok = true;
            a.message = "connected";
            out.connected = true;
        } catch (const DeviceError& e) {
            if (!e.recoverable()) throw;  // wrong call order etc. cannot be fixed by retrying
            a.message = e.what();
        }
        const bool giveUp = a.ok || n == policy.max_attempts;
        a.backoff_ms_after = giveUp ? 0 : backoff;
        out.attempts.push_back(a);
        if (giveUp) break;
        if (sleep) sleep(backoff);
        else std::this_thread::sleep_for(std::chrono::milliseconds(backoff));
        backoff *= policy.backoff_factor;
    }
    return out;
}

}  // namespace wb
