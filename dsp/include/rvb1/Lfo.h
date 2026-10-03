#pragma once

#include <cmath>
#include <cstdint>

namespace rvb1
{

// Modulation source for the delay lines: a random walk.
//
// REVERB_SPEC.md 3.1 asks for "mutually incommensurate rates and randomised
// initial phases", the aim being to stop the lines phase-locking into audible
// tonal pumping. A bank of sines does that by careful choice of rates. This
// removes the failure mode instead of avoiding it — the phase increment is
// re-drawn at random intervals, so there is no period to lock onto at all. The
// idea is NHHall's (REFERENCE_NOTES.md 8.1); the implementation is ours.
//
// A sine shape shipped alongside this one through the Phase 2 gate so the two
// could be compared by ear. The verdict was immediate once the comparison was
// made with modulation actually engaged: the sine reads as vibrato, because a
// periodic modulation is exactly what a listener recognises as one. Deleted
// rather than left as dead code.
//
// Everything is seeded and deterministic. Two instances of the plugin must
// sound identical and a session must reproduce, which rules out std::rand in a
// constructor (REFERENCE_NOTES.md 3).
class Lfo
{
public:
    void prepare (double sampleRateHz, std::uint32_t seed) noexcept
    {
        sampleRate = sampleRateHz;
        rng        = seed | 1u;

        // Randomised initial phase, deterministically derived from the seed.
        phase           = (double) nextUnit() * 6.283185307179586;
        countdown       = 0;
        increment       = 0.0;
        targetIncrement = 0.0;
    }

    void setRateHz (float hz) noexcept
    {
        rateHz = hz < 0.001f ? 0.001f : hz;
    }

    // Returns -1 .. 1.
    float next() noexcept
    {
        // Hold a direction for a random stretch, then choose another. The
        // stretch scales with 1/rate so the control still means "speed", even
        // though nothing here repeats.
        if (--countdown <= 0)
        {
            const double meanSamples = sampleRate / (double) rateHz;
            countdown = (int) (meanSamples * (0.25 + 0.75 * (double) nextUnit())) + 1;
            targetIncrement = ((double) nextUnit() - 0.5) * 4.0 * (double) rateHz / sampleRate;
        }

        // Glide to the new direction rather than snapping to it.
        //
        // The first version assigned the new increment directly. The phase
        // stayed continuous but its *velocity* jumped, and a velocity
        // discontinuity in a delay time is a click. With sixteen of these
        // running it read as a fine crackle — inaudible while the input was
        // playing, obvious the moment it stopped.
        increment += (targetIncrement - increment) * kDirectionGlide;

        phase += increment;

        // Wrapped, so the phase cannot grow until sin() starts losing
        // resolution in a long session.
        if (phase >  6.283185307179586) phase -= 6.283185307179586;
        if (phase < -6.283185307179586) phase += 6.283185307179586;

        return (float) std::sin (phase);
    }

private:
    // Small deterministic LCG. Not for cryptography, only for reproducible
    // spread — and reproducibility is the point.
    float nextUnit() noexcept
    {
        rng = rng * 1664525u + 1013904223u;
        return (float) (rng >> 8) / 16777216.0f;      // 0 .. 1
    }

    // How quickly a new direction is taken up. Slow enough that the change is
    // a curve rather than a corner, fast enough that the walk still wanders.
    static constexpr double kDirectionGlide = 0.0015;

    double sampleRate      = 48000.0;
    double phase           = 0.0;
    double increment       = 0.0;
    double targetIncrement = 0.0;
    float  rateHz        = 0.4f;
    int    countdown     = 0;
    std::uint32_t rng    = 1u;
};

} // namespace rvb1
