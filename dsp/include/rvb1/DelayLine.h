#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace rvb1
{

// A fractional delay line with 3rd-order Lagrange interpolation.
//
// This is the brick every other part of the reverb is built from, so it is
// worth being precise about three things.
//
// **Why Lagrange and not linear.** REVERB_SPEC.md 3.1 is explicit: linear
// interpolation is audible on modulated lines. It acts as a low-pass whose
// cutoff moves with the fractional part, so a delay swinging under an LFO gets
// a periodic dullness that reads as a warble (REFERENCE_NOTES.md 3).
//
// **Why the delay is clamped.** The 4-tap kernel reads one sample before and
// two after the nominal position, so a delay shorter than three samples would
// read slots that have not been written yet — returning whatever was last in
// them, which in a feedback loop is the loop's own output from a full buffer
// ago. Clamping before use, rather than trusting the caller, is the Phase 0
// lesson (REFERENCE_NOTES.md 10).
//
// **Why the buffer is a power of two.** Wrapping becomes a mask instead of a
// branch or a modulo, which matters when eight lines each do this per sample.
class DelayLine
{
public:
    // Allocates for the worst case. Called from prepareToPlay, never from the
    // audio thread — REVERB_SPEC.md 7.
    void prepare (double sampleRate, double maxDelaySeconds)
    {
        const int needed = (int) std::ceil (sampleRate * maxDelaySeconds) + kKernelMargin;

        int size = 1;
        while (size < needed)
            size <<= 1;

        buffer.assign ((size_t) size, 0.0f);
        mask       = size - 1;
        writeIndex = 0;
        maxDelay   = (float) (size - kKernelMargin);
    }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;
    }

    void write (float sample) noexcept
    {
        buffer[(size_t) writeIndex] = sample;
        writeIndex = (writeIndex + 1) & mask;
    }

    // Reads `delaySamples` behind the write head. Fractional values are
    // interpolated; integer values are returned exactly, which is what makes
    // the delay testable against a bit-exact expectation.
    float read (float delaySamples) const noexcept
    {
        const float d = clampDelay (delaySamples);

        const int   whole = (int) d;
        const float frac  = d - (float) whole;

        // writeIndex points at the next slot to be written, so the most recent
        // sample sits one behind it.
        const int base = (writeIndex - whole) & mask;

        const float y0 = buffer[(size_t) ((base + 1) & mask)];   // one *newer*
        const float y1 = buffer[(size_t) base];
        const float y2 = buffer[(size_t) ((base - 1) & mask)];
        const float y3 = buffer[(size_t) ((base - 2) & mask)];

        return lagrange3 (y0, y1, y2, y3, frac);
    }

    float getMaxDelaySamples() const noexcept { return maxDelay; }
    int   getBufferSize()      const noexcept { return (int) buffer.size(); }

    // The shortest delay the interpolator can serve honestly.
    static constexpr float minDelaySamples() noexcept { return 3.0f; }

private:
    // One before and two after the nominal read position, plus slack.
    static constexpr int kKernelMargin = 4;

    float clampDelay (float d) const noexcept
    {
        if (! (d >= minDelaySamples()))     // also catches NaN
            return minDelaySamples();

        return d > maxDelay ? maxDelay : d;
    }

    // 4-point, 3rd-order Lagrange. Exact at frac = 0 (returns y1) and at
    // frac = 1 (returns y2), which the tests rely on.
    static float lagrange3 (float y0, float y1, float y2, float y3, float f) noexcept
    {
        const float fm1 = f - 1.0f;
        const float fm2 = f - 2.0f;
        const float fp1 = f + 1.0f;

        const float c0 = -f   * fm1 * fm2 * (1.0f / 6.0f);
        const float c1 =  fp1 * fm1 * fm2 * 0.5f;
        const float c2 = -fp1 * f   * fm2 * 0.5f;
        const float c3 =  fp1 * f   * fm1 * (1.0f / 6.0f);

        return c0 * y0 + c1 * y1 + c2 * y2 + c3 * y3;
    }

    std::vector<float> buffer;
    int   mask       = 0;
    int   writeIndex = 0;
    float maxDelay   = 0.0f;
};

} // namespace rvb1
