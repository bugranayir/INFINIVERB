#pragma once

#include <cmath>

namespace rvb1
{

// Keeps the global feedback loop from running away, so the loop can be opened
// far wider than stability alone would allow.
//
// Without this the loop gain has to stay below the network's own resonant gain,
// which left Feedback doing almost nothing — measured at the Phase 3 gate, and
// the author's verdict was that turning it fully up was nearly indistinguishable
// from leaving it down. Diffusion cannot help: allpasses preserve magnitude.
//
// **It applies a flat gain and nothing else.** No filter, no waveshaping, no
// frequency dependence — colouring the sound is not merely avoided here, it is
// unavailable. The only artefact it can produce is slow level movement, and the
// time constants are chosen so that even that stays below notice: it governs,
// it does not compress.
class LoopGovernor
{
public:
    void prepare (double sampleRate) noexcept
    {
        // Deliberately far slower than the loop it governs.
        //
        // The first attempt used tenths of a second, which is the same order as
        // the loop's own period — so it chased itself: cut the gain, wait a
        // loop, see the level fall, release, watch it climb again. Measured, the
        // gain wandered between 0.52 and 0.96 over seconds, which is six
        // decibels of audible pumping.
        //
        // At these constants it cannot chase. It measures the trend rather than
        // the moment and converges on the gain that holds the level. Release is
        // far slower than attack so that once it has settled it stays settled.
        attackCoeff  = coeffFor (sampleRate, 1.0);
        releaseCoeff = coeffFor (sampleRate, 12.0);
        gainCoeff    = coeffFor (sampleRate, 4.0);
        reset();
    }

    void reset() noexcept
    {
        envelope = 0.0f;
        gain     = 1.0f;
    }

    // Takes the circulating signal, returns the gain the loop should use.
    float process (float circulating) noexcept
    {
        // Energy, not magnitude.
        //
        // Following mean magnitude let a peaky tail sit far above the ceiling in
        // RMS terms — measured at 2.3 where the ceiling was 0.35, because a
        // resonant loop with no modulation has a high crest factor and the mean
        // says nothing about it. RMS is what "level" means here.
        const float squared = circulating * circulating;
        const float coeff = squared > envelope ? attackCoeff : releaseCoeff;
        envelope += (squared - envelope) * coeff;

        const float level = std::sqrt (envelope);

        // Above the ceiling, hold at the ceiling. Below it, do nothing at all —
        // so at ordinary Feedback settings the loop is exactly as linear as it
        // was before this existed.
        const float target = level > kCeiling ? kCeiling / level : 1.0f;

        gain += (target - gain) * gainCoeff;
        return gain;
    }

    float getGain() const noexcept { return gain; }

private:
    // The level the circulating signal is allowed to settle at.
    //
    // Set by measurement rather than by taste: at unity a fully open loop
    // sustained with peaks around six times the input, which is far too hot to
    // be useful. At this value the sustained level sits near the level that
    // drove it, and Mix and Output Gain do the rest.
    static constexpr float kCeiling = 0.35f;

    static float coeffFor (double sampleRate, double seconds) noexcept
    {
        return (float) (1.0 - std::exp (-1.0 / (seconds * sampleRate)));
    }

    float envelope     = 0.0f;
    float gain         = 1.0f;
    float attackCoeff  = 0.0f;
    float releaseCoeff = 0.0f;
    float gainCoeff    = 0.0f;
};

} // namespace rvb1
