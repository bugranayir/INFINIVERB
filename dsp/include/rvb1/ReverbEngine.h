#pragma once

#include <array>

#include "Gravity.h"
#include "Halfband.h"
#include "DelayLine.h"
#include "Biquad.h"
#include "DcBlocker.h"
#include "DelayNetwork.h"
#include "Diffuser.h"
#include "LoopGovernor.h"
#include "RisingEnvelope.h"
#include "Shelf.h"
#include "Smoothed.h"

namespace rvb1
{

// The whole reverb: predelay, the global feedback loop, and the two networks
// that make the stereo tail.
//
// The routing here is decision 4.8 and 4.9, and it is the reason this class
// exists rather than the plugin wiring the pieces together itself:
//
//     in ──▶(+)──▶ predelay ──▶ diffusion ──┬─▶ network L ──┬──▶ out L
//            ▲                               └─▶ network R ──┼──▶ out R
//            │                                               │
//            └────────── global feedback ◀───────────────────┘
//
// The diffusion block is a series allpass chain on the mono signal, before the
// split — the arrangement REVERB_SPEC.md 2.2 describes for this family.
//
// The predelay sits *inside* the loop, so a long predelay with high feedback
// produces rhythmic, accumulating repeats rather than a single delayed tail.
// The return point is before the predelay, which is also where Phase 4's rising
// early envelope will go — so every repeat will re-swell once Gravity's
// negative half exists. Structured now so it does not need rewiring later.
class ReverbEngine
{
public:
    // Series diffusion as voiced by ear in Phase 3, over two sessions, the
    // second after a bug that had been giving the length control a two-second
    // lag. Same answer both times.
    //
    // 0.90 sits just past the knee REVERB_SPEC.md 2.3 describes: measured,
    // attack falls from 657 ms to 157 ms between 0.82 and 0.90, and density
    // peaks just below it. Twelve stages rather than the twenty-four the spec
    // mentions, because sixteen and above buys a little density for four
    // times the attack. Applied in prepare; setDiffusion overrides it. The
    // coefficient is Gravity's at the centre, so it lives with Gravity.
    static constexpr int   kSeriesStages      = 12;
    static constexpr float kSeriesCoefficient = kGravityCentreDiffusion;
    static constexpr float kSeriesBaseSeconds = 0.101f;

    // The host's rate. At 88.2 kHz and above the reverb itself runs at 44.1
    // or 48 kHz, the rate it was voiced at, behind a halfband resampler
    // (Halfband.h); process() still takes and returns host-rate samples.
    void prepare (double sampleRate);
    void reset() noexcept;

    // Every glide and crossfade to its end. For the start of playback, after
    // the settings have been applied: prepare() starts from the defaults, and
    // without this the first moments of a render glide from there — Size
    // sweeping in, the predelay fading across from zero.
    void snapToTargets() noexcept;

    // The rate the reverb runs at internally.
    double getInternalSampleRate() const noexcept { return sampleRate; }

    void setSize (float normalised) noexcept;
    void setDecaySeconds (float seconds) noexcept;
    void setPredelaySeconds (float seconds) noexcept;

    // 0 = no global loop, 1 = as much as the clamp allows.
    void setGlobalFeedback (float normalised) noexcept;

    void setModDepth (float normalised) noexcept;
    void setModRateHz (float hz) noexcept;
    void setBandGains (float lowLinear, float highLinear) noexcept;

    // The series diffusion in front of the network, all three values at once —
    // for tests. prepare applies the voiced values, and in the plugin only the
    // coefficient moves, through Gravity (mechanism (a)).
    void setDiffusion (int stages, float coefficient, float baseSeconds) noexcept;
    void setDiffusionCoefficient (float coefficient) noexcept;
    void setLineDiffusion (int stages, float coefficient, float baseSeconds) noexcept;

    // Mechanism (b) of Gravity's negative half. The window is given for the
    // default Size and scaled here with the space, partially — a square-root
    // curve — and capped at two seconds (decision 5.7). Uncapped, a one-second
    // line would stretch the swell into something no one could play.
    void setEnvelope (float amount, float windowSeconds, float steepness) noexcept;


    // A resonant peak at each of the Lo/Hi corners, at the output only. Its
    // depth scales with how far the band controls sit from flat, so — as on the
    // target — it does nothing while Lo and Hi are neutral.
    void setResonance (float normalised, float lowLinear, float highLinear) noexcept;

    // Mutes what enters the structure; everything already circulating keeps
    // going (decision 4.12).
    //
    // It fades over 8 ms rather than switching, which is decision 4.12's
    // requirement: cutting the input in one sample puts a step into the
    // diffusion, and the reverb turns that into a tick in the tail.
    //
    // Kill with Feedback up is a pseudo-freeze, not a freeze: the loop still
    // decays and still drifts under modulation (PROJECT_DECISIONS.md 3.2).
    void setKilled (bool shouldKill) noexcept { killGain.setTarget (shouldKill ? 0.0f : 1.0f); }

    // The input fades back in over Kill's 8 ms instead of arriving at full
    // level — for leaving bypass, where it would otherwise start mid-cycle and
    // the reverb would ring with the step.
    void fadeInInput() noexcept { killGain.snap (0.0f); }

    void process (const float* monoInput, float* outLeft, float* outRight, int numSamples) noexcept;

    // How long the output keeps sounding after the input stops, for the host
    // (getTailLengthSeconds). Infinite whenever Feedback is up: with the global
    // loop open, long decays sustain indefinitely, and measured, even a quarter
    // of the knob doubles the tail at +100. See the definition for the rest.
    double getTailSeconds() const noexcept;

    // How much the governor is currently holding the loop back. 1 means it is
    // doing nothing, which is the case at any ordinary Feedback setting.
    float getGovernorGain() const noexcept { return loopGovernor.getGain(); }

private:
    void processInternal (const float* monoInput, float* outLeft, float* outRight, int numSamples) noexcept;

    RateConverter converter;
    std::array<float, 8> pendingL {}, pendingR {};
    int pendingIndex = 0;

    DelayNetwork networkL, networkR;
    DelayLine    predelay;

    // 24 stages, because the author reached the previous ceiling of 12 and
    // wanted more — and because REVERB_SPEC.md 2.2 puts the target at about
    // twenty-four allpasses per output path. The ear arrived where the
    // documentation already was.
    Diffuser<24> diffusion;

    // Between the loop's summing point and the predelay, so every repeat of the
    // global feedback passes through it again and re-swells (decision 4.9).
    RisingEnvelope envelope;
    void applyEnvelope() noexcept;
    float envelopeAmount = 0.0f, envelopeWindow = kGravityEnvelopeWindowMin, envelopeSteepness = 1.0f;
    float envelopeWindowScaled = kGravityEnvelopeWindowMin;

    // Kept only to answer getTailSeconds.
    float decaySeconds = 2.0f;
    float seriesCoefficient = kSeriesCoefficient;
    float seriesBaseSeconds = kSeriesBaseSeconds;
    float sizeNorm = 0.5f;
    float referenceSizeNorm = 0.3f;   // the default Size; set from the parameter table in prepare
    Biquad       lowResL, lowResR, highResL, highResR;

    void updateGlobalGain() noexcept;

    // Damping inside the global loop. Repeats getting progressively darker is
    // both the classic behaviour and the thing that stops the loop building at
    // the extremes — a bare gain around a resonant network is what ran away.
    LoopGovernor loopGovernor;
    OnePoleShelf loopDamping;
    DcBlocker    loopDcBlocker;

    double sampleRate = 48000.0;     // internal: the host rate divided down to 44.1 or 48 kHz

    // A predelay change crossfades from the old tap to the new one instead of
    // jumping: a read position that jumps is a click, and turning the knob
    // made one at every step. A change that arrives mid-fade waits for it to
    // finish, so a sweep moves in short crossfaded steps.
    float readPredelay() noexcept;
    float  predelaySeconds = 0.0f;
    float  predelayTarget = 3.0f, predelayFrom = 3.0f, predelayTo = 3.0f;
    int    predelayFadeLength = 1, predelayFadeLeft = 0;

    float  requestedFeedback = 0.0f;
    Smoothed globalGain { 0.0f };     // 30 ms: a loop gain that jumps is a step in everything circulating
    float  modDepth   = 0.0f;
    float  modRateHz  = 0.4f;
    float  feedbackState = 0.0f;
    Smoothed killGain { 1.0f };
};

} // namespace rvb1
