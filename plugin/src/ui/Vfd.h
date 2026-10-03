#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>

// The vacuum-fluorescent display: preset, signed Gravity, decay time, the CORE
// envelope as a seven-row dot matrix, and the annunciators.
//
// Everything is phosphor on dark glass with a soft glow, and every element has
// a ghost — unlit segments and dots stay faintly visible, which is what makes
// it read as a real tube rather than as coloured text. The glass itself is in
// the panel artwork; this draws only what lights.
//
// Composited offscreen at the screen's pixel density and redrawn only when
// something it shows has changed: the glow is a blur, and a blur per repaint
// would cost far more than the display is worth.
class Vfd : public juce::Component
{
public:
    static constexpr int kEnvelopeCols = 33;
    static constexpr int kEnvelopeRows = 7;

    struct State
    {
        juce::String preset;                          // "01 EVERYDAY SPACE"
        int gravity = 0;

        // While a knob other than Gravity is being turned, the big readout
        // shows that knob instead: its name and its value with the unit.
        juce::String focusLabel, focusValue;
        float decaySeconds = 0.0f;
        std::array<int, kEnvelopeCols> envelope {};   // lit rows per column, from the bottom
        bool kill = false;
        bool onB = false;                             // HotSwitch: A or B
        bool dim = false;                             // bypassed: the display goes to sleep

        bool operator== (const State&) const;
    };

    Vfd();

    void setState (const State&);
    std::function<void()> onClick;

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void render (float scale);

    State state;
    juce::Image composite;
    float compositeScale = 0.0f;
    bool dirty = true;
};
