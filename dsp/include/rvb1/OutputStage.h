#pragma once

#include "Smoothed.h"

namespace rvb1
{

// The final stage: wet/dry balance, output trim, and a safety ceiling on the
// wet signal.
//
// The ceiling (decided 2026-10-03): with Feedback fully open the governor
// holds the loop rather than letting it climb, but where it holds it can be
// loud — the release checks measured +15 to +21 dBFS at the extremes, +30 with
// In and Out both at +12. So the wet signal, as it reaches the output, is
// held under 0 dBFS: a limiter aiming at -0.9 dBFS (1 ms attack, 20 ms hold,
// 150 ms release), and a soft knee from there to 0 dBFS that rounds off
// whatever gets ahead of the attack.
// Below the ceiling the gain is exactly one and the stage is bit-identical to
// what it was without it. The dry signal is the user's and is never touched.
//
// This is DSP, not host integration, so it lives in the core, where it can be
// verified bit-exactly without a host. Kill is not here: it mutes what enters
// the reverb, so it lives at the engine's input (decision 4.12).
class OutputStage
{
public:
    void prepare (double sampleRate, float smoothingSeconds = 0.03f) noexcept;

    // Targets are set per block; the gains themselves glide per sample.
    void setMixPercent (float percent) noexcept;     // 0 = fully dry, 100 = fully wet
    void setOutputTrimDb (float decibels) noexcept;

    // Jump the gains to their targets instead of gliding to them.
    void snapToTargets() noexcept;

    // In-place: `channels` carries the dry signal on entry and the result on
    // exit. `wet` is the reverb output, unchanged.
    void process (float* const* channels,
                  const float* const* wet,
                  int numChannels,
                  int numSamples) noexcept;

private:
    Smoothed dryGain, wetGain, trimGain;

    static constexpr float kWetCeiling = 1.0f;     // 0 dBFS
    static constexpr float kWetKnee    = 0.9f;     // about -0.9 dBFS: the limiter aims here
    float ceilingReduction = 0.0f;                 // 1 - the ceiling's gain
    float ceilingHeld = 0.0f;
    int   ceilingHoldLeft = 0, ceilingHoldSamples = 960;
    float ceilingAttack = 0.0f, ceilingRelease = 0.0f;

public:
    // How far the ceiling is holding the wet signal down, for tests: 1 when it
    // is doing nothing.
    float getCeilingGain() const noexcept { return 1.0f - ceilingReduction; }

private:

    // Held so prepare() can put the gains at sane values immediately. A stage
    // whose trim gain starts at zero fades in from silence on every prepare,
    // which is the same "starts from zero" trap as REFERENCE_NOTES.md 10.
    float pendingMixPercent = 50.0f;
    float pendingTrimDb     = 0.0f;
};

} // namespace rvb1
