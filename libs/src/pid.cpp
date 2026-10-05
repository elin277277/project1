#include "pid.hpp"
#include <cmath>

PID::PID(float kp, float ki, float kd, float maxOut, float maxIOut, int mode)
    : mode(static_cast<uint8_t>(mode)), kp(kp), ki(ki), kd(kd), maxOut(maxOut), maxIOut(maxIOut)
{
    Clear();
}

void PID::Tuning(float tuning_kp, float tuning_ki, float tuning_kd)
{
    kp = tuning_kp;
    ki = tuning_ki;
    kd = tuning_kd;
    Clear();
}

bool PID::UpdateResult(float dtSeconds)
{
    if ((mode != PID_POSITION && mode != PID_DELTA) ||
        !std::isfinite(dtSeconds) || dtSeconds <= 0.0f ||
        !std::isfinite(kp) || !std::isfinite(ki) || !std::isfinite(kd) ||
        !std::isfinite(ref) || !std::isfinite(fdb) ||
        !std::isfinite(maxOut) || !std::isfinite(maxIOut) || maxOut < 0.0f || maxIOut < 0.0f ||
        !std::isfinite(iResult) || !std::isfinite(result) ||
        !std::isfinite(err[0]) || !std::isfinite(previousDerivative_))
    {
        Clear();
        return false;
    }

    const float error = ref - fdb;
    const float derivative = (error - err[0]) / dtSeconds;
    const float integral = iResult + ki * error * dtSeconds;
    const float p = kp * (mode == PID_POSITION ? error : error - err[0]);
    const float d = kd * (mode == PID_POSITION ? derivative : derivative - previousDerivative_);
    if (!std::isfinite(error) || !std::isfinite(derivative) ||
        !std::isfinite(integral) || !std::isfinite(p) || !std::isfinite(d))
    {
        Clear();
        return false;
    }

    const float nextI = Numeric::LimitABS(integral, maxIOut);
    const float output = mode == PID_POSITION ? p + nextI + d : result + p + (nextI - iResult) + d;
    if (!std::isfinite(output))
    {
        Clear();
        return false;
    }
    err[2] = err[1];
    err[1] = err[0];
    err[0] = error;
    previousDerivative_ = derivative;
    pResult = p;
    iResult = nextI;
    dResult = d;
    result = Numeric::LimitABS(output, maxOut);
    return true;
}

void PID::Clear()
{
    ref = fdb = 0.0f;
    err[0] = err[1] = err[2] = 0.0f;
    pResult = iResult = dResult = result = previousDerivative_ = 0.0f;
}
