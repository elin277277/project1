#include "DJIMotorHandler.hpp"
#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>

namespace
{
constexpr double twoPi = 6.2831853071795864769;
uint16_t ReadBE(const uint8_t *data)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) | data[1]);
}
int16_t Signed16(uint16_t value)
{
    return static_cast<int16_t>(value <= INT16_MAX ? value : static_cast<int32_t>(value) - 65536);
}
int32_t Signed32(uint32_t value)
{
    return value <= INT32_MAX ? static_cast<int32_t>(value)
                             : static_cast<int32_t>(static_cast<int64_t>(value) - 4294967296LL);
}
}

DJIMotor::DJIMotor(float reduction, uint16_t currentLimit, uint32_t feedbackTimeoutMs)
    : reduction_(reduction),
      configuredCurrentLimit_(std::min(currentLimit, protocolCurrentLimit)),
      feedbackTimeoutMs_(feedbackTimeoutMs),
      configurationValid_(std::isfinite(reduction) && reduction > 0.0f &&
                          feedbackTimeoutMs > 0 && feedbackTimeoutMs <= INT32_MAX)
{
    maxCurrent = configuredCurrentLimit_;
    speedPid.maxOut = static_cast<float>(configuredCurrentLimit_);
}

uint16_t DJIMotor::CurrentLimit() const
{
    return std::min(maxCurrent, configuredCurrentLimit_);
}

void DJIMotor::Relax()
{
    controlMode = RELAX_MODE;
    currentSet = 0;
    outputReady_ = false;
    speedPid.Clear();
    positionPid.Clear();
}

void DJIMotor::CommitOutput()
{
    // Bind this computation to the feedback fault epoch. A later RX fault must
    // suppress the command even if a subsequent good frame has already arrived.
    outputFaults_ = observedFaults_;
    outputMode_ = controlMode;
    outputReady_ = true;
}

bool DJIMotor::OutputSafe(uint32_t nowMs) const
{
    if (!configurationValid_ || !outputReady_ || MotorState != MOTOR_ONLINE ||
        (controlMode != SPD_MODE && controlMode != POS_MODE) || controlMode != outputMode_)
        return false;
    const uint32_t sequence = mailbox_.sequence.load();
    const uint32_t faults = mailbox_.faults.load();
    const uint32_t timestamp = mailbox_.timestamp.load();
    const bool valid = mailbox_.valid.load() != 0;
    return !(sequence & 1U) && valid && faults == outputFaults_ &&
           static_cast<uint32_t>(nowMs - timestamp) < feedbackTimeoutMs_ &&
           sequence == mailbox_.sequence.load();
}

bool DJIMotor::PublishFeedback(const uint8_t *data, uint32_t nowMs, bool valid)
{
    // This function has one RX writer, never reads main-thread-owned state.
    const uint16_t encoder = valid ? ReadBE(data) : 0;
    valid = valid && encoder < 8192;
    mailbox_.sequence.fetch_add(1);
    if (!valid)
    {
        mailbox_.faults.fetch_add(1);
        mailbox_.valid.store(0);
        writerInitialized_ = false;
        mailbox_.sequence.fetch_add(1);
        return false;
    }
    if (writerInitialized_ && static_cast<uint32_t>(nowMs - writerTimestamp_) >= feedbackTimeoutMs_)
    {
        mailbox_.faults.fetch_add(1);
        writerInitialized_ = false;
    }
    if (!writerInitialized_)
    {
        // Lost feedback makes intervening turns unknowable. Rebase position and
        // latch RELAX through the fault counter; explicit control-thread rearm only.
        writerEncoder_ = writerOrigin_ = encoder;
        writerTurns_ = 0;
        writerInitialized_ = true;
    }
    const int delta = static_cast<int>(encoder) - writerEncoder_;
    const int turn = delta < -4096 ? 1 : (delta > 4096 ? -1 : 0);
    if ((turn > 0 && writerTurns_ == INT32_MAX) || (turn < 0 && writerTurns_ == INT32_MIN))
    {
        mailbox_.faults.fetch_add(1);
        mailbox_.valid.store(0);
        writerInitialized_ = false;
        mailbox_.sequence.fetch_add(1);
        return false;
    }
    writerTurns_ += turn;
    writerEncoder_ = encoder;
    writerTimestamp_ = nowMs;
    mailbox_.encoderSpeed.store((static_cast<uint32_t>(encoder) << 16) | ReadBE(data + 2));
    mailbox_.current.store(ReadBE(data + 4));
    mailbox_.turns.store(static_cast<uint32_t>(writerTurns_));
    mailbox_.origin.store(writerOrigin_);
    mailbox_.timestamp.store(nowMs);
    mailbox_.valid.store(1);
    mailbox_.sequence.fetch_add(1);
    return true;
}

bool DJIMotor::RefreshFeedback(uint32_t nowMs)
{
    const uint32_t sequence = mailbox_.sequence.load();
    if (!configurationValid_ || (sequence & 1U))
    {
        Relax();
        MotorState = MOTOR_OFFLINE;
        return false;
    }
    const uint32_t encoderSpeed = mailbox_.encoderSpeed.load();
    const uint32_t current = mailbox_.current.load();
    const int32_t turns = Signed32(mailbox_.turns.load());
    const uint32_t origin = mailbox_.origin.load();
    const uint32_t timestamp = mailbox_.timestamp.load();
    const uint32_t faults = mailbox_.faults.load();
    const bool valid = mailbox_.valid.load() != 0;
    if (mailbox_.sequence.load() != sequence || !valid ||
        static_cast<uint32_t>(nowMs - timestamp) >= feedbackTimeoutMs_)
    {
        Relax();
        MotorState = MOTOR_OFFLINE;
        return false;
    }
    if (faults != observedFaults_)
    {
        Relax();
        observedFaults_ = faults;
    }
    if (sequence != observedSequence_)
    {
        auto &feedback = motorFeedback;
        const auto encoder = static_cast<uint16_t>(encoderSpeed >> 16);
        const auto rpm = Signed16(static_cast<uint16_t>(encoderSpeed));
        const int64_t ticks = static_cast<int64_t>(turns) * 8192 + encoder - origin;
        const double speed = rpm * (twoPi / (60.0 * reduction_));
        const double position = ticks * (twoPi / (8192.0 * reduction_));
        // Check representability BEFORE narrowing; pathological configuration
        // must fail closed rather than rely on overflowing a float conversion.
        if (!std::isfinite(speed) || !std::isfinite(position) ||
            std::fabs(speed) > std::numeric_limits<float>::max() ||
            std::fabs(position) > std::numeric_limits<float>::max())
        {
            Relax();
            MotorState = MOTOR_OFFLINE;
            return false;
        }
        feedback.last_ecd = static_cast<int16_t>(feedback.ecd);
        feedback.lastSpeedFdb = feedback.speedFdb;
        feedback.lastPositionFdb = feedback.positionFdb;
        feedback.ecd = encoder;
        feedback.speed_rpm = rpm;
        feedback.currentFdb = Signed16(static_cast<uint16_t>(current));
        feedback.speedFdb = static_cast<float>(speed);
        feedback.positionFdb = static_cast<float>(position);
        feedback.temperatureFdb = 0.0f; // C610 bytes 6/7 are reserved.
        feedback.roundCount = turns;
        Pre_AliveFlag = AliveFlag;
        AliveFlag = sequence / 2;
        observedSequence_ = sequence;
    }
    MotorState = MOTOR_ONLINE;
    return true;
}

DJIMotorHandler::DJIMotorHandler(bsp::CanPort bus0, bsp::CanPort bus1)
    : ports_{bus0, bus1}
{
}

bool DJIMotorHandler::registerMotor(DJIMotor *motor, unsigned bus, uint16_t canId)
{
    if (registrationSealed_ || !motor || bus >= 2 || !ports_[bus].send ||
        canId < 0x201 || canId > 0x208 || !motor->configurationValid() ||
        motor->registeredHandler_ || motors_[bus][canId - 0x201])
        return false;
    motor->Relax();
    motor->busIndex = bus;
    motor->canId = canId;
    motor->registeredHandler_ = this;
    motors_[bus][canId - 0x201] = motor;
    return true;
}

void DJIMotorHandler::receiveBus0(void *context, const bsp::CanFrame &frame, uint32_t timestampMs)
{
    if (context) static_cast<DJIMotorHandler *>(context)->updateFeedback(0, frame, timestampMs);
}

void DJIMotorHandler::receiveBus1(void *context, const bsp::CanFrame &frame, uint32_t timestampMs)
{
    if (context) static_cast<DJIMotorHandler *>(context)->updateFeedback(1, frame, timestampMs);
}

bool DJIMotorHandler::updateFeedback(unsigned bus, const bsp::CanFrame &frame, uint32_t timestampMs)
{
    if (bus >= 2 || frame.id < 0x201 || frame.id > 0x208) return false;
    auto *motor = motors_[bus][frame.id - 0x201];
    return motor && motor->PublishFeedback(frame.data, timestampMs,
                                           frame.size == 8 && !frame.extended && !frame.remote);
}

void DJIMotorHandler::RelaxAll()
{
    for (auto &list : motors_)
        for (auto *motor : list)
            if (motor) motor->Relax();
}

bool DJIMotorHandler::sendControlData(uint32_t nowMs)
{
    bool occupied[2][2]{};
    for (unsigned bus = 0; bus < 2; ++bus)
        for (unsigned index = 0; index < 8; ++index)
            if (motors_[bus][index]) occupied[bus][index / 4] = true;

    for (unsigned bus = 0; bus < 2; ++bus)
        for (unsigned group = 0; group < 2; ++group)
        {
            if (!occupied[bus][group]) continue;
            bsp::CanFrame frame{};
            frame.id = group == 0 ? 0x200 : 0x1ff;
            frame.size = 8;
            for (unsigned slot = 0; slot < 4; ++slot)
            {
                const unsigned index = group * 4 + slot;
                auto *motor = motors_[bus][index];
                if (!motor) continue;
                motor->AliveCheck(nowMs);
                if (motor->busIndex != bus || motor->canId != 0x201 + index ||
                    !motor->OutputSafe(nowMs))
                    motor->Relax();
                const int limit = motor->CurrentLimit();
                motor->currentSet = static_cast<int16_t>(std::max(-limit, std::min<int>(limit, motor->currentSet)));
                const auto current = static_cast<uint16_t>(motor->currentSet);
                frame.data[slot * 2] = static_cast<uint8_t>(current >> 8);
                frame.data[slot * 2 + 1] = static_cast<uint8_t>(current);
            }
            // An RX callback may run while other slots/groups are being packed.
            // Recheck this group's fault epochs immediately before transmission.
            for (unsigned slot = 0; slot < 4; ++slot)
            {
                auto *motor = motors_[bus][group * 4 + slot];
                if (motor && !motor->OutputSafe(nowMs))
                {
                    motor->Relax();
                    frame.data[slot * 2] = frame.data[slot * 2 + 1] = 0;
                }
            }
            if (ports_[bus].send(ports_[bus].context, frame) != bsp::Status::ok)
            {
                RelaxAll();
                // Already queued frames cannot be recalled. Try zero once per
                // occupied group, bounded/nonblocking even when the bus is broken.
                for (unsigned zeroBus = 0; zeroBus < 2; ++zeroBus)
                    for (unsigned zeroGroup = 0; zeroGroup < 2; ++zeroGroup)
                        if (occupied[zeroBus][zeroGroup])
                        {
                            bsp::CanFrame zero{};
                            zero.id = zeroGroup == 0 ? 0x200 : 0x1ff;
                            zero.size = 8;
                            (void)ports_[zeroBus].send(ports_[zeroBus].context, zero);
                        }
                return false;
            }
        }
    return true;
}

void DJIMotorHandler::AllMotorAliveCheck(uint32_t nowMs)
{
    for (auto &list : motors_)
        for (auto *motor : list)
            if (motor) motor->AliveCheck(nowMs);
}
