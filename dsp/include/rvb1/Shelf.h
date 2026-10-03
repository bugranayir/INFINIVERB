#pragma once

#include <cmath>

#include "Smoothed.h"

namespace rvb1
{

// One-pole shelving filters for the feedback path.
//
// One-pole rather than biquad on purpose. These sit inside a loop that can run
// for sixty seconds, and a biquad shelf carries a resonance that would
// accumulate on every pass. A one-pole cannot resonate at all, costs two
// multiplies, and the gentle slope is what damping wants anyway — this is tone
// shaping, not surgery.
//
// Decision 4.6: Lo and Hi set how much of each band is present in the tail.
// A shelf gain below unity inside the loop *is* a shorter decay for that band,
// so the two readings of these controls — "level" and "decay" — are the same
// filter seen from two sides.
class OnePoleShelf
{
public:
    enum class Type { Low, High };

    void prepare (double sampleRate, double cornerHz, Type t) noexcept
    {
        type = t;
        // One-pole lowpass coefficient for the corner frequency.
        coeff = (float) std::exp (-2.0 * 3.14159265358979323846 * cornerHz / sampleRate);

        // The gain glides. It is recomputed on every block — the headroom it is
        // clamped against moves with Size — so an unsmoothed value would step
        // hundreds of times a second while a control is being swept.
        gain.prepare (sampleRate, 0.05f);
        gain.snap (gain.getTarget());
        reset();
    }

    void reset() noexcept { state = 0.0f; }
    void snapToTarget() noexcept { gain.snap (gain.getTarget()); }

    // Linear gain applied to the shelf's band. 1.0 is flat.
    void setGain (float linearGain) noexcept { gain.setTarget (linearGain); }

    float process (float x) noexcept
    {
        const float bandGain = gain.next();

        state = x + coeff * (state - x);       // one-pole lowpass

        return type == Type::Low
                 ? x + (bandGain - 1.0f) * state            // low band scaled
                 : x + (bandGain - 1.0f) * (x - state);     // high band scaled
    }

    // The largest magnitude this filter can apply, which is what the feedback
    // clamp needs to know about (decision 4.10).
    float maxMagnitude() const noexcept
    {
        return gain.getTarget() > 1.0f ? gain.getTarget() : 1.0f;
    }

private:
    Type     type  = Type::Low;
    float    coeff = 0.0f;
    float    state = 0.0f;
    Smoothed gain { 1.0f };   // flat until told otherwise
};

} // namespace rvb1
