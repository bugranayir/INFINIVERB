#pragma once

namespace rvb1
{

// Gravity — the signature control (PROJECT_DECISIONS.md section 5).
//
// Everything Gravity drives is derived here, from the one knob value, so the
// plugin and the headless tests read the same mapping. Phase 4 grows this one
// mechanism at a time.
//
// The two halves do categorically different jobs (decision 5.12): positive is a
// time axis — a longer decay and nothing else — and negative is a shape axis,
// the reverse envelope. Both directions lengthen the tail (5.3).

// The decay at +100. Nominal: with the in-line diffusion a T60 fit reads 5-20 %
// longer (DEVELOPMENT_ROADMAP.md, Phase 3 outcome), and the very end of the
// tail runs further still — up to about 2.4 times at the smallest Size with no
// modulation (DelayNetwork::getDecayOvershoot).
constexpr float kGravityMaxDecaySeconds = 60.0f;

// The decay at the centre — the shortest the plugin can ever be, since both
// directions lengthen from here (decision 5.3). Chosen by ear in Phase 4.1
// with a temporary slider: 1.4 s. The interim mapping had used 0.3 s, which
// the author had already found too short at the Phase 2 gate.
constexpr float kGravityCentreSeconds = 1.4f;

// The series diffusion coefficient at the centre and across the whole positive
// half, as voiced in Phase 3. Above ~0.707 an allpass chain decays smoothly
// (REVERB_SPEC.md 2.3), which is what decision 5.12 requires of the positive
// half: longer, and nothing else.
constexpr float kGravityCentreDiffusion = 0.90f;

// Mechanism (a) of the negative half (decision 5.6): the coefficient falls
// below the smooth-decay threshold, and echoes build up instead of decaying.
// Phase 0 established what this gives — the *texture* of reverse, with an
// onset that is immediate at every setting. The timing comes later, from (b).
//
// Both chosen by ear in Phase 4.2, with temporary sliders.
//
// The floor is the coefficient at -100: 0.22, just above where the tail began
// to break into separate echoes in our own diffuser. Phase 0 had put that
// point at 0.2; the spec's figure was 0.3.
constexpr float kGravityReverseFloor = 0.22f;

// How the coefficient travels from the centre to the floor. 1 is linear in the
// coefficient; above 1 it drops sooner, so the reverse texture arrives earlier
// in the knob's travel. 1.2: a little sooner than linear.
constexpr float kGravityReverseCurve = 1.2f;

// Mechanism (b) of the negative half (decision 5.6): the rising early envelope,
// and the primary tuning target of the whole negative half. The author's
// requirement is that as Gravity travels negative the swell arrives
// progressively later and rises progressively more slowly — which is the
// window length and the ramp's steepness, both growing with the travel.
//
// Near the centre the window is short (the "smeared puffs" of REVERB_SPEC.md
// 5.1) and grows exponentially toward its length at -100. The steepness grows
// linearly from a plain ramp. Both extremes chosen by ear in Phase 4.3: a
// 1.45 s window at the default Size — well past the spec's 300-800 ms — and a
// steepness of 2.
constexpr float kGravityEnvelopeWindowMin      = 0.03f;
constexpr float kGravityEnvelopeWindowMax      = 1.45f;
constexpr float kGravityEnvelopeSteepnessMax   = 2.0f;

// Decision 5.5: toward the negative extreme the shape becomes lopsided — a long
// rise, then a relatively fast fall. The rise is the envelope above; the fall
// is the network's own decay, which on the negative half moves from the centre
// toward this value rather than along the positive half's time axis.
//
// With that axis borrowed, -100 rose for 1.45 s and then rang for sixty, and
// the author heard it — correctly — as a slow attack. Chosen by ear with a
// temporary slider: 4.7 s. Still longer than the centre, so the tail does grow
// in this direction, but by a factor of about three rather than forty, and at
// -100 the author hears it as unmistakably reverse.
constexpr float kGravityReverseFallSeconds = 4.7f;

// How far into the negative half the envelope is fully in. Before that it is
// crossfaded with the straight signal, so a sweep through the centre does not
// switch anything on — it fades in over the first 15 % of the travel.
constexpr float kGravityEnvelopeFadeIn = 0.15f;

struct GravityTargets
{
    float decaySeconds = 0.0f;
    float diffusion    = 0.0f;   // series allpass coefficient

    // The rising early envelope. Amount 0 is a straight pass-through, which is
    // what the whole positive half gets (5.12).
    float envelopeAmount    = 0.0f;
    float envelopeWindow    = kGravityEnvelopeWindowMin;   // seconds, before Size scaling
    float envelopeSteepness = 1.0f;
};

// The values chosen by ear. A parameter only so the tests can check the shape
// for any choice, and so a voicing session can move them without a rebuild.
struct GravityVoicing
{
    float centreSeconds = kGravityCentreSeconds;
    float reverseFloor  = kGravityReverseFloor;
    float reverseCurve  = kGravityReverseCurve;
    float windowMax     = kGravityEnvelopeWindowMax;
    float steepnessMax  = kGravityEnvelopeSteepnessMax;
    float fallSeconds   = kGravityReverseFallSeconds;
};

// gravity is the knob value, -100..100.
GravityTargets mapGravity (float gravity, const GravityVoicing& voicing = {}) noexcept;

} // namespace rvb1
