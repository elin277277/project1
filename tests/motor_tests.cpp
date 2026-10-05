#include "DJIMotorHandler.hpp"
#include "M2006.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

// No platform headers, HAL mocks, heap, or target hardware needed.
#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
    std::abort(); } } while (false)

namespace
{
constexpr float pi = 3.14159265358979323846f;
bool Near(float a, float b, float tolerance = 0.0001f)
{
    return std::fabs(a - b) <= tolerance;
}

struct FakeCan
{
    bsp::CanFrame sent[64]{};
    unsigned calls{};
    unsigned failFrom{};
    bsp::Status failure{bsp::Status::busy};
    bsp::CanReceive receiver{};
    void *receiverContext{};
    void (*sendHook)(void *){};
    void *hookContext{};

    bsp::CanPort port() { return {this, Send, Start}; }
    static bsp::Status Send(void *context, const bsp::CanFrame &frame)
    {
        auto &self = *static_cast<FakeCan *>(context);
        CHECK(self.calls < 64);
        self.sent[self.calls++] = frame;
        if (self.sendHook) self.sendHook(self.hookContext);
        return self.failFrom && self.calls >= self.failFrom ? self.failure : bsp::Status::ok;
    }
    static bsp::Status Start(void *context, bsp::CanReceive receiver, void *receiverContext)
    {
        auto &self = *static_cast<FakeCan *>(context);
        if (!receiver || self.receiver) return bsp::Status::invalid;
        self.receiver = receiver;
        self.receiverContext = receiverContext;
        return bsp::Status::ok;
    }
    void receive(const bsp::CanFrame &frame, uint32_t nowMs)
    {
        CHECK(receiver);
        receiver(receiverContext, frame, nowMs);
    }
    void clear() { calls = 0; failFrom = 0; sendHook = nullptr; hookContext = nullptr; }
};

void PutBE(uint8_t *data, uint16_t value)
{
    data[0] = static_cast<uint8_t>(value >> 8);
    data[1] = static_cast<uint8_t>(value);
}
bsp::CanFrame Feedback(uint32_t id, uint16_t encoder = 0, int16_t rpm = 0, int16_t current = 0)
{
    bsp::CanFrame frame{};
    frame.id = id;
    frame.size = 8;
    PutBE(frame.data, encoder);
    PutBE(frame.data + 2, static_cast<uint16_t>(rpm));
    PutBE(frame.data + 4, static_cast<uint16_t>(current));
    frame.data[6] = frame.data[7] = 0xff; // Reserved, NOT temperature.
    return frame;
}
void ExpectFrame(const bsp::CanFrame &frame, uint32_t id, int a = 0, int b = 0, int c = 0, int d = 0)
{
    CHECK(frame.id == id && frame.size == 8 && !frame.extended && !frame.remote);
    const int currents[]{a, b, c, d};
    for (unsigned i = 0; i < 4; ++i)
    {
        const auto value = static_cast<uint16_t>(currents[i]);
        CHECK(frame.data[i * 2] == static_cast<uint8_t>(value >> 8));
        CHECK(frame.data[i * 2 + 1] == static_cast<uint8_t>(value));
    }
}
void Arm(M2006 &motor, float speed, uint32_t nowMs)
{
    CHECK(motor.AliveCheck(nowMs) == DJIMotor::MOTOR_ONLINE);
    motor.speedPid.Tuning(1.0f, 0.0f, 0.0f);
    motor.speedPid.maxOut = 65535.0f;
    motor.controlMode = DJIMotor::SPD_MODE;
    motor.speedSet = speed;
    motor.setOutput(0.001f, nowMs);
    CHECK(motor.controlMode == DJIMotor::SPD_MODE);
}

void RegistrationAndCallbacks()
{
    FakeCan can0, can1;
    DJIMotorHandler empty{can0.port(), can1.port()};
    CHECK(empty.sendControlData(0));
    CHECK(can0.calls == 0 && can1.calls == 0); // No empty-group queue traffic.
    M2006 a{{36.0f, 0, 100}}, b{{36.0f, 0, 100}}, c{{36.0f, 0, 100}};
    CHECK(!empty.registerMotor(nullptr, 0, 0x201));
    CHECK(!empty.registerMotor(&a, 2, 0x201));
    CHECK(!empty.registerMotor(&a, static_cast<unsigned>(-1), 0x201));
    CHECK(!empty.registerMotor(&a, 0, 0x200));
    CHECK(!empty.registerMotor(&a, 0, 0x209));
    CHECK(empty.registerMotor(&a, 0, 0x201));
    CHECK(!empty.registerMotor(&b, 0, 0x201));
    CHECK(!empty.registerMotor(&a, 0, 0x202));
    CHECK(!empty.registerMotor(&a, 1, 0x201));
    CHECK(empty.registerMotor(&b, 1, 0x201)); // Same ID on different buses is valid.
    CHECK(empty.registerMotor(&c, 0, 0x208));
    DJIMotorHandler other{can0.port()};
    CHECK(!other.registerMotor(&a, 0, 0x201)); // Reject cross-handler ownership.
    M2006 unregistered{{1.0f, 0, 10}};
    CHECK(!other.registerMotor(&unregistered, 1, 0x201)); // Omitted port.
    DJIMotorHandler absent{{}};
    CHECK(!absent.registerMotor(&unregistered, 0, 0x201));
    empty.sealRegistration();
    CHECK(!empty.registerMotor(&unregistered, 0, 0x202));
    const auto port0 = can0.port(), port1 = can1.port();
    CHECK(port0.start(port0.context, DJIMotorHandler::receiveBus0, &empty) == bsp::Status::ok);
    CHECK(port1.start(port1.context, DJIMotorHandler::receiveBus1, &empty) == bsp::Status::ok);
    can0.receive(Feedback(0x201, 123), 1);
    can1.receive(Feedback(0x201, 456), 1);
    can0.receive(Feedback(0x208, 789), 1);
    empty.AllMotorAliveCheck(1);
    CHECK(a.motorFeedback.ecd == 123 && b.motorFeedback.ecd == 456 && c.motorFeedback.ecd == 789);
    CHECK(!empty.updateFeedback(2, Feedback(0x201), 1));
    CHECK(!empty.updateFeedback(0, Feedback(0x200), 1));
    CHECK(!empty.updateFeedback(0, Feedback(0x209), 1));
    CHECK(!empty.updateFeedback(0, Feedback(0x202), 1));
    CHECK(!empty.updateFeedback(0, Feedback(0x10000201), 1));
    DJIMotorHandler::receiveBus0(nullptr, Feedback(0x201), 1);
    CHECK(empty.sendControlData(1));
    CHECK(can0.calls == 2 && can1.calls == 1);
    ExpectFrame(can0.sent[0], 0x200);
    ExpectFrame(can0.sent[1], 0x1ff);
    ExpectFrame(can1.sent[0], 0x200);
}

void ConfigurationAndDefaults()
{
    FakeCan can;
    DJIMotorHandler handler{can.port()};
    M2006 motor{{2.0f, 0, 13}};
    CHECK(motor.configurationValid());
    CHECK(motor.maxCurrent == 0 && motor.currentSet == 0);
    CHECK(motor.controlMode == DJIMotor::RELAX_MODE && motor.MotorState == DJIMotor::MOTOR_OFFLINE);
    CHECK(motor.speedPid.kp == 0 && motor.speedPid.ki == 0 && motor.speedPid.kd == 0);
    CHECK(motor.positionPid.kp == 0 && motor.positionPid.ki == 0 && motor.positionPid.kd == 0);
    CHECK(handler.registerMotor(&motor, 0, 0x201));
    CHECK(handler.updateFeedback(0, Feedback(0x201), 0));
    motor.maxCurrent = 65535; // Public fields cannot bypass constructor safety ceiling.
    Arm(motor, 65535.0f, 0);
    CHECK(motor.currentSet == 0);
    CHECK(handler.sendControlData(0));
    ExpectFrame(can.sent[0], 0x200);
    M2006 missing{{}}, zero{{0.0f, 100, 10}}, negative{{-1.0f, 100, 10}};
    M2006 nan{{std::numeric_limits<float>::quiet_NaN(), 100, 10}};
    M2006 inf{{std::numeric_limits<float>::infinity(), 100, 10}};
    M2006 timeoutZero{{1.0f, 100, 0}}, timeoutHuge{{1.0f, 100, UINT32_MAX}};
    M2006 *invalid[]{&missing, &zero, &negative, &nan, &inf, &timeoutZero, &timeoutHuge};
    for (auto *candidate : invalid)
    {
        CHECK(!candidate->configurationValid());
        CHECK(!handler.registerMotor(candidate, 0, 0x202));
        candidate->controlMode = DJIMotor::SPD_MODE;
        candidate->setOutput(0.001f, 0);
        CHECK(candidate->controlMode == DJIMotor::RELAX_MODE && candidate->currentSet == 0);
    }
    M2006 tinyReduction{{std::numeric_limits<float>::denorm_min(), 100, 10}};
    CHECK(handler.registerMotor(&tinyReduction, 0, 0x202));
    CHECK(handler.updateFeedback(0, Feedback(0x202, 0, 32767), 0));
    tinyReduction.controlMode = DJIMotor::SPD_MODE;
    tinyReduction.setOutput(0.001f, 0);
    CHECK(tinyReduction.controlMode == DJIMotor::RELAX_MODE && tinyReduction.currentSet == 0);
    CHECK(tinyReduction.MotorState == DJIMotor::MOTOR_OFFLINE);
}

void SignedFeedbackAndReduction()
{
    FakeCan can;
    DJIMotorHandler handler{can.port()};
    M2006 motor{{2.0f, 0, 31}}, other{{72.0f, 0, 31}};
    CHECK(handler.registerMotor(&motor, 0, 0x201));
    CHECK(handler.registerMotor(&other, 0, 0x202));
    CHECK(handler.updateFeedback(0, Feedback(0x201, 4096, -60, -1234), 100));
    CHECK(motor.AliveCheck(100) == DJIMotor::MOTOR_ONLINE);
    CHECK(motor.motorFeedback.ecd == 4096 && motor.motorFeedback.speed_rpm == -60);
    CHECK(motor.motorFeedback.currentFdb == -1234);
    CHECK(Near(motor.motorFeedback.speedFdb, -pi));
    CHECK(motor.motorFeedback.positionFdb == 0 && motor.motorFeedback.temperatureFdb == 0);
    CHECK(handler.updateFeedback(0, Feedback(0x201, 6144, -32768, -32768), 101));
    CHECK(motor.AliveCheck(101) == DJIMotor::MOTOR_ONLINE);
    CHECK(motor.motorFeedback.speed_rpm == -32768 && motor.motorFeedback.currentFdb == -32768);
    CHECK(Near(motor.motorFeedback.positionFdb, pi / 4));
    CHECK(motor.motorFeedback.last_ecd == 4096 && Near(motor.motorFeedback.lastSpeedFdb, -pi));
    CHECK(handler.updateFeedback(0, Feedback(0x202, 0, 60, 32767), 101));
    CHECK(other.AliveCheck(101) == DJIMotor::MOTOR_ONLINE);
    CHECK(Near(other.motorFeedback.speedFdb, 2 * pi / 72));
    CHECK(other.motorFeedback.currentFdb == 32767);
    CHECK(other.motorFeedback.temperatureFdb == 0);
}

void EncoderWrapAndMailbox()
{
    FakeCan can;
    DJIMotorHandler handler{can.port()};
    M2006 forward{{1.0f, 0, 100}}, reverse{{1.0f, 0, 100}};
    CHECK(handler.registerMotor(&forward, 0, 0x201));
    CHECK(handler.registerMotor(&reverse, 0, 0x202));
    CHECK(handler.updateFeedback(0, Feedback(0x201, 8190), 0));
    CHECK(handler.updateFeedback(0, Feedback(0x201, 2), 1));
    CHECK(forward.AliveCheck(1) == DJIMotor::MOTOR_ONLINE);
    CHECK(forward.motorFeedback.roundCount == 1);
    CHECK(Near(forward.motorFeedback.positionFdb, 4 * 2 * pi / 8192));
    CHECK(handler.updateFeedback(0, Feedback(0x201, 8190), 2));
    CHECK(forward.AliveCheck(2) == DJIMotor::MOTOR_ONLINE);
    CHECK(forward.motorFeedback.roundCount == 0 && Near(forward.motorFeedback.positionFdb, 0));
    CHECK(handler.updateFeedback(0, Feedback(0x202, 1), 0));
    CHECK(handler.updateFeedback(0, Feedback(0x202, 8191), 1));
    CHECK(reverse.AliveCheck(1) == DJIMotor::MOTOR_ONLINE);
    CHECK(reverse.motorFeedback.roundCount == -1);
    CHECK(Near(reverse.motorFeedback.positionFdb, -2 * 2 * pi / 8192));
    // Multiple ISR writes between main-thread reads preserve turns, not just last encoder.
    CHECK(handler.updateFeedback(0, Feedback(0x201, 2000), 3));
    CHECK(handler.updateFeedback(0, Feedback(0x201, 5000), 4));
    CHECK(handler.updateFeedback(0, Feedback(0x201, 8000), 5));
    CHECK(handler.updateFeedback(0, Feedback(0x201, 2000), 6));
    CHECK(forward.AliveCheck(6) == DJIMotor::MOTOR_ONLINE);
    CHECK(forward.motorFeedback.roundCount == 2);
    CHECK(Near(forward.motorFeedback.positionFdb, (8192 + 2002) * 2 * pi / 8192));
    const auto alive = forward.AliveFlag;
    CHECK(forward.AliveCheck(6) == DJIMotor::MOTOR_ONLINE && forward.AliveFlag == alive);
}

void GroupedCommandsAndCurrentCaps()
{
    FakeCan can0, can1;
    DJIMotorHandler handler{can0.port(), can1.port()};
    M2006 a{{36.0f, 65535, 100}}, b{{36.0f, 10000, 100}};
    M2006 c{{36.0f, 10000, 100}}, d{{36.0f, 10000, 100}};
    M2006 e{{36.0f, 128, 100}}, f{{36.0f, 128, 100}};
    M2006 *motors[]{&a, &b, &c, &d, &e, &f};
    const unsigned buses[]{0, 0, 0, 0, 1, 1};
    const uint16_t ids[]{0x201, 0x202, 0x204, 0x208, 0x201, 0x205};
    const float speeds[]{20000, -20000, 1, -1, 1000, -1000};
    for (unsigned i = 0; i < 6; ++i)
    {
        CHECK(handler.registerMotor(motors[i], buses[i], ids[i]));
        CHECK(handler.updateFeedback(buses[i], Feedback(ids[i]), 10));
        Arm(*motors[i], speeds[i], 10);
    }
    CHECK(a.maxCurrent == 10000);
    CHECK(handler.sendControlData(10));
    CHECK(can0.calls == 2 && can1.calls == 2);
    ExpectFrame(can0.sent[0], 0x200, 10000, -10000, 0, 1);
    ExpectFrame(can0.sent[1], 0x1ff, 0, 0, 0, -1);
    ExpectFrame(can1.sent[0], 0x200, 128);
    ExpectFrame(can1.sent[1], 0x1ff, -128);
    CHECK(can0.sent[0].data[0] == 0x27 && can0.sent[0].data[1] == 0x10);
    CHECK(can0.sent[0].data[2] == 0xd8 && can0.sent[0].data[3] == 0xf0);
    can0.clear(); can1.clear();
    a.maxCurrent = 7; // Lowering a limit after computation is honored before transmit.
    b.Relax();
    CHECK(handler.sendControlData(10));
    ExpectFrame(can0.sent[0], 0x200, 7, 0, 0, 1);
    CHECK(b.currentSet == 0);
    // Manual current changes cannot produce drive without a successful computation.
    b.controlMode = DJIMotor::SPD_MODE;
    b.currentSet = 900;
    can0.clear(); can1.clear();
    CHECK(handler.sendControlData(10));
    ExpectFrame(can0.sent[0], 0x200, 7, 0, 0, 1);
    CHECK(b.controlMode == DJIMotor::RELAX_MODE);
}

void TimeoutWrapAndNoAutomaticRecovery()
{
    FakeCan can;
    DJIMotorHandler handler{can.port()};
    M2006 motor{{3.0f, 1000, 27}};
    CHECK(handler.registerMotor(&motor, 0, 0x201));
    motor.controlMode = DJIMotor::SPD_MODE;
    motor.currentSet = 100;
    motor.setOutput(0.001f, 0);
    CHECK(motor.currentSet == 0 && motor.controlMode == DJIMotor::RELAX_MODE);
    const uint32_t start = UINT32_MAX - 10;
    CHECK(handler.updateFeedback(0, Feedback(0x201, 50), start));
    Arm(motor, 200, start);
    CHECK(motor.AliveCheck(start + 26U) == DJIMotor::MOTOR_ONLINE);
    CHECK(motor.AliveCheck(start + 27U) == DJIMotor::MOTOR_OFFLINE);
    CHECK(motor.currentSet == 0 && motor.controlMode == DJIMotor::RELAX_MODE);
    CHECK(handler.sendControlData(start + 27U));
    ExpectFrame(can.sent[0], 0x200);
    CHECK(handler.updateFeedback(0, Feedback(0x201, 7000), start + 28U));
    motor.setOutput(0.001f, start + 28U);
    CHECK(motor.MotorState == DJIMotor::MOTOR_ONLINE && motor.currentSet == 0);
    CHECK(motor.controlMode == DJIMotor::RELAX_MODE && motor.motorFeedback.positionFdb == 0);
    CHECK(handler.updateFeedback(0, Feedback(0x201, 7001), start + 29U));
    motor.setOutput(0.001f, start + 29U);
    CHECK(motor.currentSet == 0 && motor.controlMode == DJIMotor::RELAX_MODE);
    Arm(motor, 200, start + 29U); // Explicit main-thread rearm after observing recovery.
    CHECK(motor.currentSet == 200);
    // A missed timeout must latch even when a new frame arrives before the next main tick.
    CHECK(handler.updateFeedback(0, Feedback(0x201, 4000), start + 56U));
    motor.setOutput(0.001f, start + 56U);
    CHECK(motor.currentSet == 0 && motor.controlMode == DJIMotor::RELAX_MODE);
    CHECK(motor.motorFeedback.positionFdb == 0);
    Arm(motor, 200, start + 56U);
    motor.Relax();
    CHECK(motor.speedPid.result == 0 && motor.positionPid.result == 0);
    motor.setOutput(0.001f, start + 56U);
    CHECK(motor.currentSet == 0 && motor.controlMode == DJIMotor::RELAX_MODE);
    Arm(motor, 200, start + 56U);
    motor.setOutput();
    CHECK(motor.currentSet == 0 && motor.controlMode == DJIMotor::RELAX_MODE);
    CHECK(motor.AliveCheck() == DJIMotor::MOTOR_OFFLINE);
}

void BadFramesLatchAndStayInRxMailbox()
{
    for (unsigned badKind = 0; badKind < 7; ++badKind)
    {
        FakeCan can;
        DJIMotorHandler handler{can.port()};
        M2006 motor{{36.0f, 1000, 100}};
        CHECK(handler.registerMotor(&motor, 0, 0x201));
        CHECK(handler.updateFeedback(0, Feedback(0x201), 0));
        Arm(motor, 100, 0);
        auto bad = Feedback(0x201);
        if (badKind == 0) bad.size = 0;
        if (badKind == 1) bad.size = 7;
        if (badKind == 2) bad.size = 9;
        if (badKind == 3) bad.extended = true;
        if (badKind == 4) bad.remote = true;
        if (badKind == 5) PutBE(bad.data, 8192);
        if (badKind == 6) PutBE(bad.data, 65535);
        CHECK(!handler.updateFeedback(0, bad, 1));
        CHECK(motor.currentSet == 100 && motor.controlMode == DJIMotor::SPD_MODE);
        CHECK(motor.speedPid.result == 100); // ISR never clears/mutates control-thread PID.
        if (badKind & 1U)
            CHECK(handler.updateFeedback(0, Feedback(0x201, 100), 2));
        CHECK(handler.sendControlData(2)); // Fault after compute, before transmit.
        ExpectFrame(can.sent[0], 0x200);
        CHECK(motor.currentSet == 0 && motor.controlMode == DJIMotor::RELAX_MODE);
        CHECK(handler.updateFeedback(0, Feedback(0x201, 101), 3));
        motor.setOutput(0.001f, 3);
        CHECK(motor.currentSet == 0 && motor.controlMode == DJIMotor::RELAX_MODE);
        Arm(motor, 100, 3);
        CHECK(motor.currentSet == 100);
    }
}

void PidSpeedAndPositionCascade()
{
    FakeCan can;
    DJIMotorHandler handler{can.port()};
    M2006 motor{{2.0f, 1000, 100}};
    CHECK(handler.registerMotor(&motor, 0, 0x201));
    CHECK(handler.updateFeedback(0, Feedback(0x201), 0));
    CHECK(motor.AliveCheck(0) == DJIMotor::MOTOR_ONLINE);
    motor.speedPid.Tuning(10, 20, 0);
    motor.speedPid.maxIOut = 100;
    motor.speedSet = 2;
    motor.controlMode = DJIMotor::SPD_MODE;
    motor.setOutput(0.01f, 0);
    CHECK(Near(motor.speedPid.iResult, 0.4f));
    CHECK(Near(motor.speedPid.result, 20.4f) && motor.currentSet == 20);
    motor.setOutput(0.02f, 0);
    CHECK(Near(motor.speedPid.iResult, 1.2f) && motor.currentSet == 21);
    motor.positionPid.Tuning(2, 0, 0);
    motor.positionPid.maxOut = 3; // Output-shaft rad/s, not current counts.
    motor.positionSet = 10;     // Radians: intentionally not wrapped to +/-pi.
    motor.speedPid.Tuning(100, 0, 0);
    motor.controlMode = DJIMotor::POS_MODE;
    motor.setOutput(0.01f, 0);
    CHECK(Near(motor.positionPid.result, 3) && Near(motor.speedPid.ref, 3));
    CHECK(motor.currentSet == 300 && motor.speedPid.iResult == 0);
    motor.positionSet = 0.5f;
    motor.setOutput(0.01f, 0);
    CHECK(Near(motor.speedPid.ref, 1) && motor.currentSet == 100);
    motor.positionPid.maxOut = 0.4f;
    motor.setOutput(0.01f, 0);
    CHECK(Near(motor.speedPid.ref, 0.4f) && motor.currentSet == 40);
    CHECK(handler.updateFeedback(0, Feedback(0x201, 0, 60), 1));
    motor.setOutput(0.01f, 1);
    CHECK(Near(motor.speedPid.fdb, pi));
    CHECK(motor.currentSet == static_cast<int16_t>((0.4f - pi) * 100));
    motor.Relax();
    CHECK(motor.positionPid.result == 0 && motor.speedPid.result == 0);
    CHECK(handler.sendControlData(1));
    ExpectFrame(can.sent[0], 0x200);
}

void InvalidControlFailsZero()
{
    for (unsigned badKind = 0; badKind < 11; ++badKind)
    {
        FakeCan can;
        DJIMotorHandler handler{can.port()};
        M2006 motor{{36.0f, 1000, 17}};
        CHECK(handler.registerMotor(&motor, 0, 0x201));
        CHECK(handler.updateFeedback(0, Feedback(0x201), 0));
        Arm(motor, 100, 0);
        float dt = 0.001f;
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        if (badKind == 0) dt = 0;
        if (badKind == 1) dt = -0.001f;
        if (badKind == 2) dt = nan;
        if (badKind == 3) dt = inf;
        if (badKind == 4) dt = 0.018f; // Configured timeout, not hardcoded 100 ms.
        if (badKind == 5) motor.speedSet = nan;
        if (badKind == 6) motor.speedPid.kp = inf;
        if (badKind == 7) motor.speedPid.maxOut = -1;
        if (badKind == 8) { motor.speedPid.kp = std::numeric_limits<float>::max(); motor.speedSet = 2; }
        if (badKind == 9) { motor.controlMode = DJIMotor::POS_MODE; motor.positionSet = inf; }
        if (badKind == 10) { motor.controlMode = DJIMotor::POS_MODE; motor.positionPid.maxOut = nan; }
        motor.setOutput(dt, 0);
        CHECK(motor.currentSet == 0 && motor.controlMode == DJIMotor::RELAX_MODE);
        CHECK(motor.speedPid.result == 0 && motor.positionPid.result == 0);
        CHECK(handler.sendControlData(0));
        ExpectFrame(can.sent[0], 0x200);
        motor.speedPid.Tuning(1, 0, 0);
        motor.speedPid.maxOut = 1000;
        motor.speedSet = 100;
        motor.positionSet = 0;
        motor.positionPid.maxOut = 0;
        motor.setOutput(0.001f, 0);
        CHECK(motor.currentSet == 0 && motor.controlMode == DJIMotor::RELAX_MODE);
    }
}

struct InterruptFault
{
    DJIMotorHandler *handler;
    unsigned bus;
    uint16_t id;
    static void Inject(void *context)
    {
        auto &self = *static_cast<InterruptFault *>(context);
        auto bad = Feedback(self.id);
        bad.remote = true;
        CHECK(!self.handler->updateFeedback(self.bus, bad, 1));
        // Even a good frame immediately following the fault cannot revive old output.
        CHECK(self.handler->updateFeedback(self.bus, Feedback(self.id), 1));
    }
};

void FaultWhileSendingEarlierGroup()
{
    for (unsigned targetBus = 0; targetBus < 2; ++targetBus)
    {
        FakeCan can0, can1;
        DJIMotorHandler handler{can0.port(), can1.port()};
        M2006 first{{36.0f, 1000, 100}}, later{{36.0f, 1000, 100}};
        const uint16_t targetId = targetBus == 0 ? 0x205 : 0x201;
        CHECK(handler.registerMotor(&first, 0, 0x201));
        CHECK(handler.registerMotor(&later, targetBus, targetId));
        CHECK(handler.updateFeedback(0, Feedback(0x201), 0));
        CHECK(handler.updateFeedback(targetBus, Feedback(targetId), 0));
        Arm(first, 100, 0); Arm(later, 200, 0);
        InterruptFault interrupt{&handler, targetBus, targetId};
        can0.sendHook = InterruptFault::Inject;
        can0.hookContext = &interrupt;
        CHECK(handler.sendControlData(1));
        ExpectFrame(can0.sent[0], 0x200, 100);
        if (targetBus == 0) ExpectFrame(can0.sent[1], 0x1ff);
        else ExpectFrame(can1.sent[0], 0x200);
        CHECK(later.currentSet == 0 && later.controlMode == DJIMotor::RELAX_MODE);
    }
}

void TransportFailureRelaxesAllAndBoundsZeroAttempts()
{
    const bsp::Status failures[]{bsp::Status::busy, bsp::Status::not_ready, bsp::Status::timeout,
                                 bsp::Status::io_error, bsp::Status::invalid};
    for (const auto failure : failures)
    {
        FakeCan can0, can1;
        DJIMotorHandler handler{can0.port(), can1.port()};
        M2006 a{{36.0f, 1000, 100}}, b{{36.0f, 1000, 100}};
        M2006 c{{36.0f, 1000, 100}}, d{{36.0f, 1000, 100}};
        M2006 *motors[]{&a, &b, &c, &d};
        for (unsigned i = 0; i < 4; ++i)
        {
            const auto id = static_cast<uint16_t>((i & 1U) ? 0x205 : 0x201);
            CHECK(handler.registerMotor(motors[i], i / 2, id));
            CHECK(handler.updateFeedback(i / 2, Feedback(id), 0));
            Arm(*motors[i], 100, 0);
        }
        can0.failFrom = 2; // One nonzero accepted, second fails; no more drive sends.
        can0.failure = failure;
        can1.failFrom = 1;
        can1.failure = failure;
        CHECK(!handler.sendControlData(0));
        CHECK(can0.calls == 4 && can1.calls == 2); // Two attempts + four bounded zeros.
        ExpectFrame(can0.sent[0], 0x200, 100);
        ExpectFrame(can0.sent[1], 0x1ff, 100);
        ExpectFrame(can0.sent[2], 0x200);
        ExpectFrame(can0.sent[3], 0x1ff);
        ExpectFrame(can1.sent[0], 0x200);
        ExpectFrame(can1.sent[1], 0x1ff);
        for (auto *motor : motors)
            CHECK(motor->controlMode == DJIMotor::RELAX_MODE && motor->currentSet == 0);
        can0.clear(); can1.clear();
        for (auto *motor : motors) motor->setOutput(0.001f, 0);
        CHECK(handler.sendControlData(0)); // Transport recovery alone must not drive.
        ExpectFrame(can0.sent[0], 0x200);
        ExpectFrame(can0.sent[1], 0x1ff);
        ExpectFrame(can1.sent[0], 0x200);
        ExpectFrame(can1.sent[1], 0x1ff);
    }
}
} // namespace

int main()
{
    RegistrationAndCallbacks();
    ConfigurationAndDefaults();
    SignedFeedbackAndReduction();
    EncoderWrapAndMailbox();
    GroupedCommandsAndCurrentCaps();
    TimeoutWrapAndNoAutomaticRecovery();
    BadFramesLatchAndStayInRxMailbox();
    PidSpeedAndPositionCascade();
    InvalidControlFailsZero();
    FaultWhileSendingEarlierGroup();
    TransportFailureRelaxesAllAndBoundsZeroAttempts();
    std::puts("motor_tests: all checks passed");
}
