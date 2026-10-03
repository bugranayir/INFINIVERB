#pragma once

// The parameter definition table.
//
// This lives in the DSP core, not in the plugin, and deliberately uses no JUCE
// types. The plugin builds its APVTS by walking this table, so there is
// exactly one place where a parameter's range, default and curve are decided.
//
// Ranges and defaults come from PROJECT_DECISIONS.md 3.1. Where a value was
// settled by ear in Phase 0 the decision is cited on the line.

#include <cstddef>
#include <limits>

namespace rvb1
{

// The final control set, settled 2026-08-23 (decision 3.1): twelve knobs plus
// Kill, Bypass and the predelay tempo button.
//
// In HP, In LP, Lo Freq and Hi Freq were removed outright rather than hidden.
// Removing a parameter before release costs nothing; afterwards it invalidates
// every preset that references its id.
enum class Param
{
    Mix,
    Gravity,
    Size,
    Predelay,
    Lo,
    Hi,
    ModDepth,
    ModRate,
    Feedback,
    Resonance,
    InputGain,
    OutputGain,
    Kill,
    Freeze,   // declared but inert in v1 (decision 3.2)
    HotSwitch, // declared but inert in v1 (decision 3.2)
    Count
};

enum class ParamKind { Float, Bool, Choice };

// No perceptual curve — the control is linear across its range.
inline constexpr float kLinear = std::numeric_limits<float>::quiet_NaN();

struct ParamDef
{
    const char* id;        // stable identifier — never change, presets key on it
    const char* name;      // shown in the host
    ParamKind   kind;

    float minValue;
    float maxValue;
    float defaultValue;

    // Perceptual curve. REVERB_SPEC.md 5.5 requires exponential/logarithmic
    // mapping for anything time- or frequency-like. This is the value that
    // should land at the centre of the knob's travel, or kLinear for none.
    //
    // The sentinel is NaN rather than 0 on purpose: 0 is a legal skew centre
    // for a bipolar parameter like Gravity, so using it would have silently
    // marked Gravity as skewed. Every comparison against NaN is false, so the
    // ordinary range checks below already read as "not skewed".
    float skewCentre;

    const char* suffix;    // unit shown after the value

    // Step size. Every control steps in whole numbers at the author's request:
    // fractional readouts were making the panel hard to read during listening
    // tests, and nothing here needs finer resolution than one unit. Mod Rate is
    // expressed as a relative 0-100 rather than in Hz for the same reason — and
    // that also matches how the target describes it, as a relative speed.
    float step;

    // Choice parameters only.
    const char* const* choices;
    int numChoices;

    // False for parameters that exist in the tree but do nothing in v1. They are
    // declared from the start so that adding them later cannot invalidate a
    // saved session or preset (decision 3.1).
    bool activeInV1;
};

const ParamDef& definition (Param p);
inline constexpr int numParams() { return static_cast<int> (Param::Count); }

} // namespace rvb1
