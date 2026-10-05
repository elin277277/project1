#ifndef RM26_M2006_HPP
#define RM26_M2006_HPP

#include "DJIMotor.hpp"

// M2006 + C610 protocol EXAMPLE only. Other motors/ESCs need explicit protocol
// review, not a gearbox enum. Configuration comes from the parent service's
// app_config; this module never includes service configuration or platform code.
class M2006 final : public DJIMotor
{
public:
    struct Config
    {
        float reduction{};          // 改成实际电机：rotor/output-shaft ratio, must be > 0.
        uint16_t currentLimit{};    // 改成实际电机：C610 counts, 0 by default, maximum 10000.
        uint32_t feedbackTimeoutMs{}; // 改成实际电机：explicit, nonzero timeout < 2^31 ms.
    };
    // No implicit reduction/timeout. Missing/invalid configuration cannot register.
    // Gains default to ZERO, not tuned values. Always starts in RELAX_MODE.
    explicit M2006(Config config);
    ~M2006() override = default;
    using DJIMotor::setOutput;
    using DJIMotor::AliveCheck;
    void setOutput(float dtSeconds, uint32_t nowMs) override;
    MotorStateTypedef AliveCheck(uint32_t nowMs) override;

private:
    MotorControlModeType previousMode_{RELAX_MODE};
};

#endif // RM26_M2006_HPP
