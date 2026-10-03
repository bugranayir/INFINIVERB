#pragma once

#include <cmath>

namespace rvb1
{

// A peaking biquad, used only at the output.
//
// Deliberately not used inside the feedback path — a resonant filter in a loop
// that can run for a minute would accumulate its own peak until it dominated
// the tail. That is why the in-loop shelves are one-poles (see Shelf.h) and why
// decision 4.7 keeps Resonance out here where it is safe: a resonance inside
// the loop can overload.
class Biquad
{
public:
    void reset() noexcept { x1 = x2 = y1 = y2 = 0.0f; }

    void setPeaking (double sampleRate, double freqHz, double q, double gainDb) noexcept
    {
        const double a  = std::pow (10.0, gainDb / 40.0);
        const double w  = 2.0 * 3.14159265358979323846 * freqHz / sampleRate;
        const double cw = std::cos (w);
        const double alpha = std::sin (w) / (2.0 * std::max (0.05, q));

        const double b0 =  1.0 + alpha * a;
        const double b1 = -2.0 * cw;
        const double b2 =  1.0 - alpha * a;
        const double a0 =  1.0 + alpha / a;
        const double a1 = -2.0 * cw;
        const double a2 =  1.0 - alpha / a;

        c_b0 = (float) (b0 / a0);
        c_b1 = (float) (b1 / a0);
        c_b2 = (float) (b2 / a0);
        c_a1 = (float) (a1 / a0);
        c_a2 = (float) (a2 / a0);
    }

    float process (float x) noexcept
    {
        const float y = c_b0 * x + c_b1 * x1 + c_b2 * x2 - c_a1 * y1 - c_a2 * y2;
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return y;
    }

private:
    float c_b0 = 1.0f, c_b1 = 0.0f, c_b2 = 0.0f, c_a1 = 0.0f, c_a2 = 0.0f;
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
};

} // namespace rvb1
