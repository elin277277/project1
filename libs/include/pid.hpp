#ifndef RM26_PID_HPP
#define RM26_PID_HPP

#include "math.hpp"
#include <cstdint>

enum PidModeType
{
    PID_POSITION = 0x01,
    PID_DELTA = 0x02,
};

class PID
{
public:
    uint8_t mode;
    float kp, ki, kd;
    float ref{}, fdb{}, err[3]{};
    // iResult is the clamped integral contribution in BOTH modes.
    // In delta mode pResult/dResult are changes in P/D, result is accumulated.
    float pResult{}, iResult{}, dResult{}, result{};
    float maxOut, maxIOut;

    PID(float kp, float ki, float kd, float maxOut, float maxIOut, int mode = PID_POSITION);
    void Tuning(float tuning_kp, float tuning_ki, float tuning_kd);
    // dt in seconds: I += ki*error*dt, D = kd*d(error)/dt.
    // The default preserves legacy per-sample gains; control loops pass real dt.
    // Invalid input/configuration resets history and returns zero.
    bool UpdateResult(float dtSeconds = 1.0f);
    void Clear();

private:
    float previousDerivative_{};
};

#endif // RM26_PID_HPP
