#include <vector>

#include "devices.h"
#include "doctest.h"

namespace {
// Run a callable and return the DeviceError code it throws ("" if none).
template <typename Fn>
std::string errorCode(Fn&& fn) {
    try {
        fn();
    } catch (const wb::DeviceError& e) {
        return e.code();
    }
    return "";
}
}  // namespace

TEST_CASE("discovery lists one simulated device of each kind") {
    wb::DeviceManager m;
    const auto devs = m.discoverAll();
    REQUIRE(devs.size() == 3);
    CHECK(devs[0].kind == wb::DeviceKind::Mcu);
    CHECK(devs[1].kind == wb::DeviceKind::Fpga);
    CHECK(devs[2].kind == wb::DeviceKind::JtagAdapter);
    for (const auto& d : devs) {
        CHECK(d.simulated);
        CHECK(d.display_name.rfind("[SIMULATED]", 0) == 0);
        CHECK(m.capabilitiesOf(d.id)["simulated"] == true);
        CHECK(m.capabilitiesOf(d.id)["label"] == "SIMULATED");
        CHECK(m.stateOf(d.id) == wb::ConnectionState::Disconnected);
    }
}

TEST_CASE("capabilities do not pretend the JTAG adapter is a compute board") {
    wb::DeviceManager m;
    const auto jtag = m.capabilitiesOf("jtag0");
    CHECK(jtag["compute"] == false);
    CHECK(jtag["runs_user_application"] == false);
    // A bare FT2232H has 3.3 V I/O cells, so VTref tracking must come from separate buffers.
    CHECK(jtag["level_translation"].get<std::string>().find("VTref") != std::string::npos);
    CHECK(jtag["signals"]["VTref"].get<std::string>().find("target") != std::string::npos);
    CHECK(m.capabilitiesOf("mcu0")["io_voltage_V"] == doctest::Approx(3.3));
    CHECK(m.capabilitiesOf("fpga0")["io_voltage_V"] == doctest::Approx(3.3));
}

TEST_CASE("state machine: connect, telemetry, disconnect, and wrong-order calls") {
    wb::DeviceManager m;
    for (const char* id : {"mcu0", "fpga0", "jtag0"}) {
        CHECK(errorCode([&] { m.readTelemetry(id); }) == "invalid_state");  // not connected yet
        CHECK(errorCode([&] { m.disconnect(id); }) == "invalid_state");
        m.connect(id);
        CHECK(m.stateOf(id) == wb::ConnectionState::Connected);
        CHECK(errorCode([&] { m.connect(id); }) == "invalid_state");  // already connected

        const auto t0 = m.readTelemetry(id), t1 = m.readTelemetry(id);
        CHECK(t0["simulated"] == true);
        CHECK(t0["label"] == "SIMULATED");
        CHECK(t0["device_id"] == id);
        CHECK(t0["seq"] == 0);
        CHECK(t1["seq"] == 1);
        CHECK(t0["values"].is_object());

        m.disconnect(id);
        CHECK(m.stateOf(id) == wb::ConnectionState::Disconnected);
    }
}

TEST_CASE("telemetry values are plausible for each device type") {
    wb::DeviceManager m;
    m.connect("mcu0");
    m.connect("fpga0");
    m.connect("jtag0");
    CHECK(m.readTelemetry("mcu0")["values"]["supply_V"].get<double>() == doctest::Approx(3.3).epsilon(0.01));
    CHECK(m.readTelemetry("fpga0")["values"]["vccint_V"].get<double>() == doctest::Approx(1.0).epsilon(0.01));
    CHECK(m.readTelemetry("jtag0")["values"]["vtref_V"].get<double>() == doctest::Approx(3.3).epsilon(0.01));
}

TEST_CASE("unknown device id is reported as not_found") {
    wb::DeviceManager m;
    CHECK(errorCode([&] { m.connect("nope"); }) == "not_found");
    CHECK(errorCode([&] { m.capabilitiesOf("nope"); }) == "not_found");
}

TEST_CASE("simulated timeout is recoverable: device goes Failed and a later connect succeeds") {
    wb::DeviceManager m;
    m.simulateTimeout(true, 1);
    CHECK(m.pendingTimeouts() == 1);
    try {
        m.connect("mcu0");
        FAIL("expected a timeout");
    } catch (const wb::DeviceError& e) {
        CHECK(e.code() == "timeout");
        CHECK(e.recoverable());
        CHECK(std::string(e.what()).find("SIMULATED") != std::string::npos);
    }
    CHECK(m.stateOf("mcu0") == wb::ConnectionState::Failed);
    CHECK(m.pendingTimeouts() == 0);
    CHECK(errorCode([&] { m.readTelemetry("mcu0"); }) == "invalid_state");
    m.connect("mcu0");  // fault has cleared
    CHECK(m.stateOf("mcu0") == wb::ConnectionState::Connected);
}

TEST_CASE("retry with exponential backoff recovers after two timeouts") {
    wb::DeviceManager m;
    m.simulateTimeout(true, 2);
    std::vector<int> sleeps;
    const wb::ConnectOutcome out = m.connectWithRetry("fpga0", {}, [&](int ms) { sleeps.push_back(ms); });
    CHECK(out.connected);
    REQUIRE(out.attempts.size() == 3);
    CHECK_FALSE(out.attempts[0].ok);
    CHECK_FALSE(out.attempts[1].ok);
    CHECK(out.attempts[2].ok);
    CHECK(sleeps == std::vector<int>{100, 200});
    CHECK(out.attempts[0].backoff_ms_after == 100);
    CHECK(out.attempts[2].backoff_ms_after == 0);
    CHECK(m.stateOf("fpga0") == wb::ConnectionState::Connected);
}

TEST_CASE("retry gives up after max_attempts but the device stays recoverable") {
    wb::DeviceManager m;
    m.simulateTimeout(true, 5);
    wb::RetryPolicy policy;
    policy.max_attempts = 2;
    const wb::ConnectOutcome out = m.connectWithRetry("jtag0", policy, [](int) {});
    CHECK_FALSE(out.connected);
    CHECK(out.attempts.size() == 2);
    CHECK(m.stateOf("jtag0") == wb::ConnectionState::Failed);
    m.simulateTimeout(false);  // operator clears the fault
    CHECK(m.connectWithRetry("jtag0", policy, [](int) {}).connected);
    CHECK(m.stateOf("jtag0") == wb::ConnectionState::Connected);
}

TEST_CASE("disconnect resets a failed connection; non-recoverable errors are not retried") {
    wb::DeviceManager m;
    m.simulateTimeout(true, 1);
    CHECK(errorCode([&] { m.connect("mcu0"); }) == "timeout");
    m.disconnect("mcu0");
    CHECK(m.stateOf("mcu0") == wb::ConnectionState::Disconnected);
    m.connect("mcu0");
    int sleeps = 0;
    CHECK(errorCode([&] { m.connectWithRetry("mcu0", {}, [&](int) { ++sleeps; }); }) == "invalid_state");
    CHECK(sleeps == 0);
}
