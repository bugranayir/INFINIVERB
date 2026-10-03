#pragma once

#include "Smoothed.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace rvb1
{

// Mechanism (b) of Gravity's negative half (decision 5.6): a causal,
// rising-gain early envelope.
//
// A multitap delay whose taps get louder across a window, feeding the reverb.
// What goes in arrives quietly and builds, then stops at the end of the window,
// and the network's own decay takes over from there. That lopsided shape — a
// long rise and a faster fall — is where the reverse percept lives
// (REVERB_SPEC.md 5.4): the attack edge is what says "reverse", not time
// running backwards. Nothing looks ahead, so there is no latency.
//
// Mechanism (a) gave the *texture* of reverse, with an onset that was immediate
// at every setting. This supplies the *timing*: the longer the window, the
// later the swell peaks; the steeper the ramp, the longer the start stays quiet.
//
// Three design choices, each for a reason:
//
//  - The taps never move. Their positions are fixed at prepare, spread over the
//    longest window this can ever have, and the window is drawn by the gains
//    alone. Moving taps would pitch-shift every one of them whenever Gravity
//    turned.
//
//  - They are packed more densely near the start — positions follow x^1.5 — so
//    a short window near the centre still has a dozen or more taps rather than
//    two or three. Each tap's gain is weighted by the square root of the gap it
//    covers, so the envelope's energy follows the ramp rather than the density
//    of the taps.
//
//  - Random polarity and jittered positions (REFERENCE_NOTES.md 2.3).
//    Evenly spaced taps of one sign are a comb filter.
//
// The taps are normalised to unit energy (decision 4.9). The global feedback
// returns before this block, so every repeat passes through it again, and a
// block with gain above one would pump the loop.
class RisingEnvelope
{
public:
    static constexpr int    kNumTaps          = 256;
    static constexpr double kMaxWindowSeconds = 2.0;   // decision 5.7's cap

    void prepare (double sampleRate, std::uint32_t seed)
    {
        const int needed = (int) std::ceil (kMaxWindowSeconds * sampleRate) + 2;
        int size = 1;
        while (size < needed)
            size <<= 1;

        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        writeIndex = 0;

        std::uint32_t rng = seed | 1u;
        auto rand = [&rng]
        {
            rng = rng * 1664525u + 1013904223u;
            return (float) (rng >> 8) / 16777216.0f;
        };

        for (int k = 0; k < kNumTaps; ++k)
        {
            // Jittered within its own cell, so the taps stay in order.
            const double x = ((double) k + (double) rand()) / (double) kNumTaps;
            const double t = kMaxWindowSeconds * std::pow (x, 1.5);

            tapSeconds[k]  = (float) t;
            tapPosition[k] = std::min (mask - 1, (int) std::lround (t * sampleRate));
            tapSign[k]     = rand() < 0.5f ? -1.0f : 1.0f;

            // The gap this tap covers is proportional to the slope of x^1.5.
            tapDensityWeight[k] = (float) std::sqrt (std::sqrt (std::max (x, 1.0 / kNumTaps)));

            tapGain[k].prepare (sampleRate, kGlideSeconds);
            tapGain[k].snap (0.0f);
        }

        amount.prepare (sampleRate, kGlideSeconds);
        amount.snap (0.0f);

        lastWindow = -1.0f;
        lastSteepness = -1.0f;
        computeGains (kMaxWindowSeconds * 0.1f, 1.0f, true);
    }

    // Every glide to its end, for the start of playback.
    void snapToTargets() noexcept
    {
        amount.snap (amount.getTarget());
        for (auto& g : tapGain)
            g.snap (g.getTarget());
    }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;
    }

    // amount: 0 passes straight through, 1 is the envelope alone.
    // windowSeconds: where the ramp ends. steepness: 1 is a linear ramp; higher
    // keeps the start quiet for longer and packs the energy toward the end.
    void setShape (float newAmount, float windowSeconds, float steepness) noexcept
    {
        amount.setTarget (std::isfinite (newAmount) ? std::clamp (newAmount, 0.0f, 1.0f) : 0.0f);

        const float window = std::isfinite (windowSeconds)
                               ? std::clamp (windowSeconds, 0.005f, (float) kMaxWindowSeconds)
                               : 0.1f;
        const float steep  = std::isfinite (steepness) ? std::clamp (steepness, 0.25f, 8.0f) : 1.0f;

        // Up to 256 powers — once per block, and only when the shape moved.
        if (window != lastWindow || steep != lastSteepness)
            computeGains (window, steep, false);
    }

    float process (float x) noexcept
    {
        buffer[(size_t) writeIndex] = x;

        float out = x;

        // At rest and fully out, the taps cannot contribute — skip all of them.
        // The positive half of Gravity costs nothing and stays bit-exact.
        if (amount.isSmoothing() || amount.getTarget() > 0.0f)
        {
            const float m = amount.next();

            float sum = 0.0f;
            for (int k = 0; k < kNumTaps; ++k)
            {
                const float g = tapGain[k].next();
                if (g != 0.0f)
                    sum += g * tapSign[k] * buffer[(size_t) ((writeIndex - tapPosition[k]) & mask)];
            }

            out = (1.0f - m) * x + m * sum;
        }

        writeIndex = (writeIndex + 1) & mask;
        return out;
    }

private:
    void computeGains (float window, float steep, bool snap) noexcept
    {
        float target[kNumTaps];
        double energy = 0.0;

        for (int k = 0; k < kNumTaps; ++k)
        {
            const float u = tapSeconds[k] / window;
            const float w = u <= 1.0f ? std::pow (u, steep) * tapDensityWeight[k] : 0.0f;
            target[k] = w;
            energy += (double) w * (double) w;
        }

        // A window shorter than every tap but the first has nothing to ramp
        // over; the first tap carries it alone rather than the block going
        // silent.
        if (energy <= 0.0)
        {
            target[0] = 1.0f;
            energy = 1.0;
        }

        const float scale = (float) (1.0 / std::sqrt (energy));

        for (int k = 0; k < kNumTaps; ++k)
        {
            if (snap) tapGain[k].snap (target[k] * scale);
            else      tapGain[k].setTarget (target[k] * scale);
        }

        lastWindow = window;
        lastSteepness = steep;
    }

    // Within REVERB_SPEC.md 5.5's 20-50 ms.
    static constexpr float kGlideSeconds = 0.03f;

    std::vector<float> buffer;
    int mask = 0;
    int writeIndex = 0;

    float    tapSeconds[kNumTaps] {};
    int      tapPosition[kNumTaps] {};
    float    tapSign[kNumTaps] {};
    float    tapDensityWeight[kNumTaps] {};
    Smoothed tapGain[kNumTaps];

    Smoothed amount { 0.0f };
    float lastWindow = -1.0f;
    float lastSteepness = -1.0f;
};

} // namespace rvb1
