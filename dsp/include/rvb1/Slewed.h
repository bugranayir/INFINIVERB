#pragma once

#include <cmath>

namespace rvb1
{

// A value that moves toward its target at a bounded rate.
//
// Used for delay lengths, where the rate of change *is* the pitch shift: a
// delay growing by r samples per sample plays back at (1 - r) times speed. So
// capping the rate caps the Doppler, which is the difference between a tape
// effect and an unusable dive.
//
// A plain time-constant smoother cannot do this. Its rate depends on how far it
// has to travel, so a large Size jump produces a large excursion no matter how
// long the smoothing is set to. The cap has to be on the rate itself.
class Slewed
{
public:
    void prepare (double sampleRate, float maxStepPerSample) noexcept
    {
        maxStep = maxStepPerSample;

        // A one-pole on the output, a couple of milliseconds long.
        //
        // Not decoration. A bare rate limiter moves at full speed until it
        // reaches the target and then stops dead, and since the target only
        // updates once per block it does that repeatedly — velocity alternating
        // between the cap and zero. A velocity discontinuity in a delay time is
        // a click, which is the same fault the modulation had. Rounding the
        // corners costs a millisecond of lag and removes it.
        smoothing = (float) std::exp (-1.0 / (0.002 * sampleRate));
    }

    void snap (float value) noexcept { current = target = smoothed = value; }
    void setTarget (float value) noexcept { target = value; }

    float next() noexcept
    {
        const float remaining = target - current;

        if (remaining > maxStep)        current += maxStep;
        else if (remaining < -maxStep)  current -= maxStep;
        else                            current = target;

        smoothed = current + smoothing * (smoothed - current);
        return smoothed;
    }

    float getCurrent() const noexcept { return smoothed; }

private:
    float current   = 0.0f;
    float target    = 0.0f;
    float smoothed  = 0.0f;
    float maxStep   = 1.0f;
    float smoothing = 0.0f;
};

} // namespace rvb1
