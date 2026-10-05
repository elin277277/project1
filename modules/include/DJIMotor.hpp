#ifndef DJIMOTOR_HPP
#define DJIMOTOR_HPP

#include "pid.hpp"
#include <atomic>
#include <cstdint>

class DJIMotorHandler;

// C610 protocol support for the M2006 example, NOT a universal DJI motor driver.
// Register lifetime-stable motors before starting CAN RX. Only the control
// thread accesses public state/PIDs; the RX callback writes an atomic mailbox.
class DJIMotor
{
public:
    enum MotorControlModeType { RELAX_MODE = 0, SPD_MODE = 1, POS_MODE = 2 };
    enum MotorStateTypedef { MOTOR_OFFLINE = 0, MOTOR_ONLINE = 1 };
    static constexpr uint16_t protocolCurrentLimit = 10000;

    struct MotorFeedBack
    {
        int16_t last_ecd{};
        uint16_t ecd{};
        int16_t speed_rpm{};     // Rotor rpm, not output-shaft rpm.
        float currentFdb{};      // Signed C610 feedback counts, NOT amperes.
        float speedFdb{};        // Output shaft rad/s, using configured reduction.
        float lastSpeedFdb{};
        float positionFdb{};     // Multi-turn output shaft rad, first frame is origin.
        float lastPositionFdb{};
        float temperatureFdb{};  // C610 has no temperature field; remains zero.
        int32_t roundCount{};    // Signed rotor turns since initial encoder frame.
    };

    MotorStateTypedef MotorState{MOTOR_OFFLINE};
    MotorControlModeType controlMode{RELAX_MODE};
    MotorFeedBack motorFeedback{};
    uint16_t canId{};            // Registration metadata; do not change after registration.
    unsigned busIndex{2};
    PID speedPid{0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    PID positionPid{0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    float speedSet{};            // Output shaft rad/s.
    float positionSet{};         // Multi-turn output shaft rad, NOT wrapped to +/-pi.
    int16_t currentSet{};        // C610 command counts; updated by setOutput().
    uint16_t maxCurrent{};       // May lower, but never raise constructor safety limit.
    uint32_t AliveFlag{}, Pre_AliveFlag{};

    virtual void setOutput(float dtSeconds, uint32_t nowMs) = 0;
    virtual MotorStateTypedef AliveCheck(uint32_t nowMs) = 0;
    // Missing timing information must fail closed; use the explicit overloads.
    virtual void setOutput() { Relax(); }
    virtual MotorStateTypedef AliveCheck() { Relax(); return MotorState = MOTOR_OFFLINE; }
    void Relax();
    bool configurationValid() const { return configurationValid_; }
    uint32_t feedbackFaultEpoch() const { return mailbox_.faults.load(); }
    virtual ~DJIMotor() = default;
    DJIMotor(const DJIMotor &) = delete;
    DJIMotor &operator=(const DJIMotor &) = delete;

protected:
    DJIMotor(float reduction, uint16_t currentLimit, uint32_t feedbackTimeoutMs);
    bool RefreshFeedback(uint32_t nowMs);
    uint16_t CurrentLimit() const;
    uint32_t FeedbackTimeoutMs() const { return feedbackTimeoutMs_; }
    void CommitOutput();

private:
    friend class DJIMotorHandler;
    const float reduction_;
    const uint16_t configuredCurrentLimit_;
    const uint32_t feedbackTimeoutMs_;
    const bool configurationValid_;
    DJIMotorHandler *registeredHandler_{};
    // Single RX writer per motor. Plain-data seqlocks would be a C++ data race;
    // all shared values are atomic and readers make only one bounded attempt.
    static_assert(ATOMIC_INT_LOCK_FREE == 2, "CAN RX requires lock-free 32-bit atomics");
    struct Mailbox
    {
        std::atomic<uint32_t> sequence{0}, encoderSpeed{0}, current{0};
        std::atomic<uint32_t> turns{0}, origin{0}, timestamp{0}, faults{0}, valid{0};
    } mailbox_;
    bool writerInitialized_{};
    uint16_t writerEncoder_{}, writerOrigin_{};
    int32_t writerTurns_{};
    uint32_t writerTimestamp_{};
    uint32_t observedFaults_{}, observedSequence_{};
    bool outputReady_{};
    uint32_t outputFaults_{};
    MotorControlModeType outputMode_{RELAX_MODE};
    bool PublishFeedback(const uint8_t *data, uint32_t nowMs, bool valid);
    bool OutputSafe(uint32_t nowMs) const;
};

#endif // DJIMOTOR_HPP
