#pragma once

#include <array>
#include <cmath>

namespace rvb1
{

// Resampling by powers of two, so the reverb can run at 44.1 or 48 kHz inside
// a session at 88.2 kHz and above (decided 2026-10-03).
//
// Why: the presets were voiced at 44.1 and 48, and the structure is not rate
// independent — fractional delay reads and the one-pole shelves lose a little
// more top end per pass at 48 kHz than at 96, and over hundreds of passes that
// added up to a tail 1-1.5 dB brighter at 96 kHz and 2 dB heavier in the bass
// at 192. Running the reverb at the rate it was voiced at removes the question
// rather than compensating for it, and halves its cost at the higher rates.
//
// The filter is a linear-phase windowed-sinc halfband: every other tap is
// zero and the centre is one half, so it costs half its length. 71 taps with a
// Kaiser window (beta 9): flat to 20 kHz and about 90 dB down from 28 kHz at a
// 96 kHz host rate. Its delay, 35 host samples each way, is in the wet path
// only, where it amounts to under a millisecond of extra predelay.
class HalfbandFilter
{
public:
    static constexpr int kTaps   = 71;
    static constexpr int kCentre = kTaps / 2;
    static constexpr int kSide   = (kCentre + 1) / 2;   // the non-zero taps either side

    HalfbandFilter() noexcept
    {
        constexpr double pi = 3.14159265358979323846;
        constexpr double beta = 9.0;

        auto besselI0 = [] (double x)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 50; ++k)
            {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
            }
            return sum;
        };

        double total = 0.0;
        for (int k = 0; k < kSide; ++k)
        {
            const int offset = 2 * k + 1;
            const double r = (double) offset / (double) kCentre;
            const double window = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / besselI0 (beta);
            const double ideal = ((k % 2 == 0) ? 1.0 : -1.0) / (pi * offset);   // 0.5 sinc (offset / 2)
            side[(size_t) k] = (float) (ideal * window);
            total += 2.0 * ideal * window;
        }

        // Unity at DC and a true zero at Nyquist: the side taps sum to one
        // half, the centre is the other half.
        for (auto& c : side)
            c = (float) ((double) c * 0.5 / total);

        reset();
    }

    void reset() noexcept
    {
        history.fill (0.0f);
        pos = 0;
    }

    // Adds a sample to the history without computing an output.
    void push (float x) noexcept
    {
        pos = (pos == 0 ? kTaps : pos) - 1;
        history[(size_t) pos] = history[(size_t) (pos + kTaps)] = x;
    }

    float process (float x) noexcept
    {
        push (x);
        const float* h = history.data() + pos;    // h[0] is the newest sample

        float acc = 0.5f * h[kCentre];
        for (int k = 0; k < kSide; ++k)
            acc += side[(size_t) k] * (h[kCentre - (2 * k + 1)] + h[kCentre + (2 * k + 1)]);
        return acc;
    }

private:
    std::array<float, kSide> side {};
    std::array<float, 2 * kTaps> history {};     // written twice, so a read never wraps
    int pos = 0;
};

// Host rate to internal rate and back, by 1, 2, 4 or 8, as a cascade of
// halfband stages. One channel down (the reverb's input is mono), two up.
class RateConverter
{
public:
    static constexpr int kMaxStages = 3;

    // The factor that brings a host rate to 44.1 or 48 kHz.
    static int factorFor (double hostRate) noexcept
    {
        int f = 1;
        while (hostRate / f >= 80000.0 && f < (1 << kMaxStages))
            f *= 2;
        return f;
    }

    void prepare (int factor) noexcept
    {
        stages = 0;
        while ((1 << stages) < factor && stages < kMaxStages)
            ++stages;
        reset();
    }

    int getFactor() const noexcept { return 1 << stages; }

    void reset() noexcept
    {
        for (auto& d : down) d.filter.reset(), d.waiting = false;
        for (auto& ch : up)
            for (auto& f : ch) f.reset();
    }

    // One host-rate sample in; true when an internal-rate sample is ready.
    bool decimate (float x, float& out) noexcept
    {
        float v = x;
        for (int s = 0; s < stages; ++s)
        {
            auto& d = down[(size_t) s];
            if (! d.waiting)
            {
                d.filter.push (v);
                d.waiting = true;
                return false;
            }
            v = d.filter.process (v);
            d.waiting = false;
        }
        out = v;
        return true;
    }

    // One internal-rate sample in; getFactor() host-rate samples out.
    void interpolate (int channel, float x, float* out) noexcept
    {
        std::array<float, 1 << kMaxStages> a {}, b {};
        a[0] = x;
        int n = 1;
        for (int s = stages - 1; s >= 0; --s)
        {
            auto& f = up[(size_t) channel][(size_t) s];
            for (int i = 0; i < n; ++i)
            {
                b[(size_t) (2 * i)]     = 2.0f * f.process (a[(size_t) i]);
                b[(size_t) (2 * i + 1)] = 2.0f * f.process (0.0f);
            }
            a = b;
            n *= 2;
        }
        for (int i = 0; i < n; ++i)
            out[i] = a[(size_t) i];
    }

private:
    struct Down { HalfbandFilter filter; bool waiting = false; };
    std::array<Down, kMaxStages> down;
    std::array<std::array<HalfbandFilter, kMaxStages>, 2> up;
    int stages = 0;
};

} // namespace rvb1
