#include "rvb1/OutputStage.h"

#include <algorithm>
#include <cmath>

namespace rvb1
{

namespace
{
    constexpr float kHalfPi = 1.57079632679489661923f;

    // Equal-power crossfade, with both endpoints special-cased to exact values.
    //
    // The special cases are not cosmetic. cos(0) and sin(0) are exact, but
    // cos(pi/2) is about 6e-17 rather than 0 — so without this, a plugin at
    // 100 % wet still passes a trace of the dry signal, and Kill never reaches
    // true silence. Inaudible, but it means "fully wet" is not fully wet, and
    // a bit-exact test can never pass. Cheaper to be exact than to explain it.
    inline float dryGainFor (float mixNormalised) noexcept
    {
        if (mixNormalised <= 0.0f) return 1.0f;
        if (mixNormalised >= 1.0f) return 0.0f;
        return std::cos (mixNormalised * kHalfPi);
    }

    inline float wetGainFor (float mixNormalised) noexcept
    {
        if (mixNormalised <= 0.0f) return 0.0f;
        if (mixNormalised >= 1.0f) return 1.0f;
        return std::sin (mixNormalised * kHalfPi);
    }

    inline float gainFromDecibels (float db) noexcept
    {
        return db == 0.0f ? 1.0f                       // exact unity, not 10^0
                          : std::pow (10.0f, db * 0.05f);
    }

    inline float clamp01 (float v) noexcept
    {
        return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    }
}

void OutputStage::prepare (double sampleRate, float smoothingSeconds) noexcept
{
    dryGain .prepare (sampleRate, smoothingSeconds);
    wetGain .prepare (sampleRate, smoothingSeconds);
    trimGain.prepare (sampleRate, smoothingSeconds);

    ceilingReduction = ceilingHeld = 0.0f;
    ceilingHoldLeft = 0;
    ceilingHoldSamples = (int) std::lround (0.02 * sampleRate);
    ceilingAttack  = (float) std::exp (-1.0 / (0.001 * sampleRate));
    ceilingRelease = (float) std::exp (-1.0 / (0.15 * sampleRate));

    // Start at the current settings rather than at zero. Found by the null
    // test: without this the trim gain begins at 0, so the stage fades in from
    // silence every time the host prepares it.
    snapToTargets();
}

void OutputStage::setMixPercent (float percent) noexcept
{
    pendingMixPercent = percent;
    const float m = clamp01 (percent * 0.01f);
    dryGain.setTarget (dryGainFor (m));
    wetGain.setTarget (wetGainFor (m));
}

void OutputStage::setOutputTrimDb (float decibels) noexcept
{
    pendingTrimDb = decibels;
    trimGain.setTarget (gainFromDecibels (decibels));
}

void OutputStage::snapToTargets() noexcept
{
    const float m = clamp01 (pendingMixPercent * 0.01f);
    dryGain .snap (dryGainFor (m));
    wetGain .snap (wetGainFor (m));
    trimGain.snap (gainFromDecibels (pendingTrimDb));
}

void OutputStage::process (float* const* channels,
                           const float* const* wet,
                           int numChannels,
                           int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        const float dry  = dryGain .next();
        const float w    = wetGain .next();
        const float trim = trimGain.next();

        // The wet signal's peak as it will reach the output, both sides linked.
        const float scale = w * trim;
        float peak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            peak = std::max (peak, std::abs (scale * wet[ch][i]));

        // Gain reduction, tracked as 1 - gain: it decays towards zero cleanly,
        // where a gain approaching one would stall a few float steps short.
        // Held for 20 ms after each peak so it does not ripple between the
        // peaks of anything above 25 Hz, then released.
        const float needed = peak > kWetKnee ? 1.0f - kWetKnee / peak : 0.0f;
        if (needed >= ceilingHeld)
        {
            ceilingHeld = needed;
            ceilingHoldLeft = ceilingHoldSamples;
        }
        else if (ceilingHoldLeft > 0)
            --ceilingHoldLeft;
        else
        {
            ceilingHeld = needed + (ceilingHeld - needed) * ceilingRelease;
            if (ceilingHeld < 1.0e-4f && needed == 0.0f)
                ceilingHeld = 0.0f;           // exactly transparent again, not nearly
        }

        ceilingReduction = ceilingHeld > ceilingReduction
                               ? ceilingHeld + (ceilingReduction - ceilingHeld) * ceilingAttack
                               : ceilingHeld;
        const float ceilingGain = 1.0f - ceilingReduction;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float dryIn = channels[ch][i];
            float wetIn = wet[ch][i];

            if (ceilingReduction > 0.0f || needed > 0.0f)
            {
                // What gets ahead of the 1 ms attack is rounded off between
                // the knee and the ceiling rather than clipped.
                float y = scale * wetIn * ceilingGain;
                const float a = std::abs (y);
                if (a > kWetKnee)
                    y = std::copysign (kWetKnee + (kWetCeiling - kWetKnee) * std::tanh ((a - kWetKnee) / (kWetCeiling - kWetKnee)), y);
                wetIn = y / scale;
            }

            channels[ch][i] = (dry * dryIn + w * wetIn) * trim;
        }
    }
}

} // namespace rvb1
