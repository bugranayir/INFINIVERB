#include "rvb1/DelayNetwork.h"

#include <algorithm>
#include <cmath>

namespace rvb1
{

namespace
{
    // From the point where the author stopped hearing metallic ringing up to a
    // longest line of about a second (decisions 2.6, 2.7).
    //
    // The floor was 16.5 ms, which kept a metallic small-Size region on purpose.
    // At the Phase 3 check it read as a defect, and below what used to be
    // Size 25 the ringing started to bother the author — so that point is now
    // Size 0. The shortest line at the floor is about 25 ms, the longest 61 ms.
    constexpr double kMinSizeSeconds = 0.0424;
    constexpr double kMaxSizeSeconds = 0.717;

    // Headroom over the longest line for modulation and for the interpolator's
    // kernel. Allocated once, in prepare.
    constexpr double kBufferSeconds  = 1.25;

    // Long enough that a modulation depth never steps, short enough that the
    // control still feels immediate.
    constexpr float kDelayGlideSeconds = 0.05f;

    // The pitch limit, expressed as the largest the delay may change per
    // sample. A delay growing by r samples per sample plays at (1 - r) speed,
    // so this caps the downward shift at 12*log2(1/(1-r)) and the upward shift
    // at 12*log2(1+r).
    //
    // 0.25 gave about five semitones, which the author found closer but still
    // too much. 0.12 gives about two — audible as movement rather than as a
    // dive. The cost is at the other end: a full-range Size jump now takes
    // around eight seconds to arrive, because the pitch and the settling time
    // are the same number seen twice.
    constexpr float kMaxDelayStepPerSample = 0.12f;

    constexpr double kMaxModSeconds  = 0.004;   // a few ms, per REVERB_SPEC.md 3.1

    // Fixed corners: 500 Hz and 4.2 kHz rather than 350 Hz and 2 kHz, because
    // that is what the author preferred by ear in Phase 0 test B6 (decision 4.6).
    // In-line diffusion stages are short — they are there for density inside
    // the loop, not to pre-smear the input the way the series block does.
    constexpr double kMaxLineStageSeconds = 0.06;

    constexpr double kLowCornerHz  = 500.0;
    constexpr double kHighCornerHz = 4200.0;

    // Eight primes. Their ratios are incommensurate by construction.
    constexpr int kPrimes[DelayNetwork::kNumLines] = { 23, 29, 31, 37, 41, 43, 47, 53 };

    float lcg (std::uint32_t& state) noexcept
    {
        state = state * 1664525u + 1013904223u;
        return (float) (state >> 8) / 16777216.0f;
    }
}

void DelayNetwork::prepare (double sampleRateHz, std::uint32_t seed)
{
    sampleRate = sampleRateHz;

    float mean = 0.0f;
    for (int p : kPrimes)
        mean += (float) p;
    mean /= (float) kNumLines;

    std::uint32_t rng = seed | 1u;

    for (int i = 0; i < kNumLines; ++i)
    {
        // A few percent of jitter on each length, derived from the seed. Two
        // networks built from different seeds then have different comb
        // structures, which is what makes the stereo pair wide
        // (REFERENCE_NOTES.md 2.6). Small enough not to disturb the primes'
        // incommensurability.
        const float jitter = 0.97f + 0.06f * lcg (rng);
        ratios[i] = (float) kPrimes[i] / mean * jitter;

        // Modulation depth and rate spread per line, so the lines do not all
        // breathe together (REFERENCE_NOTES.md 2.3).
        modScale[i] = 0.7f + 0.3f * lcg (rng);

        // Feeding the same signal into all eight lines with the same sign makes
        // the first pass arrive as one coherent burst. Randomising the polarity
        // scatters it instead (REFERENCE_NOTES.md 2.3).
        inputSign[i] = lcg (rng) < 0.5f ? -1.0f : 1.0f;

        lines[i].prepare (sampleRateHz, kBufferSeconds);
        delaySlewed[i].prepare (sampleRateHz, kMaxDelayStepPerSample);
        modSmoothed[i].prepare (sampleRateHz, kDelayGlideSeconds);
        feedbackSmoothed[i].prepare (sampleRateHz, kDelayGlideSeconds);
        lfos[i].prepare (sampleRateHz, seed * 2654435761u + (std::uint32_t) (i + 1) * 40503u);
        dcBlockers[i].prepare (sampleRateHz);
        lowShelves[i].prepare (sampleRateHz, kLowCornerHz, OnePoleShelf::Type::Low);
        highShelves[i].prepare (sampleRateHz, kHighCornerHz, OnePoleShelf::Type::High);

        // A different seed per line, so the eight lines do not all diffuse
        // through the same pattern.
        lineDiffusers[i].prepare (sampleRateHz, kMaxLineStageSeconds,
                                  seed ^ ((std::uint32_t) (i + 1) * 0x9e3779b9u));

        configureLineDiffuser (i, kLineStages, kLineCoefficient, kLineBaseSeconds);
        lineDiffusers[i].snapToTargets();
    }

    recomputeDelays();
    recomputeFeedback();

    // Start where we are, never at zero — the Phase 0 lesson
    // (REFERENCE_NOTES.md 10).
    for (int i = 0; i < kNumLines; ++i)
    {
        delaySlewed[i].snap (delaySamples[i]);
        modSmoothed[i].snap (modSamples[i]);
        feedbackSmoothed[i].snap (feedbackGain[i]);
    }
}

void DelayNetwork::reset() noexcept
{
    for (int i = 0; i < kNumLines; ++i)
    {
        lines[i].reset();
        dcBlockers[i].reset();
        lowShelves[i].reset();
        highShelves[i].reset();
        lineDiffusers[i].reset();
    }
}

void DelayNetwork::setSize (float normalised) noexcept
{
    sizeNorm = std::clamp (normalised, 0.0f, 1.0f);
    recomputeDelays();
    recomputeFeedback();   // the T60 gain depends on how long a pass takes
}

void DelayNetwork::setDecaySeconds (float t60Seconds) noexcept
{
    // Clamped before it is used as a divisor. This is the Phase 0 failure
    // exactly: a decay time and a delay length ramping toward zero together is
    // what made the gain calculation degenerate and filled the Phase 0 rig
    // with NaN (REFERENCE_NOTES.md 10).
    decaySecs = std::max (0.05f, t60Seconds);
    recomputeFeedback();
}

void DelayNetwork::setModDepth (float normalised) noexcept
{
    depthNorm = std::clamp (normalised, 0.0f, 1.0f);
    recomputeDelays();
    updateLineModulation();
}

void DelayNetwork::setModRateHz (float hz) noexcept
{
    rateHz = std::max (0.001f, hz);

    for (int i = 0; i < kNumLines; ++i)
        lfos[i].setRateHz (rateHz * modScale[i]);

    updateLineModulation();
}

// The in-line diffusers are modulated too, so Mod Depth and Mod Rate have to
// reach them directly. They used to reach them only through setLineDiffusion,
// which the plugin happens to call every block — so the plugin was right and
// the core on its own was not: a test asking for Mod Depth 0 still got
// modulated diffusers, and because the random walk takes a different path at
// every sample rate, the decay time drifted by sample rate too.
void DelayNetwork::updateLineModulation() noexcept
{
    for (int i = 0; i < kNumLines; ++i)
        lineDiffusers[i].setModulation (depthNorm * 0.5f, rateHz * modScale[i]);
}

void DelayNetwork::setLineDiffusion (int stages, float coefficient, float baseSeconds) noexcept
{
    for (int i = 0; i < kNumLines; ++i)
        configureLineDiffuser (i, stages, coefficient, baseSeconds);

    recomputeFeedback();   // the loop just got longer or shorter
}

void DelayNetwork::configureLineDiffuser (int line, int stages, float coefficient, float baseSeconds) noexcept
{
    auto& d = lineDiffusers[line];
    d.setStageCount (stages);
    d.setCoefficient (coefficient);
    d.setBaseDelaySeconds (baseSeconds);
    d.setModulation (depthNorm * 0.5f, rateHz * modScale[line]);
}

void DelayNetwork::snapToTargets() noexcept
{
    for (int i = 0; i < kNumLines; ++i)
    {
        delaySlewed[i].snap (delaySamples[i]);
        modSmoothed[i].snap (modSamples[i]);
        feedbackSmoothed[i].snap (feedbackGain[i]);
        lineDiffusers[i].snapToTargets();
        lowShelves[i].snapToTarget();
        highShelves[i].snapToTarget();
    }
}

void DelayNetwork::setBandGains (float lowLinear, float highLinear) noexcept
{
    lowGain  = std::max (0.0f, lowLinear);
    highGain = std::max (0.0f, highLinear);
    applyBandGains();
}

// A shelf above unity multiplies the loop gain inside its own band, so a boost
// has to fit in whatever headroom the line's decay gain leaves. The limit goes
// on the *shelf*, not on the base gain — an earlier version divided the base
// gain by the boost instead, which quietly shortened every band's decay the
// moment either shelf went above flat. The measurement caught it: a 60 second
// tail was arriving at silence in under twenty.
//
// The consequence is physically honest. On a very long tail there is no
// headroom left, so a boost does nothing: you cannot make a band ring longer
// than a decay that is already nearly endless. Cuts always work.
//
// The headroom is a band decay of at most kMaxBandStretch times the decay,
// not a per-pass gain of 0.999 (decided 2026-10-03, from the release checks).
// The old ceiling let any boost past the headroom sit at 0.999 per pass: at a
// three-second decay the boosted band rang for minutes, the host could not be
// told how long the tail was, and sitting that close to unity made the decay
// hang on details as small as the sample rate's effect on the shelves — the
// same setting rang twice as long at 192 kHz as at 48.
void DelayNetwork::applyBandGains() noexcept
{
    for (int i = 0; i < kNumLines; ++i)
    {
        const float g = std::max (1.0e-6f, feedbackGain[i]);
        const float ceiling = std::min (0.999f, std::pow (g, 1.0f / kMaxBandStretch));
        const float headroom = ceiling / g;

        lowShelves[i].setGain (std::min (lowGain, headroom));
        highShelves[i].setGain (std::min (highGain, headroom));
    }
}

// Per-pass gain g gives a decay proportional to 1 / ln g; a band boosted by b
// (within its headroom) decays as 1 / ln (g b).
float DelayNetwork::getBandStretch() const noexcept
{
    const float boost = std::max (lowGain, highGain);
    if (boost <= 1.0f)
        return 1.0f;

    float stretch = 1.0f;
    for (int i = 0; i < kNumLines; ++i)
    {
        const float g = std::clamp (feedbackGain[i], 1.0e-6f, 0.999f);
        const float ceiling = std::min (0.999f, std::pow (g, 1.0f / kMaxBandStretch));
        const float boosted = std::min (g * boost, ceiling);
        stretch = std::max (stretch, std::log (g) / std::log (boosted));
    }
    return stretch;
}

void DelayNetwork::recomputeDelays() noexcept
{
    const double sizeSeconds = sizeSecondsFor (sizeNorm);

    for (int i = 0; i < kNumLines; ++i)
    {
        delaySamples[i] = (float) (sizeSeconds * (double) ratios[i] * sampleRate);

        // Depth scales with the line's own length. REVERB_SPEC.md 3.1 is
        // specific about this: a fixed depth that is gentle on a long line is a
        // watery artefact on a short one.
        const float wanted = (float) (kMaxModSeconds * sampleRate) * depthNorm * modScale[i];
        modSamples[i] = std::min (wanted, delaySamples[i] * 0.4f);

        delaySlewed[i].setTarget (delaySamples[i]);
        modSmoothed[i].setTarget (modSamples[i]);
    }
}

void DelayNetwork::recomputeFeedback() noexcept
{
    const double t60 = (double) decaySecs;

    for (int i = 0; i < kNumLines; ++i)
    {
        // One circulation is the delay line *plus* the diffuser in the same
        // loop. Leaving the diffuser out made the tail longer than asked for —
        // caught by the T60 test the moment in-line diffusion was switched on
        // by default.
        const double passSeconds =
            ((double) delaySamples[i] + (double) lineDiffusers[i].getTotalDelaySamples()) / sampleRate;

        // -60 dB over T60, so every line reaches silence together despite
        // having a different length. Without this the tail develops a lopsided
        // spectrum as the short lines die first (REFERENCE_NOTES.md 2.1).
        const double decibelsPerPass = -60.0 * passSeconds / t60;
        const double gain = std::pow (10.0, decibelsPerPass / 20.0);

        // The ceiling is applied to the *computed* value, never assumed from
        // sane inputs (decision 4.10, and REFERENCE_NOTES.md 10's second point).
        feedbackGain[i] = (float) std::min (0.999, gain);
        feedbackSmoothed[i].setTarget (feedbackGain[i]);
    }

    // The shelves' headroom depends on the gain just computed.
    applyBandGains();
}

float DelayNetwork::process (float input) noexcept
{
    float taps[kNumLines];

    for (int i = 0; i < kNumLines; ++i)
    {
        const float base  = delaySlewed[i].next();
        const float depth = modSmoothed[i].next();

        taps[i] = lines[i].read (base + depth * lfos[i].next());
    }

    // Each line closes its own loop — nothing crosses between them.
    for (int i = 0; i < kNumLines; ++i)
    {
        // The shelves are part of the loop's attenuation, so they belong with
        // the gain. The diffuser goes before them: it scatters what
        // circulates, the shelves colour it.
        float v = taps[i] * feedbackSmoothed[i].next();
        v = lineDiffusers[i].process (v);
        v = lowShelves[i].process (v);
        v = highShelves[i].process (v);

        const float toWrite = dcBlockers[i].process (input * inputSign[i] + v);
        lines[i].write (softClip (toWrite));
    }

    float sum = 0.0f;
    for (int i = 0; i < kNumLines; ++i)
        sum += taps[i];

    // Energy-preserving for uncorrelated lines (REFERENCE_NOTES.md 2.2).
    return sum * 0.35355339f;   // 1 / sqrt(8)
}

float DelayNetwork::getMaxLineGain() const noexcept
{
    float g = 0.0f;
    for (int i = 0; i < kNumLines; ++i)
        g = std::max (g, feedbackGain[i]);

    return g;
}

// The per-line gain is computed from the nominal time one circulation takes.
// But an allpass delays some frequencies far longer than its nominal length —
// up to (1 + g) / (1 - g) times, nine at 0.80 — and the modes that circulate
// slowest are the ones still there at the end. So the more of each loop is
// in-line diffuser rather than plain delay, the further the last tens of dB run
// past the nominal decay; modulation smears those modes and pulls it back.
//
// Fitted to renders, timed to -60 dB below the peak: about 2.4 times at the
// smallest Size with no modulation, about 1.3 times at the largest, and less
// with modulation. The early decay — what a T60 fit reads — overshoots far
// less, which is why Phase 3 measured only 5-20 %.
float DelayNetwork::getDecayOvershoot() const noexcept
{
    double share = 0.0;
    for (int i = 0; i < kNumLines; ++i)
    {
        const double d = (double) lineDiffusers[i].getTotalDelaySamples();
        share += d / std::max (1.0, (double) delaySamples[i] + d);
    }
    share /= (double) kNumLines;

    const double modulation = 1.0 / (1.0 + 2.5 * (double) depthNorm);
    return (float) (1.0 + 1.8 * share * modulation);
}

// Exponential, so the knob's travel is perceptually even (REVERB_SPEC.md 5.5).
double DelayNetwork::sizeSecondsFor (float normalised) noexcept
{
    const double n = std::isfinite (normalised) ? std::clamp ((double) normalised, 0.0, 1.0) : 0.5;
    return kMinSizeSeconds * std::pow (kMaxSizeSeconds / kMinSizeSeconds, n);
}

float DelayNetwork::getLongestDelaySeconds() const noexcept
{
    float longest = 0.0f;
    for (int i = 0; i < kNumLines; ++i)
        longest = std::max (longest, delaySamples[i]);

    return longest / (float) sampleRate;
}

} // namespace rvb1
