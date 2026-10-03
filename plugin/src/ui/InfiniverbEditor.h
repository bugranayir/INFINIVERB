#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>

#include "Layout.h"
#include "PresetBrowser.h"
#include "Vfd.h"

class Rvb1Processor;

// The INFINIVERB panel (Figma "INFINIVERB / Panel", hand-off in
// design/infiniverb/): a two-tier rack unit.
//
// The panel is drawn in the design's own 1623 x 422.5 units and scaled as one.
// The static plate — labels, scales, knob bodies, buttons, the empty display
// glass — is a single image exported from the design. Live on top of it: each
// knob's pointer sprite, the pressed state of the push buttons, the bypass LED,
// the display, and the preset browser.
class InfiniverbEditor : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    InfiniverbEditor (Rvb1Processor&, juce::AudioProcessorValueTreeState&);
    ~InfiniverbEditor() override;

    void resized() override;

private:
    class Panel;
    class Knob;
    class Pointers;
    class PushButton;

    void timerCallback() override;
    Vfd::State displayState() const;
    static juce::String formatValue (const juce::String& param, float value);

    // The knob last turned by hand, and when; the display shows it for a moment.
    juce::String focusParam, focusLabel;
    juce::uint32 focusTime = 0;

    Rvb1Processor& owner;
    juce::AudioProcessorValueTreeState& apvts;

    std::unique_ptr<Panel> panel;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> knobAttachments;
    std::unique_ptr<Pointers> pointers;

    std::unique_ptr<PushButton> kill, bypass, presetDown, presetUp, hotA, hotB;
    void selectSlot (bool b);
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> killAttachment, bypassAttachment;

    Vfd display;
    std::unique_ptr<PresetBrowserOverlay> browser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InfiniverbEditor)
};
