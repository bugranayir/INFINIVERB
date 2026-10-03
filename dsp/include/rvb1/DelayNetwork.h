#pragma once

#include "DcBlocker.h"
#include "DelayLine.h"
#include "Diffuser.h"
#include "Lfo.h"
#include "Slewed.h"
#include "Smoothed.h"
#include "Shelf.h"

#include <cstdint>

namespace rvb1
{

// Eight delay lines and their modulation.
//
// The lines never exchange energy. Each is an independent comb with its own
// allpass diffusion inside it, rather than a textbook FDN's mixing
// (decision 4.3). A Householder matrix was built and compared
// by ear and by measurement; with the in-line diffusers present it changed
// nothing — the diffusers already do the job a mixing matrix exists for.
//
// One of these makes one channel of the tail. Decision 4.2 sums the input to
// mono and generates the stereo pair from two of these, decorrelated by seed —
// so the two sides differ in their delay lengths and their modulation, not by
// being fed different signal.
class DelayNetwork
{
public:
    static constexpr int kNumLines = 8;

    // In-line diffusion as voiced by ear in Phase 3. The author heard a clear
    // contribution here even though the density measure barely moved — a
    // single-instant density figure does not capture what scattering inside
    // the loop does over time. Applied in prepare, so the core runs the voiced
    // structure without being told; setLineDiffusion overrides it.
    static constexpr int   kLineStages      = 8;
    static constexpr float kLineCoefficient = 0.80f;
    static constexpr float kLineBaseSeconds = 0.060f;

    void prepare (double sampleRateHz, std::uint32_t seed);
    void reset() noexcept;

    // Every glide to its end: lengths, modulation, decay, band gains.
    void snapToTargets() noexcept;

    void setSize (float normalised) noexcept;          // 0..1
    void setDecaySeconds (float t60Seconds) noexcept;
    void setModDepth (float normalised) noexcept;      // 0..1
    void setModRateHz (float hz) noexcept;

    // 1.0 is flat. Below it the band recedes and decays faster, above it the
    // band is emphasised and hangs on longer (decision 4.6).
    void setBandGains (float lowGain, float highGain) noexcept;

    // Allpass diffusion inside each line's own feedback path. This is the
    // second half of REVERB_SPEC.md 2.2's structure, and the thing that lets
    // the global feedback be opened up: it narrows the gap between the
    // network's gain at its resonances and its gain everywhere else, and that
    // gap is what forced the loop to be scaled back in Phase 2.
    void setLineDiffusion (int stages, float coefficient, float baseSeconds) noexcept;

    float process (float input) noexcept;

    float getLongestDelaySeconds() const noexcept;

    // How much longer than its nominal decay the very end of the tail runs.
    // Used to tell the host how long the tail really is; see the definition.
    float getDecayOvershoot() const noexcept;

    // How many times longer than the decay a boosted Lo or Hi band rings: 1
    // when neither is boosted, at most kMaxBandStretch. For the tail report.
    float getBandStretch() const noexcept;

    // The longest a boosted band may ring, as a multiple of the decay
    // (decided 2026-10-03; see applyBandGains).
    static constexpr float kMaxBandStretch = 3.0f;

    // The base line length Size maps to, before each line's own ratio. Public
    // so the reverse window can scale with the space (decision 5.7) without
    // restating the curve.
    static double sizeSecondsFor (float normalised) noexcept;

    // The largest per-line decay gain. The engine needs it because the network's
    // gain at its own resonances rises as this approaches unity, and anything
    // wrapped around the network has to leave room for that.
    float getMaxLineGain() const noexcept;

private:
    void recomputeDelays() noexcept;
    void recomputeFeedback() noexcept;
    void applyBandGains() noexcept;
    void configureLineDiffuser (int line, int stages, float coefficient, float baseSeconds) noexcept;
    void updateLineModulation() noexcept;

    DelayLine  lines[kNumLines];
    Lfo        lfos[kNumLines];
    DcBlocker  dcBlockers[kNumLines];
    OnePoleShelf lowShelves[kNumLines];
    OnePoleShelf highShelves[kNumLines];

    // Eight stages per line.
    // Twelve in the series block plus these reaches the count REVERB_SPEC.md
    // 2.2 gives for the target.
    Diffuser<8> lineDiffusers[kNumLines];

    // Mutually prime line lengths, as REVERB_SPEC.md 3.1 asks for. Primes make
    // the ratios incommensurate by construction, so the lines cannot fall into
    // a common period and ring as one comb.
    float ratios[kNumLines] {};

    // Delay lengths move at a bounded rate rather than jumping at block
    // boundaries. Two things depend on this.
    //
    // REVERB_SPEC.md 5.5 is explicit that an unsmoothed delay length clicks,
    // and it does — sweeping Size crackled until this existed.
    //
    // And the rate cap *is* the pitch limit. A delay growing by r samples per
    // sample plays back at (1 - r) speed, so bounding r bounds the Doppler.
    // Without the bound a full Size sweep dives about eighteen semitones, which
    // the author described as the signal squashing into the low end. Some pitch
    // movement on Size is wanted — just far less, settling quickly. That is
    // what the cap is set for (decision 2.8).
    Slewed   delaySlewed[kNumLines];
    Smoothed modSmoothed[kNumLines];

    // The decay gain moves with Size as well as with the decay time, so it
    // steps on every block during a sweep unless it glides too.
    Smoothed feedbackSmoothed[kNumLines];

    float delaySamples[kNumLines] {};
    float modSamples[kNumLines] {};
    float feedbackGain[kNumLines] {};
    float modScale[kNumLines] {};      // per-line spread, from the seed
    float inputSign[kNumLines] {};     // +/-1, so the input does not arrive coherently

    double sampleRate = 48000.0;
    float  sizeNorm   = 0.5f;
    float  decaySecs  = 2.0f;
    float  depthNorm  = 0.4f;
    float  rateHz     = 0.4f;
    float  lowGain    = 1.0f;
    float  highGain   = 1.0f;
};

} // namespace rvb1
