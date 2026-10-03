#pragma once

#include <cmath>

namespace rvb1
{

// One-pole DC blocker.
//
// REVERB_SPEC.md 3.1 and 7 require one at every feedback node, and with a 60
// second decay target that is not boilerplate: any DC offset entering a loop
// with near-unity gain accumulates for as long as the tail lasts. A low shelf
// alone is not enough (REFERENCE_NOTES.md 1.3); we follow the spec.
class DcBlocker
{
public:
    void prepare (double sampleRate, double cutoffHz = 20.0) noexcept
    {
        r = (float) std::exp (-2.0 * 3.14159265358979323846 * cutoffHz / sampleRate);
        reset();
    }

    void reset() noexcept { x1 = 0.0f; y1 = 0.0f; }

    float process (float x) noexcept
    {
        const float y = x - x1 + r * y1;
        x1 = x;
        y1 = y;
        return y;
    }

private:
    float r  = 0.9974f;
    float x1 = 0.0f;
    float y1 = 0.0f;
};

// Safety limiter for the feedback path (decision 4.10).
//
// Inaudible in normal use — it is flat up to the threshold and only bends above
// it. It exists so that no combination of parameters, and no parameter sweep,
// can produce a speaker-damaging transient. It is not a character device: the
// signal path stays linear by decision 6.4.
inline float softClip (float x) noexcept
{
    constexpr float threshold = 2.0f;

    if (x > -threshold && x < threshold)
        return x;

    return x > 0.0f ? threshold + std::tanh (x - threshold)
                    : -threshold + std::tanh (x + threshold);
}

} // namespace rvb1
