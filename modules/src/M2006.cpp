#include "M2006.hpp"
#include <algorithm>
#include <cmath>

M2006::M2006(Config config)
    : DJIMotor(config.reduction, config.currentLimit, config.feedbackTimeoutMs)
{
    // Zero gains and zero default current limit are intentional. No tuning or
    // installed-hardware compatibility is implied by this protocol example.
}

void M2006::setOutput(float dtSeconds, uint32_t nowMs)
{
    if (!RefreshFeedback(nowMs) || !std::isfinite(dtSeconds) || dtSeconds <= 0.0f ||
        dtSeconds > FeedbackTimeoutMs() * 0.001f ||
        (controlMode != SPD_MODE && controlMode != POS_MODE))
    {
        Relax();
        previousMode_ = RELAX_MODE;
        return;
    }
    if (controlMode != previousMode_)
    {
        speedPid.Clear();
        positionPid.Clear();
        previousMode_ = controlMode;
    }
    float speedReference = speedSet;
    if (controlMode == POS_MODE)
    {
        positionPid.ref = positionSet;
        positionPid.fdb = motorFeedback.positionFdb;
        if (!positionPid.UpdateResult(dtSeconds))
        {
            Relax();
            return;
        }
        // Outer output is output-shaft rad/s; inner output is current counts.
        // positionPid.maxOut is the service-configured output speed limit.
        speedReference = positionPid.result;
    }
    speedPid.ref = speedReference;
    speedPid.fdb = motorFeedback.speedFdb;
    if (!speedPid.UpdateResult(dtSeconds) || !std::isfinite(speedPid.result))
    {
        Relax();
        return;
    }
    const float limit = static_cast<float>(CurrentLimit());
    // Clamp before float-to-integer conversion, including when callers changed
    // public PID limits. The constructor limit can only be lowered at runtime.
    currentSet = static_cast<int16_t>(Numeric::LimitABS(speedPid.result, limit));
    CommitOutput();
}

M2006::MotorStateTypedef M2006::AliveCheck(uint32_t nowMs)
{
    RefreshFeedback(nowMs);
    return MotorState;
}
