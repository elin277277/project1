// Host-only regression harness for the REAL service loops and motor modules.
// Sleep hooks model selected interleavings, not ThreadX scheduling, Cortex-M IRQ
// priorities/ABI, HAL peripherals, CAN delivery, or physical motor behavior.
#include "service_internal.hpp"
#include "app_config.hpp"
#include "bsp.hpp"
#include "bsp_can.hpp"
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace test {
struct StopLoop {};
void check(bool condition, const char *expression, int line) {
    if (!condition) throw std::runtime_error(std::string("line ") + std::to_string(line) + ": " + expression);
}
#define CHECK(expression) test::check(static_cast<bool>(expression), #expression, __LINE__)

uint32_t time_ms = 1000;
UINT interrupt_mask = TX_INT_ENABLE;
unsigned created_threads = 0;
std::function<void(ULONG)> sleep_hook;
std::function<void()> next_get_preemption;
std::function<void()> deferred_preemption;
bool red_high = true, green_high = true, blue_high = true;
struct Bus {
    bsp::CanReceive receive{};
    void *receiver{};
    uint32_t errors{};
    unsigned starts{};
    unsigned aborts{}; // Calls only: this fake does not model physical CAN mailbox state.
    std::vector<bsp::CanFrame> frames;
    std::function<void()> after_send;
    bool fail_next_send{};
};
Bus buses[2];

void feedback(bool valid = true) {
    auto &bus = buses[0];
    CHECK(bus.receive != nullptr);
    bsp::CanFrame frame{};
    frame.id = app_config::motor_feedback_id;
    frame.size = valid ? 8 : 7; // Addressed malformed frame increments the real module fault epoch.
    frame.data[0] = 0x04; // Encoder 1024, zero RPM/current; deterministic nonzero P output.
    bus.receive(bus.receiver, frame, time_ms);
}
void bad_then_good() { feedback(false); feedback(true); }
void request(services::ControlMode mode, float target = 0) {
    CHECK(services::RequestMotor(mode, target));
}
void speed() { request(services::ControlMode::speed, 5.0f); }
void relax() { request(services::ControlMode::relax); }

bsp::Status start(void *context, bsp::CanReceive receive, void *receiver) {
    auto &bus = *static_cast<Bus *>(context);
    CHECK(bus.starts++ == 0);
    bus.receive = receive;
    bus.receiver = receiver;
    feedback();
    return bsp::Status::ok;
}
bsp::Status send(void *context, const bsp::CanFrame &frame) {
    auto &bus = *static_cast<Bus *>(context);
    if (bus.fail_next_send) {
        bus.fail_next_send = false;
        return bsp::Status::io_error;
    }
    bus.frames.push_back(frame);
    if (bus.after_send) {
        auto hook = std::move(bus.after_send);
        bus.after_send = {};
        hook(); // Fault in/after transport acceptance: cannot recall this captured frame.
    }
    return bsp::Status::ok;
}
uint32_t clock_now(void *) { return time_ms; }
void clock_sleep(void *, uint32_t) { throw std::runtime_error("service used blocking BSP sleep"); }
void pin_write(void *context, bool high) { *static_cast<bool *>(context) = high; }

int current(const bsp::CanFrame &frame) {
    CHECK(frame.id == 0x200);
    CHECK(frame.size == 8 && !frame.extended && !frame.remote);
    for (unsigned i = 2; i != 8; ++i) CHECK(frame.data[i] == 0);
    const unsigned raw = (static_cast<unsigned>(frame.data[0]) << 8) | frame.data[1];
    return raw < 32768 ? static_cast<int>(raw) : static_cast<int>(raw) - 65536;
}

void initialization_and_mailbox() {
    CHECK(!services::RequestMotor(services::ControlMode::speed, 1));
    tx_application_define(nullptr);
    CHECK(services::detail::initialized.load());
    CHECK(created_threads == 3);
    CHECK(services::detail::imu_lock.inherit == TX_INHERIT);
    CHECK(services::detail::command_lock.inherit == TX_INHERIT);
    CHECK(services::detail::imu_beat.count == 0);
    CHECK(services::detail::control_beat.count == 0);
    CHECK(!services::RequestMotor(services::ControlMode::speed, std::numeric_limits<float>::quiet_NaN()));
    CHECK(!services::RequestMotor(services::ControlMode::speed, std::numeric_limits<float>::infinity()));
    CHECK(!services::RequestMotor(static_cast<services::ControlMode>(255), 0));
    services::detail::command_lock.locked = true;
    CHECK(!services::RequestMotor(services::ControlMode::relax, 0));
    services::detail::command_lock.locked = false;
    CHECK(services::detail::command.sequence == 0);
    CHECK(services::detail::ticks(1) == 1);
    CHECK(services::detail::ticks(1500) == 1500);
    sleep_hook = [](ULONG ticks) { CHECK(ticks == 1); };
    services::detail::sleep_ms(nullptr, 0);
    sleep_hook = {};
}

void control_regressions() {
    using services::ControlMode;
    using namespace services::detail;
    struct Step { std::string name; std::function<void()> prepare; bool drive; };
    std::vector<Step> steps;
    auto add = [&](const char *name, std::function<void()> action, bool drive) {
        steps.push_back({name, std::move(action), drive});
    };
    add("startup must not drive", {}, false);
    add("speed without initial relax cannot arm", speed, false);
    add("explicit initial relax", relax, false);
    add("relax then speed drives", speed, true);
    add("mutex contention retains cached fresh lease", [] { command_lock.locked = true; }, true);
    add("cached lease remains valid at 4ms", {}, true);
    add("cached lease remains valid at 6ms", {}, true);
    add("cached lease remains valid at 8ms", {}, true);
    add("mutex contention does not extend 10ms lease", {}, false);
    add("target refresh after expiry cannot autoarm", [] { command_lock.locked = false; speed(); }, false);
    add("explicit relax after expiry", relax, false);
    add("explicit speed after expiry rearm", speed, true);
    add("bad+good feedback before AliveCheck while driving", [] { bad_then_good(); speed(); }, false);
    add("normal refresh cannot undo feedback fault", speed, false);
    add("acknowledge observed driving fault", relax, false);
    add("drive after observed driving fault acknowledgement", speed, true);
    add("arm while remaining relaxed", relax, false);
    add("bad+good feedback while armed relaxed", [] { bad_then_good(); speed(); }, false);
    add("armed-relax fault requires explicit rearm", speed, false);
    add("acknowledge armed-relax fault", relax, false);
    add("drive after armed-relax acknowledgement", speed, true);
    add("fault arrives after nonzero frame transport acceptance", [] {
        speed(); buses[0].after_send = bad_then_good;
    }, true);
    add("post-send fault prevents next refresh rearm", speed, false);
    add("acknowledge post-send fault", relax, false);
    add("drive after post-send fault acknowledgement", speed, true);
    add("relax queued before fault is not acknowledgement", [] { relax(); bad_then_good(); }, false);
    add("pre-fault relax does not authorize target", speed, false);
    add("new relax acknowledges fault", relax, false);
    add("new speed after acknowledged fault", speed, true);
    add("pre-fault relax hidden behind busy command mutex", [] {
        relax();
        command_lock.locked = true; // Writer's completed payload cannot yet be copied by control.
        bad_then_good();
    }, false);
    add("hidden pre-fault relax remains revoked when mutex releases", [] {
        CHECK(command.epoch != command_epoch.load());
        command_lock.locked = false;
    }, false);
    add("delayed pre-fault relax cannot authorize refreshed speed", speed, false);
    add("new-epoch relax acknowledges fault after contention", relax, false);
    add("new-epoch speed drives after contention acknowledgement", speed, true);
    add("bus error latches disarm", [] { ++buses[0].errors; speed(); }, false);
    add("bus error normal refresh cannot autoarm", speed, false);
    add("acknowledge bus error", relax, false);
    add("drive after bus error acknowledgement", speed, true);
    add("send failure attempts zero and disarms", [] { speed(); buses[0].fail_next_send = true; }, false);
    add("send failure refresh cannot autoarm", speed, false);
    add("acknowledge send failure", relax, false);
    add("drive after send failure acknowledgement", speed, true);
    add("overspeed request relaxes", [] { request(ControlMode::speed, 21); }, false);
    add("valid speed cannot autoarm after overspeed", speed, false);
    add("explicit relax before position", relax, false);
    add("position mode uses real cascaded PID", [] { request(ControlMode::position, 5); }, true);
    add("final explicit stop", relax, false);

    size_t step = 0;
    size_t previous_frames = 0;
    sleep_hook = [&](ULONG duration) {
        CHECK(duration == app_config::control_period_ms);
        const auto &expect = steps.at(step);
        try {
            CHECK(buses[0].frames.size() > previous_frames);
            const int output = current(buses[0].frames.back());
            CHECK(expect.drive ? output > 0 : output == 0);
            CHECK(output <= app_config::motor_current_limit);
            CHECK(buses[1].frames.empty());
            CHECK(control_beat.count == 1); // Many control iterations must not queue old heartbeats.
        } catch (const std::exception &error) {
            throw std::runtime_error(expect.name + ": " + error.what());
        }
        previous_frames = buses[0].frames.size();
        if (++step == steps.size()) throw StopLoop{};
        time_ms += duration;
        feedback(); // Keep feedback online so lease/fault tests cannot pass just through disconnection.
        if (steps[step].prepare) steps[step].prepare();
    };
    try { control_entry(0); CHECK(false); }
    catch (const StopLoop &) {}
    sleep_hook = {};
    CHECK(step == steps.size());
    CHECK(buses[0].starts == 1 && buses[1].starts == 0);
    CHECK(!command_lock.locked);
    std::cout << "control: " << steps.size() << " scripted safety steps passed\n";
}

void heartbeat_regressions() {
    using namespace services::detail;
    // Drain the control run's one token; reset only published monitoring state.
    (void)consume_beat(control_beat, control_pending);
    (void)consume_beat(imu_beat, imu_pending);
    CHECK(interrupt_mask == TX_INT_ENABLE);
    for (unsigned i = 0; i != 100; ++i) {
        ++time_ms;
        beat(imu_beat, imu_pending);
        beat(control_beat, control_pending);
    }
    CHECK(imu_beat.count == 1 && control_beat.count == 1);
    CHECK(imu_last_beat.load() == time_ms && control_last_beat.load() == time_ms);
    CHECK(consume_beat(imu_beat, imu_pending));
    CHECK(!consume_beat(imu_beat, imu_pending));
    CHECK(consume_beat(control_beat, control_pending));
    CHECK(!consume_beat(control_beat, control_pending));

    // Attempt producer preemption immediately after an empty semaphore get.
    // The real critical section must defer it until after pending=false.
    next_get_preemption = [&] { beat(control_beat, control_pending); };
    CHECK(!consume_beat(control_beat, control_pending));
    CHECK(!next_get_preemption && !deferred_preemption);
    CHECK(control_beat.count == 1 && control_pending.load());
    beat(control_beat, control_pending);
    CHECK(control_beat.count == 1);
    CHECK(consume_beat(control_beat, control_pending));
    CHECK(!consume_beat(control_beat, control_pending));

    const UINT saved = tx_interrupt_control(TX_INT_DISABLE);
    beat(imu_beat, imu_pending);
    CHECK(interrupt_mask == TX_INT_DISABLE);
    CHECK(consume_beat(imu_beat, imu_pending));
    CHECK(interrupt_mask == TX_INT_DISABLE);
    tx_interrupt_control(saved);
    CHECK(interrupt_mask == TX_INT_ENABLE);
    std::cout << "heartbeat: bounded tokens, injected consume race, mask restoration passed\n";
}

void monitor_and_snapshot_regressions() {
    using namespace services::detail;
    unsigned sleeps = 0;
    sleep_hook = [&](ULONG duration) {
        services::Health health{};
        if (sleeps++ == 0) {
            CHECK(duration == 1500);
            time_ms += duration;
            imu_healthy = true; control_healthy = true;
            beat(imu_beat, imu_pending); beat(control_beat, control_pending);
            return;
        }
        CHECK(duration == app_config::monitor_period_ms);
        CHECK(services::HealthSnapshot(health));
        if (sleeps == 2) {
            CHECK(health.initialized && health.imu_alive && health.control_alive);
            CHECK(health.imu_healthy && health.control_healthy);
            CHECK(red_high && !green_high && !blue_high);
            time_ms += duration; // No new producer beats: next real monitor iteration must report dead.
            return;
        }
        CHECK(!health.imu_alive && !health.control_alive);
        CHECK(!health.imu_healthy && !health.control_healthy);
        CHECK(!red_high && green_high && blue_high);
        throw StopLoop{};
    };
    try { alive_entry(0); CHECK(false); }
    catch (const StopLoop &) {}
    sleep_hook = {};
    CHECK(sleeps == 3);

    services::Health health{};
    imu_alive = true; control_alive = true;
    imu_healthy = true; control_healthy = true;
    monitor_last_beat = time_ms;
    imu_last_beat = time_ms; control_last_beat = time_ms;
    CHECK(services::HealthSnapshot(health));
    time_ms += 2 * app_config::monitor_period_ms - 1;
    CHECK(services::HealthSnapshot(health));
    CHECK(health.imu_alive && health.control_alive);
    ++time_ms;
    CHECK(!services::HealthSnapshot(health)); // All stored flags true, monitor itself stale.
    CHECK(!health.imu_alive && !health.control_alive && !health.imu_healthy && !health.control_healthy);
    monitor_last_beat = time_ms; // Fresh monitor does not make stale producer timestamps healthy.
    CHECK(services::HealthSnapshot(health));
    CHECK(!health.imu_alive && !health.control_alive && !health.imu_healthy && !health.control_healthy);
    beat(imu_beat, imu_pending);
    CHECK(services::HealthSnapshot(health));
    CHECK(health.imu_alive && health.imu_healthy && !health.control_alive && !health.control_healthy);

    // Timestamp subtraction must remain correct across uint32_t clock wrap.
    time_ms = 10;
    monitor_last_beat = UINT32_MAX - 5;
    imu_last_beat = UINT32_MAX - 5; control_last_beat = UINT32_MAX - 5;
    CHECK(services::HealthSnapshot(health));
    CHECK(health.imu_alive && health.control_alive && health.imu_healthy && health.control_healthy);

    imu_sample = {};
    imu_sample.valid = true;
    imu_sample.timestamp_ms = time_ms;
    imu_sample.sequence = 42;
    imu_sample.error = imu_status_t::ok;
    imu_snapshot_t sample{};
    CHECK(services::ImuSnapshot(sample) && sample.valid && sample.sequence == 42);
    time_ms += 50;
    CHECK(services::ImuSnapshot(sample));
    ++time_ms;
    CHECK(!services::ImuSnapshot(sample) && !sample.valid);
    imu_lock.locked = true;
    sample.valid = true;
    CHECK(!services::ImuSnapshot(sample) && !sample.valid);
    imu_lock.locked = false;
    std::cout << "monitor/snapshots: real monitor loop, health expiry, wrap, sample expiry passed\n";
}
} // namespace test

// Fake ThreadX: creation only records static objects; no real threads are started.
UINT tx_mutex_create(TX_MUTEX *mutex, CHAR *, UINT inherit) {
    CHECK(!mutex->created);
    *mutex = {true, false, inherit};
    return TX_SUCCESS;
}
UINT tx_mutex_get(TX_MUTEX *mutex, ULONG) {
    CHECK(mutex->created);
    if (mutex->locked) return TX_NOT_AVAILABLE;
    mutex->locked = true;
    return TX_SUCCESS;
}
UINT tx_mutex_put(TX_MUTEX *mutex) {
    CHECK(mutex->created && mutex->locked);
    mutex->locked = false;
    return TX_SUCCESS;
}
UINT tx_semaphore_create(TX_SEMAPHORE *semaphore, CHAR *, ULONG count) {
    CHECK(!semaphore->created);
    *semaphore = {true, count};
    return TX_SUCCESS;
}
UINT tx_semaphore_get(TX_SEMAPHORE *semaphore, ULONG wait) {
    CHECK(semaphore->created && wait == TX_NO_WAIT);
    const UINT status = semaphore->count ? (--semaphore->count, TX_SUCCESS) : TX_NO_INSTANCE;
    if (test::next_get_preemption) {
        auto hook = std::move(test::next_get_preemption);
        test::next_get_preemption = {};
        if (test::interrupt_mask == TX_INT_DISABLE) {
            CHECK(!test::deferred_preemption);
            test::deferred_preemption = std::move(hook);
        } else hook();
    }
    return status;
}
UINT tx_semaphore_put(TX_SEMAPHORE *semaphore) {
    CHECK(semaphore->created);
    ++semaphore->count;
    return TX_SUCCESS;
}
UINT tx_interrupt_control(UINT mask) {
    const UINT saved = test::interrupt_mask;
    test::interrupt_mask = mask;
    if (mask == TX_INT_ENABLE && test::deferred_preemption) {
        auto hook = std::move(test::deferred_preemption);
        test::deferred_preemption = {};
        hook();
    }
    return saved;
}
UINT tx_thread_create(TX_THREAD *thread, CHAR *, void (*entry)(ULONG), ULONG input,
                      void *stack, ULONG stack_size, UINT priority, UINT threshold,
                      ULONG slice, UINT start) {
    CHECK(!thread->created && entry && stack);
    CHECK(input == 0 && priority == threshold && slice == TX_NO_TIME_SLICE && start == TX_AUTO_START);
    CHECK(reinterpret_cast<std::uintptr_t>(stack) % 8 == 0);
    CHECK(stack_size >= 1536);
    CHECK(!services::detail::initialized.load()); // Creation must finish before publication.
    *thread = {true, entry, stack, stack_size, priority};
    ++test::created_threads;
    return TX_SUCCESS;
}
UINT tx_thread_sleep(ULONG duration) {
    CHECK(test::interrupt_mask == TX_INT_ENABLE);
    CHECK(test::sleep_hook);
    test::sleep_hook(duration);
    return TX_SUCCESS;
}
void tx_kernel_enter() { throw std::runtime_error("host test must not enter a real RTOS"); }

namespace bsp {
Clock SystemClock() { return {nullptr, test::clock_now, test::clock_sleep}; }
CanPort Can(unsigned bus) {
    return bus < 2 ? CanPort{&test::buses[bus], test::send, test::start} : CanPort{};
}
uint32_t CanErrors(unsigned bus) { return bus < 2 ? test::buses[bus].errors : 0; }
Status CanAbort(unsigned bus) {
    if (bus >= 2) return Status::invalid;
    ++test::buses[bus].aborts;
    return Status::ok;
}
DigitalOut LedRed() { return {&test::red_high, test::pin_write}; }
DigitalOut LedGreen() { return {&test::green_high, test::pin_write}; }
DigitalOut LedBlue() { return {&test::blue_high, test::pin_write}; }
} // namespace bsp

namespace services::detail {
// The IMU producer is intentionally not scheduled here. The real snapshot/monitor
// code is tested against injected complete samples; BMI088 has its own host suite.
void imu_entry(ULONG) { throw std::runtime_error("IMU entry is outside this host harness"); }
}

int main() {
    try {
        test::initialization_and_mailbox();
        test::control_regressions();
        test::heartbeat_regressions();
        test::monitor_and_snapshot_regressions();
        std::cout << "service regressions passed (deterministic host mocks only)\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "service regression FAILED: " << error.what() << '\n';
        return 1;
    }
}
