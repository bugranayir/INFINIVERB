#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// HotSwitch: two complete settings, A and B, and a switch between them.
//
// Switching stores the current settings in the slot being left and recalls the
// other. A slot that has never been visited starts as a copy of the current
// settings, so the first switch changes nothing until the two are made to
// differ.
//
// The recall goes through the host — a change gesture and a host-notified
// value for every parameter that differs — so the host treats it like the
// user turning those knobs: parameters under automation have their automation
// overridden, and the user re-enables it. Which slot the automation was
// written for is the user's business; there is one automation lane per
// parameter, read whichever slot is selected. (The author's specification,
// 2026-10-02.)
//
// Kill, Bypass, Freeze and the switch itself are performance and session
// controls, not part of a setting, and are left alone. Both slots live in the
// parameter tree's state, so they are saved with the session.
class HotSwitch : private juce::AudioProcessorValueTreeState::Listener,
                  private juce::AsyncUpdater
{
public:
    explicit HotSwitch (juce::AudioProcessorValueTreeState&);
    ~HotSwitch() override;

    // After the host restores a session: the slot in use is whatever the
    // restored switch says.
    void syncToState();

    bool isOnB() const;

    // Performs a pending switch now rather than on the next message-loop turn.
    // For the checks tool, which has no message loop running.
    void flush() { handleUpdateNowIfNeeded(); }

private:
    void parameterChanged (const juce::String&, float) override;
    void handleAsyncUpdate() override;

    juce::ValueTree capture() const;
    void recall (const juce::ValueTree&);
    static bool belongsToSettings (const juce::String& id);

    juce::AudioProcessorValueTreeState& apvts;
    int currentSlot = 0;   // 0 = A, 1 = B
};
