#include "BMI088.hpp"
#include "LED.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {
struct FakeClock {
    uint32_t now{};
    uint32_t extra_sleep{};
    std::vector<uint32_t> sleeps;
    static uint32_t Now(void *context) { return static_cast<FakeClock *>(context)->now; }
    static void Sleep(void *context, uint32_t ms) {
        auto &clock = *static_cast<FakeClock *>(context);
        assert(ms > 0);
        clock.sleeps.push_back(ms);
        clock.now += ms + clock.extra_sleep;
    }
    bsp::Clock port() { return {this, Now, Sleep}; }
};

struct FakeBus;
struct SelectLine {
    FakeBus *bus{};
    bool high{true};
    size_t assertions{};
    size_t releases{};
    size_t transactions_this_select{};
    static void Write(void *context, bool high);
};

struct Event {
    bool accel;
    uint8_t address;
    bool read;
    size_t size;
    uint8_t value;
    uint32_t at_ms;
};

struct FakeChip {
    FakeBus *bus;
    bool accel;
    SelectLine select;
    std::array<uint8_t, 128> regs{};
    bool spi_mode;
    size_t ignored_reads{};
    size_t resets{};
    uint32_t reset_at{};
    uint32_t power_conf_at{};
    uint32_t power_ctrl_at{};
    uint8_t id;
    uint8_t reset_id;
    int bad_readback{-1};
    uint8_t bad_mask{1};

    FakeChip(FakeBus &owner, bool is_accel)
        : bus(&owner), accel(is_accel), select{&owner}, spi_mode(!is_accel),
          id(is_accel ? 0x1E : 0x0F), reset_id(id) { Defaults(); }
    void Defaults() {
        regs.fill(0);
        regs[0] = id;
        if (accel) { regs[0x40] = 0xA8; regs[0x7C] = 3; }
        else regs[0x10] = 0x80; // RO high bit must not spoil bandwidth verification.
    }
    static bsp::Status Exchange(void *context, const uint8_t *tx, uint8_t *rx,
                                size_t size, uint32_t timeout_ms);
    bsp::SpiDevice port() { return {this, Exchange, {&select, SelectLine::Write}}; }
};

struct FakeBus {
    FakeClock clock;
    FakeChip accel{*this, true};
    FakeChip gyro{*this, false};
    std::vector<Event> events;
    size_t fail_at{}; // One-based transfer, zero disables injection.
    bsp::Status fault{bsp::Status::io_error};
    uint32_t exchange_ms{};
    std::vector<uint32_t> timeouts;
};

void SelectLine::Write(void *context, bool high) {
    auto &line = *static_cast<SelectLine *>(context);
    if (!high) {
        // The two chips share a bus: neither CS may already be active.
        assert(line.high && line.bus->accel.select.high && line.bus->gyro.select.high);
        line.transactions_this_select = 0;
        ++line.assertions;
    } else if (!line.high) {
        assert(line.transactions_this_select == 1);
        ++line.releases;
    }
    line.high = high;
}

bsp::Status FakeChip::Exchange(void *context, const uint8_t *tx, uint8_t *rx,
                               size_t size, uint32_t timeout_ms) {
    auto &chip = *static_cast<FakeChip *>(context);
    auto &bus = *chip.bus;
    assert(!chip.select.high);
    assert((chip.accel ? bus.gyro : bus.accel).select.high);
    assert(++chip.select.transactions_this_select == 1);
    assert(tx && rx && tx != rx && size >= 2 && size <= 8);
    assert(timeout_ms > 0 && timeout_ms <= 10);
    bus.timeouts.push_back(timeout_ms);
    const bool read = (tx[0] & 0x80) != 0;
    const uint8_t address = tx[0] & 0x7F;
    if (read) {
        const size_t prefix = chip.accel ? 2 : 1;
        const size_t payload = ((chip.accel && address == 0x12) ||
                                (!chip.accel && address == 0x02)) ? 6 :
                               (chip.accel && address == 0x22) ? 2 : 1;
        assert(size == prefix + payload); // Detect missing accel dummy or extra gyro dummy.
        for (size_t i = 1; i < size; ++i) assert(tx[i] == 0);
    } else assert(size == 2); // Writes never have the accel read dummy.
    bus.events.push_back({chip.accel, address, read, size, tx[1], bus.clock.now});
    for (size_t i = 0; i < size; ++i) rx[i] = 0xD3; // Poison address/dummy bytes.
    bus.clock.now += bus.exchange_ms;
    if (bus.fail_at == bus.events.size()) return bus.fault;
    if (chip.resets)
        assert(static_cast<uint32_t>(bus.clock.now - chip.reset_at) >= (chip.accel ? 50U : 80U));

    if (!chip.spi_mode) {
        assert(chip.accel && read && address == 0);
        chip.spi_mode = true;
        ++chip.ignored_reads;
        // Interface-selection read is intentionally not the chip ID.
        for (size_t i = 0; i < size; ++i) rx[i] = 0;
        return bsp::Status::ok;
    }
    if (read) {
        const size_t prefix = chip.accel ? 2 : 1;
        for (size_t i = prefix; i < size; ++i) {
            const size_t reg = address + i - prefix;
            rx[i] = chip.regs[reg];
            if (static_cast<int>(reg) == chip.bad_readback) rx[i] ^= chip.bad_mask;
        }
    } else if (address == (chip.accel ? 0x7E : 0x14)) {
        assert(tx[1] == 0xB6);
        chip.id = chip.reset_id;
        chip.Defaults();
        ++chip.resets;
        chip.reset_at = bus.clock.now;
        chip.spi_mode = !chip.accel;
    } else {
        if (chip.accel && address == 0x7C) {
            assert(tx[1] == 0);
            chip.power_conf_at = bus.clock.now;
        }
        if (chip.accel && address == 0x7D) {
            assert(tx[1] == 4 && chip.regs[0x7C] == 0);
            assert(static_cast<uint32_t>(bus.clock.now - chip.power_conf_at) >= 5);
            chip.power_ctrl_at = bus.clock.now;
        }
        if (chip.accel && (address == 0x40 || address == 0x41)) {
            assert(chip.regs[0x7D] == 4);
            assert(static_cast<uint32_t>(bus.clock.now - chip.power_ctrl_at) >= 50);
        }
        chip.regs[address] = tx[1];
        if (!chip.accel && address == 0x10) chip.regs[address] |= 0x80;
    }
    return bsp::Status::ok;
}

bool Near(float a, float b) { return std::fabs(a - b) < 0.00001f; }

void AssertDeselected(const FakeBus &bus) {
    assert(bus.accel.select.high && bus.gyro.select.high);
    assert(bus.accel.select.assertions == bus.accel.select.releases);
    assert(bus.gyro.select.assertions == bus.gyro.select.releases);
}

void AssertInvalid(const imu_snapshot_t &out, imu_status_t error, uint32_t sequence) {
    assert(!out.valid && out.error == error && out.sequence == sequence);
    assert(out.timestamp_ms == 0 && out.temperature_c == 0);
    assert(out.accel.x == 0 && out.accel.y == 0 && out.accel.z == 0);
    assert(out.gyro.x == 0 && out.gyro.y == 0 && out.gyro.z == 0);
}

imu_snapshot_t DirtySnapshot() {
    return {{1, 2, 3}, {4, 5, 6}, 7, true, 8, 9, imu_status_t::ok};
}

void Put16(FakeChip &chip, uint8_t address, int32_t value) {
    const auto bits = static_cast<uint16_t>(value);
    chip.regs[address] = static_cast<uint8_t>(bits);
    chip.regs[address + 1] = static_cast<uint8_t>(bits >> 8);
}

void PutTemperature(FakeChip &chip, int32_t value) {
    const auto bits = static_cast<uint16_t>(value) & 0x07FF;
    chip.regs[0x22] = static_cast<uint8_t>(bits >> 3);
    chip.regs[0x23] = static_cast<uint8_t>((bits << 5) | 0x1F); // Low five bits ignored.
}

void TestConfiguration() {
    FakeBus bus;
    BMI088::cBMI088 imu(bus.accel.port(), bus.gyro.port(), bus.clock.port());
    assert(bus.events.empty() && bus.clock.sleeps.empty());
    assert(!imu.initialized() && imu.status() == imu_status_t::not_initialized);
    auto out = DirtySnapshot();
    assert(imu.Update(out) == imu_status_t::not_initialized);
    AssertInvalid(out, imu_status_t::not_initialized, 0);
    assert(bus.events.empty());
    assert(imu.Config() == imu_status_t::ok && imu.initialized());
    assert(imu.status() == imu_status_t::ok);
    assert(bus.accel.ignored_reads == 2 && bus.gyro.ignored_reads == 0);
    assert(bus.accel.resets == 1 && bus.gyro.resets == 1);
    const Event expected[]{
        {true, 0x00, true, 3, 0, 0}, {true, 0x00, true, 3, 0, 0},
        {false, 0x00, true, 2, 0, 0}, {true, 0x7E, false, 2, 0xB6, 0},
        {true, 0x00, true, 3, 0, 0}, {true, 0x00, true, 3, 0, 0},
        {false, 0x14, false, 2, 0xB6, 0}, {false, 0x00, true, 2, 0, 0},
        {true, 0x7C, false, 2, 0x00, 0}, {true, 0x7C, true, 3, 0, 0},
        {true, 0x7D, false, 2, 0x04, 0}, {true, 0x7D, true, 3, 0, 0},
        {true, 0x41, false, 2, 0x01, 0}, {true, 0x41, true, 3, 0, 0},
        {true, 0x40, false, 2, 0xAA, 0}, {true, 0x40, true, 3, 0, 0},
        {false, 0x11, false, 2, 0x00, 0}, {false, 0x11, true, 2, 0, 0},
        {false, 0x0F, false, 2, 0x00, 0}, {false, 0x0F, true, 2, 0, 0},
        {false, 0x10, false, 2, 0x03, 0}, {false, 0x10, true, 2, 0, 0}
    };
    assert(bus.events.size() == sizeof(expected) / sizeof(expected[0]));
    for (size_t i = 0; i < bus.events.size(); ++i) {
        const auto &actual = bus.events[i];
        const auto &want = expected[i];
        assert(actual.accel == want.accel && actual.address == want.address);
        assert(actual.read == want.read && actual.size == want.size && actual.value == want.value);
    }
    assert(bus.events.front().at_ms >= 80);
    assert(bus.accel.regs[0x40] == 0xAA && bus.accel.regs[0x41] == 1);
    assert(bus.accel.regs[0x7C] == 0 && bus.accel.regs[0x7D] == 4);
    assert(bus.gyro.regs[0x0F] == 0 && bus.gyro.regs[0x10] == 0x83);
    assert(bus.gyro.regs[0x11] == 0 && bus.clock.now < 1000);
    AssertDeselected(bus);
}

void TestSignedSamples() {
    FakeBus bus;
    BMI088::cBMI088 imu(bus.accel.port(), bus.gyro.port(), bus.clock.port());
    assert(imu.Config() == imu_status_t::ok);
    Put16(bus.accel, 0x12, -32768);
    Put16(bus.accel, 0x14, 32767);
    Put16(bus.accel, 0x16, -1);
    Put16(bus.gyro, 0x02, 32767);
    Put16(bus.gyro, 0x04, -32768);
    Put16(bus.gyro, 0x06, -12345);
    PutTemperature(bus.accel, -200);
    bus.clock.now = 1234;
    bus.exchange_ms = 1;
    imu_snapshot_t out;
    assert(imu.Update(out) == imu_status_t::ok);
    assert(out.valid && out.error == imu_status_t::ok && out.sequence == 1);
    assert(out.timestamp_ms == 1237);
    const float acc_scale = 6.0f * 9.80665f / 32768.0f;
    const float gyro_scale = 2000.0f * 3.14159265358979323846f / 180.0f / 32768.0f;
    assert(Near(out.accel.x, -32768 * acc_scale));
    assert(Near(out.accel.y, 32767 * acc_scale));
    assert(Near(out.accel.z, -acc_scale));
    assert(Near(out.gyro.x, 32767 * gyro_scale));
    assert(Near(out.gyro.y, -32768 * gyro_scale));
    assert(Near(out.gyro.z, -12345 * gyro_scale));
    assert(Near(out.temperature_c, -2));
    uint32_t sequence = 1;
    for (int32_t raw : {-1024, -1, 0, 1, 1023}) {
        PutTemperature(bus.accel, raw);
        assert(imu.Update(out) == imu_status_t::ok);
        assert(out.sequence == ++sequence);
        assert(Near(out.temperature_c, 23.0f + raw * 0.125f));
    }
    for (uint8_t address = 0x02; address <= 0x06; address += 2) Put16(bus.gyro, address, 0);
    assert(imu.Update(out) == imu_status_t::ok);
    assert(out.gyro.x == 0 && out.gyro.y == 0 && out.gyro.z == 0); // No invented bias.
    AssertDeselected(bus);
}

void TestIdsAndReadbacks() {
    for (bool accel : {true, false}) {
        for (bool after_reset : {true, false}) {
            FakeBus bus;
            FakeChip &chip = accel ? bus.accel : bus.gyro;
            if (after_reset) chip.reset_id = 0xFF;
            else chip.regs[0] = 0xFF;
            BMI088::cBMI088 imu(bus.accel.port(), bus.gyro.port(), bus.clock.port());
            const auto error = accel ? imu_status_t::accel_id_error : imu_status_t::gyro_id_error;
            assert(imu.Config() == error && !imu.initialized() && imu.status() == error);
            auto out = DirtySnapshot();
            assert(imu.Update(out) == error);
            AssertInvalid(out, error, 0);
            AssertDeselected(bus);
        }
    }
    struct Mismatch { bool accel; uint8_t reg, mask; };
    for (const Mismatch mismatch : {Mismatch{true, 0x7C, 1}, {true, 0x7D, 4},
                                   {true, 0x41, 1}, {true, 0x40, 1}, {true, 0x40, 0x80},
                                   {false, 0x11, 0x80}, {false, 0x0F, 1}, {false, 0x10, 1}}) {
        FakeBus bus;
        FakeChip &chip = mismatch.accel ? bus.accel : bus.gyro;
        chip.bad_readback = mismatch.reg;
        chip.bad_mask = mismatch.mask;
        BMI088::cBMI088 imu(bus.accel.port(), bus.gyro.port(), bus.clock.port());
        assert(imu.Config() == imu_status_t::config_error);
        assert(!imu.initialized() && imu.status() == imu_status_t::config_error);
        AssertDeselected(bus);
    }
}

void TestTransportFaults() {
    struct Fault { bsp::Status port; imu_status_t expected; };
    const Fault faults[]{
        {bsp::Status::io_error, imu_status_t::transport_error},
        {bsp::Status::not_ready, imu_status_t::transport_error},
        {bsp::Status::timeout, imu_status_t::timeout},
        {bsp::Status::busy, imu_status_t::busy},
        {bsp::Status::invalid, imu_status_t::invalid_port}
    };
    // Every initialization exchange, including dummy accesses and writes, can fail.
    for (size_t transfer = 1; transfer <= 22; ++transfer) {
        for (const Fault &fault : faults) {
            FakeBus bus;
            bus.fail_at = transfer;
            bus.fault = fault.port;
            BMI088::cBMI088 imu(bus.accel.port(), bus.gyro.port(), bus.clock.port());
            assert(imu.Config() == fault.expected && !imu.initialized());
            assert(bus.events.size() == transfer); // Must stop immediately, no retries.
            assert(imu.status() == fault.expected);
            AssertDeselected(bus);
        }
    }
    for (size_t transfer = 1; transfer <= 3; ++transfer) {
        for (const Fault &fault : faults) {
            FakeBus bus;
            BMI088::cBMI088 imu(bus.accel.port(), bus.gyro.port(), bus.clock.port());
            assert(imu.Config() == imu_status_t::ok);
            Put16(bus.accel, 0x12, 123);
            imu_snapshot_t out;
            assert(imu.Update(out) == imu_status_t::ok && out.sequence == 1);
            bus.fail_at = bus.events.size() + transfer;
            bus.fault = fault.port;
            assert(imu.Update(out) == fault.expected);
            AssertInvalid(out, fault.expected, 1);
            assert(!imu.initialized() && imu.status() == fault.expected);
            const size_t count = bus.events.size();
            out = DirtySnapshot();
            assert(imu.Update(out) == fault.expected);
            AssertInvalid(out, fault.expected, 1);
            assert(bus.events.size() == count); // Explicit reconfiguration required.
            AssertDeselected(bus);
            bus.fail_at = 0;
            assert(imu.Config() == imu_status_t::ok);
            assert(imu.Update(out) == imu_status_t::ok && out.sequence == 2);
        }
    }
}

void TestPortsAndDeadlines() {
    // Six required functions; context may legitimately be null for stateless ports.
    for (int missing = 0; missing < 6; ++missing) {
        FakeBus bus;
        auto accel = bus.accel.port();
        auto gyro = bus.gyro.port();
        auto clock = bus.clock.port();
        switch (missing) {
        case 0: accel.exchange = nullptr; break;
        case 1: accel.cs.write = nullptr; break;
        case 2: gyro.exchange = nullptr; break;
        case 3: gyro.cs.write = nullptr; break;
        case 4: clock.now_ms = nullptr; break;
        case 5: clock.sleep_ms = nullptr; break;
        }
        BMI088::cBMI088 imu(accel, gyro, clock);
        assert(imu.Config() == imu_status_t::invalid_port && !imu.initialized());
        assert(bus.events.empty() && bus.clock.sleeps.empty());
        auto out = DirtySnapshot();
        assert(imu.Update(out) == imu_status_t::invalid_port);
        AssertInvalid(out, imu_status_t::invalid_port, 0);
    }
    {
        FakeBus bus;
        bus.clock.extra_sleep = 1000;
        BMI088::cBMI088 imu(bus.accel.port(), bus.gyro.port(), bus.clock.port());
        assert(imu.Config() == imu_status_t::timeout);
        assert(bus.events.empty() && bus.clock.sleeps.size() == 1);
        AssertDeselected(bus);
    }
    {
        FakeBus bus;
        bus.clock.now = UINT32_MAX - 30;
        BMI088::cBMI088 imu(bus.accel.port(), bus.gyro.port(), bus.clock.port());
        assert(imu.Config() == imu_status_t::ok); // Configuration deadline wraps safely.
        bus.clock.now = UINT32_MAX - 1;
        bus.exchange_ms = 1;
        imu_snapshot_t out;
        assert(imu.Update(out) == imu_status_t::ok && out.timestamp_ms == 1);
        bus.exchange_ms = 10;
        assert(imu.Update(out) == imu_status_t::timeout); // Three exchanges exhaust 30ms.
        AssertInvalid(out, imu_status_t::timeout, 1);
        AssertDeselected(bus);
    }
    {
        FakeBus bus;
        BMI088::cBMI088 imu(bus.accel.port(), bus.gyro.port(), bus.clock.port());
        assert(imu.Config() == imu_status_t::ok);
        bus.exchange_ms = 14; // Misbehaving callback: driver still bounds future work.
        auto out = DirtySnapshot();
        assert(imu.Update(out) == imu_status_t::timeout);
        assert(bus.timeouts.back() == 2); // Last transfer gets only remaining budget.
        AssertInvalid(out, imu_status_t::timeout, 0);
        AssertDeselected(bus);
    }
}

struct FakeOutput {
    bool high{};
    size_t writes{};
    static void Write(void *context, bool high) {
        auto &output = *static_cast<FakeOutput *>(context);
        output.high = high;
        ++output.writes;
    }
    bsp::DigitalOut port() { return {this, Write}; }
};

void TestLed() {
    FakeOutput red, green, blue;
    LED::cLED led(red.port(), green.port(), blue.port());
    assert(!led.initialized() && !led.Set(true, true, true));
    assert(red.writes == 0 && green.writes == 0 && blue.writes == 0);
    assert(led.Init() && led.initialized());
    assert(red.high && green.high && blue.high);
    for (unsigned mask = 0; mask < 8; ++mask) {
        assert(led.Set(mask & 1, mask & 2, mask & 4));
        assert(red.high == !(mask & 1));
        assert(green.high == !(mask & 2));
        assert(blue.high == !(mask & 4));
    }
    assert(led.Init() && red.high && green.high && blue.high);
    for (int missing = 0; missing < 3; ++missing) {
        const size_t writes = red.writes + green.writes + blue.writes;
        auto r = red.port(), g = green.port(), b = blue.port();
        if (missing == 0) r.write = nullptr;
        if (missing == 1) g.write = nullptr;
        if (missing == 2) b.write = nullptr;
        LED::cLED invalid(r, g, b);
        assert(!invalid.Init() && !invalid.Set(true, true, true));
        assert(!invalid.initialized());
        assert(red.writes + green.writes + blue.writes == writes);
    }
}
} // namespace

int main() {
    TestConfiguration();
    TestSignedSamples();
    TestIdsAndReadbacks();
    TestTransportFaults();
    TestPortsAndDeadlines();
    TestLed();
    std::puts("BMI088 fake-chip and LED tests passed (no hardware validation).");
}
