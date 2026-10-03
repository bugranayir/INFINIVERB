#pragma once

#include "Smoothed.h"

namespace rvb1
{

// A smoothed gain applied across a block. Used for Input Gain, and small enough
// to be worth having as its own thing rather than duplicating the smoothing
// logic wherever a gain is needed.
//
// Unity is exact: a gain of 0 dB multiplies by literally 1.0f rather than by
// pow(10, 0), so a transparent setting stays bit-transparent.
class GainStage
{
public:
    void prepare (double sampleRate, float smoothingSeconds = 0.03f) noexcept
    {
        gain.prepare (sampleRate, smoothingSeconds);
        gain.snap (gainFor (pendingDb));
    }

    void setDecibels (float db) noexcept
    {
        pendingDb = db;
        gain.setTarget (gainFor (db));
    }

    void snapToTarget() noexcept { gain.snap (gainFor (pendingDb)); }

    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        // Skip the work entirely when there is nothing to do. This also keeps
        // the block bit-transparent at 0 dB rather than merely close to it.
        if (! gain.isSmoothing() && gain.getTarget() == 1.0f)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            const float g = gain.next();

            for (int ch = 0; ch < numChannels; ++ch)
                channels[ch][i] *= g;
        }
    }

private:
    static float gainFor (float db) noexcept
    {
        return db == 0.0f ? 1.0f : std::pow (10.0f, db * 0.05f);
    }

    Smoothed gain;
    float pendingDb = 0.0f;
};

} // namespace rvb1
