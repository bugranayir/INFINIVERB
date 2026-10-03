#include "rvb1/ReverbEngine.h"
#include "rvb1/DcBlocker.h"
#include "rvb1/Parameters.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rvb1
{

namespace
{
    // Decision 3.1: predelay reaches 2 seconds.
    constexpr double kMaxPredelaySeconds = 2.0;

    // The global loop wraps a structure whose own gain rises as the decay
    // lengthens, so a fixed ceiling is not enough. A comb with per-pass gain g
    // has a resonant gain of about 1/(1-g), so scaling the loop by (1-g) makes
    // the product — the total loop gain — independent of the decay time. What
    // is left is this constant, and it simply has to be below one.
    //
    // Confirmed by a grid sweep over decay, size and modulation: at 0.85 the
    // worst case anywhere decays to a tenth; at 1.27 it diverges. The boundary
    // sits exactly where the algebra says it should.
    // Just under unity at the top of the knob. The governor is what keeps this
    // safe; without it this value would run away at any long decay.
    constexpr float kMaxGlobalFeedback = 0.95f;

    // Damping in the loop, so successive repeats get darker. The sweep showed
    // it contributes almost nothing to stability — that is the scaling's job —
    // so it is set for how it sounds rather than for safety.
    // The longest a single diffusion stage can be. Stages are spread
    // logarithmically below this. Raised from 120 ms after the author settled
    // near the old ceiling.
    constexpr double kMaxStageSeconds = 0.26;

    constexpr double kLoopDampingHz   = 3500.0;
    constexpr float  kLoopDampingGain = 0.8f;   // about -1.9 dB per pass

    // The same corners the in-loop shelves use (decision 4.6).
    constexpr double kLowCornerHz  = 500.0;
    constexpr double kHighCornerHz = 4200.0;
}

void ReverbEngine::prepare (double hostRate)
{
    const int factor = RateConverter::factorFor (hostRate);
    converter.prepare (factor);
    pendingL.fill (0.0f);
    pendingR.fill (0.0f);
    pendingIndex = 0;

    const double sr = hostRate / factor;
    sampleRate = sr;

    // Different seeds are the whole of the stereo width: same signal in, two
    // different comb structures out (decision 4.2).
    networkL.prepare (sr, 0x5eed1234u);
    networkR.prepare (sr, 0xa17b93c5u);

    predelay.prepare (sr, kMaxPredelaySeconds);
    predelayFadeLength = std::max (1, (int) std::lround (0.03 * sr));
    setPredelaySeconds (predelaySeconds);
    predelayFrom = predelayTo = predelayTarget;
    predelayFadeLeft = 0;
    globalGain.prepare (sr, 0.03f);
    diffusion.prepare (sr, kMaxStageSeconds, 0x1dea5eedu);
    setDiffusion (kSeriesStages, kSeriesCoefficient, kSeriesBaseSeconds);
    diffusion.snapToTargets();

    envelope.prepare (sr, 0x5e11c0deu);
    referenceSizeNorm = definition (Param::Size).defaultValue * 0.01f;
    applyEnvelope();

    for (auto* f : { &lowResL, &lowResR, &highResL, &highResR })
        f->reset();

    loopDamping.prepare (sr, kLoopDampingHz, OnePoleShelf::Type::High);
    loopDamping.setGain (kLoopDampingGain);
    loopDcBlocker.prepare (sr, 30.0);
    loopGovernor.prepare (sr);
    killGain.prepare (sr, 0.008f);

    setResonance (0.0f, 1.0f, 1.0f);

    feedbackState = 0.0f;
}

// Everything that holds signal, not just the parts that hold a lot of it. The
// filters carry only a sample or two each, but a reset that leaves anything
// behind cannot be tested for exact silence — and exact silence is what the
// test asks for.
void ReverbEngine::reset() noexcept
{
    networkL.reset();
    networkR.reset();
    predelay.reset();
    predelayFrom = predelayTo = predelayTarget;
    predelayFadeLeft = 0;
    diffusion.reset();
    envelope.reset();
    loopGovernor.reset();
    loopDamping.reset();
    loopDcBlocker.reset();

    for (auto* f : { &lowResL, &lowResR, &highResL, &highResR })
        f->reset();

    converter.reset();
    pendingL.fill (0.0f);
    pendingR.fill (0.0f);
    pendingIndex = 0;

    feedbackState = 0.0f;
}

void ReverbEngine::snapToTargets() noexcept
{
    networkL.snapToTargets();
    networkR.snapToTargets();
    diffusion.snapToTargets();
    envelope.snapToTargets();
    globalGain.snap (globalGain.getTarget());
    killGain.snap (killGain.getTarget());
    predelayFrom = predelayTo = predelayTarget;
    predelayFadeLeft = 0;
}

void ReverbEngine::setSize (float n) noexcept
{
    networkL.setSize (n);
    networkR.setSize (n);
    updateGlobalGain();     // the network's gain moved with it

    sizeNorm = n;
    applyEnvelope();        // the reverse window scales with the space
}

void ReverbEngine::setEnvelope (float amount, float windowSeconds, float steepness) noexcept
{
    envelopeAmount    = amount;
    envelopeWindow    = windowSeconds;
    envelopeSteepness = steepness;
    applyEnvelope();
}

void ReverbEngine::applyEnvelope() noexcept
{
    // Relative to the default Size, so a window chosen by ear at the default
    // is heard at exactly that length there.
    const double ratio = DelayNetwork::sizeSecondsFor (sizeNorm)
                       / DelayNetwork::sizeSecondsFor (referenceSizeNorm);

    const float window = std::min ((float) RisingEnvelope::kMaxWindowSeconds,
                                   envelopeWindow * (float) std::sqrt (ratio));

    envelope.setShape (envelopeAmount, window, envelopeSteepness);
    envelopeWindowScaled = window;
}

// Built from measurement, not from the nominal decay alone.
//
// Measured to -60 dB after a second of noise: at Gravity 0 the nominal decay is
// 1.4 s, yet the output takes 6.6 s to get there. The series diffusion is the
// reason. A stage with coefficient g and length d loses 20*log10(1/g) dB every
// d seconds, so the longest stage, 101 ms at 0.90, takes 6.6 s to fall 60 dB on
// its own — longer than the network it feeds. So the tail is whichever runs
// longer, the network or the chain, after the predelay and the reverse window.
//
// The network's own end runs past its nominal decay by an amount that depends
// on Size and modulation (DelayNetwork::getDecayOvershoot); 15 % on top of that
// model, 10 % on the chain, whose longest stage is at most its base length,
// plus one crossing of the longest line.
// Checked against renders across Size, modulation and predelay in the tests.
double ReverbEngine::getTailSeconds() const noexcept
{
    if (requestedFeedback > 0.0f)
        return std::numeric_limits<double>::infinity();

    const double g = std::clamp ((double) seriesCoefficient, 0.0, 0.98);
    const double chainRing = g > 1.0e-3 ? (double) seriesBaseSeconds * 3.0 / std::log10 (1.0 / g) : 0.0;

    const double predelayTime = (double) std::max ({ predelayTarget, predelayFrom, predelayTo }) / sampleRate;
    const double window = envelopeAmount > 0.0f ? (double) envelopeWindowScaled : 0.0;

    const double overshoot = std::max (networkL.getDecayOvershoot(), networkR.getDecayOvershoot());
    const double stretch   = std::max (networkL.getBandStretch(), networkR.getBandStretch());
    const double network   = 1.15 * overshoot * stretch * (double) decaySeconds;

    // Whatever the chain is still ringing has to cross the network once more,
    // and at the largest Size one crossing of the longest line is a second.
    const double crossing = std::max (networkL.getLongestDelaySeconds(), networkR.getLongestDelaySeconds());

    return predelayTime + window + crossing + std::max (network, 1.1 * chainRing) + 0.5;
}

void ReverbEngine::setDecaySeconds (float s) noexcept
{
    decaySeconds = s;
    networkL.setDecaySeconds (s);
    networkR.setDecaySeconds (s);
    updateGlobalGain();
}

void ReverbEngine::setPredelaySeconds (float s) noexcept
{
    predelaySeconds = s;
    const double clamped = std::clamp ((double) s, 0.0, kMaxPredelaySeconds);
    predelayTarget = std::max (DelayLine::minDelaySamples(), (float) (clamped * sampleRate));
}

float ReverbEngine::readPredelay() noexcept
{
    if (predelayFadeLeft == 0)
    {
        if (predelayTarget == predelayFrom)
            return predelay.read (predelayFrom);

        predelayTo = predelayTarget;
        predelayFadeLeft = predelayFadeLength;
    }

    const float t = 1.0f - (float) predelayFadeLeft / (float) predelayFadeLength;
    const float w = t * t * (3.0f - 2.0f * t);
    const float a = predelay.read (predelayFrom), b = predelay.read (predelayTo);

    if (--predelayFadeLeft == 0)
        predelayFrom = predelayTo;

    return a + w * (b - a);
}

void ReverbEngine::setGlobalFeedback (float n) noexcept
{
    requestedFeedback = std::clamp (n, 0.0f, 1.0f);
    updateGlobalGain();
}

// With the governor holding the loop, the feedback gain no longer has to be
// scaled against the network's own resonant gain.
//
// That scaling existed purely for stability, and it worked — but it also meant
// the knob did almost nothing, because the amount of loop the network could
// safely take was the same amount that made the repeats audible. Two sessions
// at the gate said the same thing: fully open was nearly indistinguishable from
// closed.
//
// Now the loop is simply opened, and the governor catches whatever the network
// cannot absorb. Below its ceiling nothing has changed and the path is exactly
// as linear as before; above it, Feedback behaves like an infinite setting that
// sustains rather than one that climbs.
void ReverbEngine::updateGlobalGain() noexcept
{
    globalGain.setTarget (requestedFeedback * kMaxGlobalFeedback);
}

void ReverbEngine::setModDepth (float n) noexcept
{
    modDepth = n;
    networkL.setModDepth (n);
    networkR.setModDepth (n);
    diffusion.setModulation (modDepth, modRateHz);
}

void ReverbEngine::setModRateHz (float hz) noexcept
{
    modRateHz = hz;
    networkL.setModRateHz (hz);
    networkR.setModRateHz (hz);
    diffusion.setModulation (modDepth, modRateHz);
}

void ReverbEngine::setDiffusion (int stages, float coefficient, float baseSeconds) noexcept
{
    diffusion.setStageCount (stages);
    diffusion.setCoefficient (coefficient);
    diffusion.setBaseDelaySeconds (baseSeconds);
    seriesCoefficient = coefficient;
    seriesBaseSeconds = baseSeconds;
}

void ReverbEngine::setDiffusionCoefficient (float coefficient) noexcept
{
    diffusion.setCoefficient (coefficient);
    seriesCoefficient = coefficient;
}

void ReverbEngine::setLineDiffusion (int stages, float coefficient, float baseSeconds) noexcept
{
    networkL.setLineDiffusion (stages, coefficient, baseSeconds);
    networkR.setLineDiffusion (stages, coefficient, baseSeconds);
    updateGlobalGain();
}

void ReverbEngine::setBandGains (float lo, float hi) noexcept
{
    networkL.setBandGains (lo, hi);
    networkR.setBandGains (lo, hi);
}

// Resonance does nothing while Lo and Hi are flat, and sharpens as they move
// away from it. The peak sits at the
// corner the band control is already shaping, so the two read as one filter
// even though they are implemented in different places.
void ReverbEngine::setResonance (float normalised, float lowLinear, float highLinear) noexcept
{
    const float amount = std::clamp (normalised, 0.0f, 1.0f);

    auto shape = [&] (Biquad& l, Biquad& r, double freq, float bandGain)
    {
        const float deviation = std::min (1.0f, std::fabs (1.0f - bandGain));
        const double gainDb   = 12.0 * (double) (amount * deviation);
        const double q        = 0.7 + 5.0 * (double) amount;

        l.setPeaking (sampleRate, freq, q, gainDb);
        r.setPeaking (sampleRate, freq, q, gainDb);
    };

    shape (lowResL,  lowResR,  kLowCornerHz,  lowLinear);
    shape (highResL, highResR, kHighCornerHz, highLinear);
}

void ReverbEngine::process (const float* monoInput, float* outLeft, float* outRight, int numSamples) noexcept
{
    if (converter.getFactor() == 1)
    {
        processInternal (monoInput, outLeft, outRight, numSamples);
        return;
    }

    // Each internal sample becomes getFactor() host samples, played out one
    // per host sample until the next is ready.
    const int factor = converter.getFactor();
    for (int i = 0; i < numSamples; ++i)
    {
        float low = 0.0f;
        if (converter.decimate (monoInput[i], low))
        {
            float l = 0.0f, r = 0.0f;
            processInternal (&low, &l, &r, 1);
            converter.interpolate (0, l, pendingL.data());
            converter.interpolate (1, r, pendingR.data());
            pendingIndex = 0;
        }

        outLeft[i]  = pendingL[(size_t) pendingIndex];
        outRight[i] = pendingR[(size_t) pendingIndex];
        pendingIndex = std::min (pendingIndex + 1, factor - 1);
    }
}

void ReverbEngine::processInternal (const float* monoInput, float* outLeft, float* outRight, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        // At rest the smoother returns exactly 1 or 0, so the input passes
        // bit-exactly when Kill is off.
        const float in = monoInput[i] * killGain.next();

        // Damped and DC-blocked on its way round, so successive repeats get
        // darker instead of accumulating.
        const float returned = loopDcBlocker.process (loopDamping.process (feedbackState));

        // The loop closes here, before the predelay — so the predelay is inside
        // it and each repeat is delayed again.
        // A flat gain, and only a gain. See LoopGovernor.
        float x = in + returned * globalGain.next() * loopGovernor.process (feedbackState);

        // The backstop. Flat until the signal is already far louder than any
        // musical level, so it does nothing in normal use; it exists so that no
        // combination of feedback, band boost and predelay can produce a
        // speaker-damaging transient.
        x = softClip (x);

        // Mechanism (b): at rest on the positive half this returns x untouched.
        x = envelope.process (x);

        predelay.write (x);
        const float delayed = readPredelay();

        // Series diffusion on the mono signal, before the split. This is where
        // the family's character comes from: echo density grows with the power
        // of the chain length, so a modest number of stages turns the network's
        // discrete echoes into a cloud (REVERB_SPEC.md 2.2, 2.3).
        const float diffused = diffusion.process (delayed);

        const float wetL = networkL.process (diffused);
        const float wetR = networkR.process (diffused);

        // The loop is fed from the mono sum of the tail, so the two sides stay
        // coupled through the global repeats rather than drifting apart.
        feedbackState = (wetL + wetR) * 0.5f;

        // Resonance is applied after the feedback tap, so it colours the output
        // without ever circulating (decision 4.7).
        outLeft[i]  = highResL.process (lowResL.process (wetL));
        outRight[i] = highResR.process (lowResR.process (wetR));
    }
}

} // namespace rvb1
