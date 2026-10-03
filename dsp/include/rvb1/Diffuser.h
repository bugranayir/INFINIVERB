#pragma once

#include "Allpass.h"
#include "Smoothed.h"

#include <algorithm>
#include <cmath>

namespace rvb1
{

// A series chain of modulated allpasses.
//
// One of these sits on the mono-summed signal before the late network, which is
// the arrangement REVERB_SPEC.md 2.2 describes for this family: a long series
// chain feeding two parallel per-channel paths.
//
// Stage lengths are spread logarithmically rather than evenly. An even spread
// puts the resulting resonances close together; a log spread covers an order of
// magnitude, so they scatter. It is the single cheapest thing that keeps a
// long chain from ringing (REFERENCE_NOTES.md 2.3).
template <int MaxStages>
class Diffuser
{
public:
    void prepare (double sampleRate, double maxStageSeconds, std::uint32_t seed)
    {
        sampleRateHz = sampleRate;

        std::uint32_t rng = seed | 1u;
        auto rand = [&rng]
        {
            rng = rng * 1664525u + 1013904223u;
            return (float) (rng >> 8) / 16777216.0f;
        };

        for (int i = 0; i < MaxStages; ++i)
        {
            // Log-uniform across a 10:1 range.
            spread[i] = std::pow (10.0f, rand()) * 0.1f;

            stages[i].prepare (sampleRate, maxStageSeconds, seed + (std::uint32_t) (i + 1) * 2654435761u);
        }

        applyLengths();

        coefficient.prepare (sampleRate, kCoefficientGlideSeconds);

        // Start at the configured values instead of gliding to them from the
        // constructor's defaults. No audio has been processed yet, so there is
        // nothing to click.
        snapToTargets();
    }

    // Jumps the stage lengths and the coefficient straight to their targets.
    // Only for use before audio runs — mid-stream it is exactly the click the
    // glides exist to avoid.
    void snapToTargets() noexcept
    {
        for (auto& stage : stages)
            stage.snapToLength();

        coefficient.snap (coefficientTarget);
    }

    void reset() noexcept
    {
        for (auto& s : stages)
            s.reset();
    }

    void setStageCount (int count) noexcept
    {
        active = count < 0 ? 0 : (count > MaxStages ? MaxStages : count);
    }

    void setBaseDelaySeconds (float seconds) noexcept
    {
        baseSeconds = seconds;
        applyLengths();
    }

    // Clamped below unity: an allpass at |g| >= 1 is no longer stable.
    void setCoefficient (float g) noexcept
    {
        coefficientTarget = std::isfinite (g) ? std::clamp (g, 0.0f, 0.98f) : 0.7f;
        coefficient.setTarget (coefficientTarget);
    }

    void setModulation (float depthNormalised, float rateHz) noexcept
    {
        modDepth = depthNormalised;
        modRate  = rateHz;
        applyLengths();
    }

    float process (float x) noexcept
    {
        const float g = coefficient.next();

        for (int i = 0; i < active; ++i)
            x = stages[i].process (x, g);

        return x;
    }

    int getStageCount() const noexcept { return active; }

    // Total delay the active stages add. Anything computing a loop time has to
    // include this: the diffuser sits inside the feedback path, so its stages
    // are part of how long one circulation takes.
    float getTotalDelaySamples() const noexcept
    {
        float total = 0.0f;
        for (int i = 0; i < active; ++i)
            total += (float) (baseSeconds * (double) spread[i] * sampleRateHz);

        return total;
    }

private:
    void applyLengths() noexcept
    {
        for (int i = 0; i < MaxStages; ++i)
        {
            const float samples = (float) (baseSeconds * (double) spread[i] * sampleRateHz);
            stages[i].setDelaySamples (samples);

            // Scaled to this stage's own length, capped so a short stage cannot
            // be modulated into a warble.
            stages[i].setModulation (samples * 0.06f * modDepth, modRate);
        }
    }

    // Within REVERB_SPEC.md 5.5's 20-50 ms. An allpass coefficient that jumps
    // rescales everything circulating in the stage at once, which is a step.
    static constexpr float kCoefficientGlideSeconds = 0.03f;

    Allpass  stages[MaxStages];
    float    spread[MaxStages] {};
    Smoothed coefficient { 0.7f };
    float    coefficientTarget = 0.7f;

    double sampleRateHz = 48000.0;
    float  baseSeconds  = 0.05f;
    float  modDepth     = 0.0f;
    float  modRate      = 0.4f;
    int    active       = 8;
};

} // namespace rvb1
