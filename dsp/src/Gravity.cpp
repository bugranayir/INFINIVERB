#include "rvb1/Gravity.h"

#include <algorithm>
#include <cmath>

namespace rvb1
{

namespace
{
    // A non-finite value passes straight through std::clamp and on into the
    // loop, and a NaN in a feedback loop never leaves — the Phase 0 trap
    // (REFERENCE_NOTES.md 10). Every input is caught at the door instead.
    float finiteOr (float value, float fallback) noexcept
    {
        return std::isfinite (value) ? value : fallback;
    }
}

GravityTargets mapGravity (float gravity, const GravityVoicing& voicing) noexcept
{
    gravity = finiteOr (gravity, 0.0f);

    const float centre = std::clamp (finiteOr (voicing.centreSeconds, kGravityCentreSeconds),
                                     0.1f, kGravityMaxDecaySeconds);
    const float floor  = std::clamp (finiteOr (voicing.reverseFloor, kGravityReverseFloor),
                                     0.0f, kGravityCentreDiffusion);
    const float curve  = std::clamp (finiteOr (voicing.reverseCurve, kGravityReverseCurve),
                                     0.5f, 4.0f);
    const float windowMax = std::clamp (finiteOr (voicing.windowMax, kGravityEnvelopeWindowMax),
                                        kGravityEnvelopeWindowMin, 2.0f);
    const float steepMax  = std::clamp (finiteOr (voicing.steepnessMax, kGravityEnvelopeSteepnessMax),
                                        1.0f, 8.0f);
    const float fall      = std::clamp (finiteOr (voicing.fallSeconds, kGravityReverseFallSeconds),
                                        0.1f, kGravityMaxDecaySeconds);

    const float travel  = std::clamp (std::fabs (gravity) * 0.01f, 0.0f, 1.0f);
    const float reverse = gravity < 0.0f ? travel : 0.0f;

    GravityTargets t;

    // Constant across the positive half (5.12). On the negative half it falls
    // to the floor; at the centre the two meet, so a sweep through zero is
    // continuous.
    t.diffusion = floor + (kGravityCentreDiffusion - floor) * std::pow (1.0f - reverse, curve);

    // Mechanism (b). All three are continuous through the centre: amount is 0
    // there, and the window and steepness sit at their starting values.
    t.envelopeAmount    = std::clamp (reverse / kGravityEnvelopeFadeIn, 0.0f, 1.0f);
    t.envelopeWindow    = kGravityEnvelopeWindowMin
                        * std::pow (windowMax / kGravityEnvelopeWindowMin, reverse);
    t.envelopeSteepness = 1.0f + (steepMax - 1.0f) * reverse;

    if (gravity >= 0.0f)
    {
        // Exponential, so equal knob travel is an equal *ratio* of decay time
        // (REVERB_SPEC.md 5.5): 1 s to 2 s takes the same turn as 10 s to 20 s.
        // A linear map would spend almost all of the knob on long tails and
        // cram everything short into the first few degrees.
        t.decaySeconds = centre * std::pow (kGravityMaxDecaySeconds / centre, travel);
    }
    else
    {
        // The fall: from the centre toward the reverse fall time (5.5), on the
        // same exponential footing. It is never allowed so short that rise plus
        // fall comes in under the centre, so the sound as a whole still does
        // not get shorter in this direction (5.3) — even though the tail does.
        const float towardFall = centre * std::pow (fall / centre, reverse);
        t.decaySeconds = std::max (towardFall, centre - t.envelopeWindow);
    }

    return t;
}

} // namespace rvb1
