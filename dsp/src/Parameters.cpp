#include "rvb1/Parameters.h"

namespace rvb1
{

namespace
{
    // Shorthand so the table below reads as a table rather than as code.
    // Everything steps by 1 — see the note on ParamDef::step.
    constexpr ParamDef flt (const char* id, const char* name,
                            float lo, float hi, float def,
                            float skew, const char* suffix, bool active = true)
    {
        return { id, name, ParamKind::Float, lo, hi, def, skew, suffix, 1.0f, nullptr, 0, active };
    }

    constexpr ParamDef boolean (const char* id, const char* name, bool def, bool active = true)
    {
        return { id, name, ParamKind::Bool, 0.0f, 1.0f, def ? 1.0f : 0.0f, kLinear, "", 1.0f, nullptr, 0, active };
    }

    const ParamDef kTable[] =
    {
        // Panel order, set by the author 2026-08-23. The generic editor and the
        // test editor both follow it, so what we hear matches what the finished
        // interface will look like.

        flt ("mix", "Mix", 0.0f, 100.0f, 50.0f, kLinear, " %"),

        // The signature control (section 5). Bipolar with a centre detent:
        // negative reshapes the envelope, positive is a pure time axis (5.12).
        flt ("gravity", "Gravity", -100.0f, 100.0f, 0.0f, kLinear, ""),

        // Exponential onto line lengths from ~42 ms to ~1 s (decisions 2.6,
        // 2.7). Default 30, the author's choice once the floor had moved up —
        // a little smaller than the old default of 50 on the old range.
        flt ("size", "Size", 0.0f, 100.0f, 30.0f, kLinear, " %"),

        // Sits inside the feedback loop (decision 4.8). Skewed so the musically
        // dense short end gets most of the travel. Tempo sync is deferred.
        flt ("predelay", "Predelay", 0.0f, 2000.0f, 0.0f, 200.0f, " ms"),

        // How much of each band is present in the tail, via shelving filters in
        // the feedback path. 100 % neutral;
        // below it the band recedes and decays faster, above it it hangs on.
        // Corners are fixed near 500 Hz and 4.2 kHz (decision 4.6).
        flt ("lo", "Lo", 0.0f, 200.0f, 100.0f, kLinear, " %"),
        flt ("hi", "Hi", 0.0f, 200.0f, 100.0f, kLinear, " %"),

        // Phase 0 test B8 put "churning" around the middle of this range and
        // found 30 % indistinguishable from off, so the lower half must not be a
        // dead zone when Phase 3 voices it.
        flt ("modDepth", "Mod Depth", 0.0f, 100.0f, 40.0f, kLinear, " %"),

        // Relative, not Hz. Phase 2 maps 0-100 onto the useful LFO range, which
        // lives well below 1 Hz at the slow end and would read as "0" in whole
        // Hz. The target describes its own control as a relative speed too.
        flt ("modRate", "Mod Rate", 0.0f, 100.0f, 20.0f, kLinear, " %"),

        // The global loop around the whole structure, opened to at most 0.95.
        // The loop governor holds whatever the network cannot absorb, so fully
        // open sustains rather than climbs. Never unity — Freeze is not in v1.
        flt ("feedback", "Feedback", 0.0f, 100.0f, 0.0f, kLinear, " %"),

        // Output only, outside the loop: inside it, a resonance can overload.
        // The safe version (decision 4.7).
        flt ("resonance", "Resonance", 0.0f, 100.0f, 0.0f, kLinear, " %"),

        // In scales the whole input, dry included, so in a linear chain only its
        // product with Out matters (decision 3.6).
        flt ("inputGain", "In", -12.0f, 12.0f, 0.0f, kLinear, " dB"),
        flt ("outputGain", "Out", -12.0f, 12.0f, 0.0f, kLinear, " dB"),

        boolean ("kill", "Kill", false),

        // Declared, inert (decision 3.2), and since 2026-10-02 no longer on the
        // panel. Kept declared so saved sessions that carry it still load.
        boolean ("freeze", "Freeze", false, false),

        // HotSwitch rather than Ribbon: the same idea without the morph. Two
        // stored states, one button, smoothed transition. Chosen over Ribbon
        // because a continuous morph needs an interpolation rule for every
        // parameter, and those rules would be rewritten every time voicing
        // moves a range. Active since 2026-10-02: off is A, on is B (the
        // switching itself is in the plugin, presets/HotSwitch).
        boolean ("hotswitch", "HotSwitch", false, true)
    };

    static_assert (sizeof (kTable) / sizeof (kTable[0]) == static_cast<size_t> (Param::Count),
                   "the parameter table and the Param enum have drifted apart");
}

const ParamDef& definition (Param p)
{
    return kTable[static_cast<int> (p)];
}

} // namespace rvb1
