#pragma once

#include "DelayLine.h"
#include "Lfo.h"
#include "Slewed.h"
#include "Smoothed.h"

namespace rvb1
{

// A modulated Schroeder allpass.
//
// The whole character of this family comes from stacking these. An allpass
// passes every frequency at unit gain and only rearranges phase, so a chain of
// them multiplies echo density without colouring the spectrum — REVERB_SPEC.md
// 2.3: density grows with the *power* of the chain length, which is how two
// dozen stages make an enormous tail out of very little delay memory.
//
// The coefficient is the control that matters, and 2.3 tabulates what it does:
//
//     ~0.5        slow, roughly symmetric fade in and out — inherently
//                 reverse-sounding, and the seed of Gravity's negative half
//     ~0.7        long decay, soft attack
//     ~0.82-0.91  sharper attack with an enormous tail
//
// Gravity drives the coefficient of the series chain (Phase 4.2), so it moves
// while audio runs. It is therefore not stored here: the Diffuser smooths one
// value for the whole chain and hands it to every stage, sample by sample.
class Allpass
{
public:
    void prepare (double sampleRate, double maxDelaySeconds, std::uint32_t seed)
    {
        delay.prepare (sampleRate, maxDelaySeconds);
        lfo.prepare (sampleRate, seed);

        // The rate limit here is about avoiding clicks when the length changes,
        // not about capping Doppler — these lengths are set while voicing, not
        // played. It has to be fast enough to actually arrive: at 0.05 samples
        // per sample a stage took two seconds to reach a hundred-millisecond
        // target, so every measurement of stage length was taken before the
        // diffuser had got there, and every one of them came back identical.
        delaySlewed.prepare (sampleRate, 2.0f);
        delaySlewed.snap (delaySamples);

        // A depth that jumps moves the read position in one step, which is a
        // click on anything sustained (measured at the release checks: Mod
        // Depth end to end in one automation step).
        modDepthSmoothed.prepare (sampleRate, 0.03f);
    }

    // Jump to the configured length. Only safe before audio starts.
    void snapToLength() noexcept { delaySlewed.snap (delaySamples); modDepthSmoothed.snap (modDepth); }

    void reset() noexcept { delay.reset(); }

    void setDelaySamples (float samples) noexcept
    {
        delaySamples = samples;
        delaySlewed.setTarget (samples);
    }

    // Depth is given in samples and should already be scaled to this stage's
    // own length — REVERB_SPEC.md 3.1 is specific that a depth which is gentle
    // on a long delay is a watery artefact on a short one.
    void setModulation (float depthSamples, float rateHz) noexcept
    {
        modDepth = depthSamples;
        modDepthSmoothed.setTarget (depthSamples);
        lfo.setRateHz (rateHz);
    }

    float process (float x, float g) noexcept
    {
        const float delayed = delay.read (delaySlewed.next() + modDepthSmoothed.next() * lfo.next());

        const float v = x + g * delayed;
        delay.write (v);

        return delayed - g * v;
    }

private:
    DelayLine delay;
    Lfo       lfo;
    Slewed    delaySlewed;

    float delaySamples = 100.0f;
    float modDepth     = 0.0f;
    Smoothed modDepthSmoothed { 0.0f };
};

} // namespace rvb1
