#include "math.hpp"
#include <cmath>

namespace Numeric
{
    float LimitABS(float input, float maxValue)
    {
        // Non-finite values must never reach an actuator conversion.
        if (!std::isfinite(input) || !std::isfinite(maxValue) || maxValue < 0.0f)
            return 0.0f;
        if (input > maxValue)
            return maxValue;
        if (input < -maxValue)
            return -maxValue;
        return input;
    }
}
