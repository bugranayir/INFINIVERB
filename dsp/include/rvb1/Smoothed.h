#pragma once

#include <algorithm>
#include <cmath>

namespace rvb1
{

// A linear ramp smoother.
//
// REVERB_SPEC.md 5.5 requires smoothing on every audio-rate parameter, so this
// gets used everywhere and is worth being exact about.
//
// Two properties matter enough to state:
//
//   1. At rest it returns the target *exactly*. Not "close to" — the same float.
//      The null test depends on this: at Mix = 0 the wet gain must be a true
//      zero, or a fraction of the wet path leaks into the output forever.
//
//   2. It has no notion of "starting from zero". prepare() keeps the current
//      value. A smoother that ramps up from zero on the first block is what
//      permanently silenced the Phase 0 listening rig by dragging a delay
//      length and a decay time through a degenerate region together
//      (REFERENCE_NOTES.md 10).
class Smoothed
{
public:
    // Construct at a chosen resting value. A gain smoother that defaults to
    // zero would fade in from silence on its first block — the trap from
    // REFERENCE_NOTES.md 10, which this project has now walked into twice.
    explicit Smoothed (float initial = 0.0f) noexcept
        : current (initial), target (initial) {}

    void prepare (double sampleRate, float rampSeconds) noexcept
    {
        rampSamples = std::max (1, (int) std::lround (sampleRate * (double) rampSeconds));
        remaining   = 0;
        current     = target;
    }

    // Jump immediately. Use at prepare time, and for anything that must not
    // glide (a preset recall, a discrete switch).
    void snap (float value) noexcept
    {
        current = target = value;
        remaining = 0;
    }

    void setTarget (float newTarget) noexcept
    {
        if (newTarget == target)
            return;

        target    = newTarget;
        remaining = rampSamples;
        increment = (target - current) / (float) rampSamples;
    }

    float next() noexcept
    {
        if (remaining <= 0)
            return target;              // exact, see note 1 above

        current += increment;

        if (--remaining == 0)
            current = target;           // land on it exactly rather than near it

        return current;
    }

    float getTarget() const noexcept { return target; }
    bool  isSmoothing() const noexcept { return remaining > 0; }

private:
    float current   = 0.0f;
    float target    = 0.0f;
    float increment = 0.0f;
    int   rampSamples = 1;
    int   remaining   = 0;
};

} // namespace rvb1
