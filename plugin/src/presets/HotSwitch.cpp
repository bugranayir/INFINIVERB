#include "HotSwitch.h"

namespace
{
    const juce::Identifier kSlotTag[2] { "HotSwitchA", "HotSwitchB" };
    const juce::Identifier kParamTag   { "PARAM" };
    constexpr const char*  kSwitchId   = "hotswitch";
}

HotSwitch::HotSwitch (juce::AudioProcessorValueTreeState& state) : apvts (state)
{
    apvts.addParameterListener (kSwitchId, this);
    syncToState();
}

HotSwitch::~HotSwitch()
{
    apvts.removeParameterListener (kSwitchId, this);
    cancelPendingUpdate();
}

bool HotSwitch::belongsToSettings (const juce::String& id)
{
    return id != "bypass" && id != "kill" && id != "freeze" && id != kSwitchId;
}

bool HotSwitch::isOnB() const
{
    return apvts.getRawParameterValue (kSwitchId)->load() > 0.5f;
}

void HotSwitch::syncToState()
{
    currentSlot = isOnB() ? 1 : 0;
}

// Possibly on the audio thread (automation), so only a flag here; the recall
// happens on the message thread.
void HotSwitch::parameterChanged (const juce::String&, float)
{
    triggerAsyncUpdate();
}

juce::ValueTree HotSwitch::capture() const
{
    juce::ValueTree v ("Slot");
    for (auto* p : apvts.processor.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (belongsToSettings (r->getParameterID()))
                v.appendChild (juce::ValueTree (kParamTag, { { "id", r->getParameterID() },
                                                             { "value", r->getValue() } }), nullptr);
    return v;
}

void HotSwitch::recall (const juce::ValueTree& slot)
{
    for (auto* p : apvts.processor.getParameters())
    {
        auto* r = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (r == nullptr || ! belongsToSettings (r->getParameterID()))
            continue;

        const auto child = slot.getChildWithProperty ("id", r->getParameterID());
        if (! child.isValid())
            continue;

        // Only what differs is touched, so automation on a parameter the two
        // slots share is left running.
        const float target = (float) child.getProperty ("value");
        if (! std::isfinite (target) || std::abs (target - r->getValue()) < 1.0e-6f)
            continue;

        r->beginChangeGesture();
        r->setValueNotifyingHost (target);
        r->endChangeGesture();
    }
}

void HotSwitch::handleAsyncUpdate()
{
    const int target = isOnB() ? 1 : 0;
    if (target == currentSlot)
        return;

    auto& state = apvts.state;

    // Keep what is being left.
    state.getOrCreateChildWithName (kSlotTag[currentSlot], nullptr).copyPropertiesAndChildrenFrom (capture(), nullptr);

    // Recall the other; a slot never visited starts as a copy of this one.
    const auto incoming = state.getChildWithName (kSlotTag[target]);
    currentSlot = target;

    if (incoming.isValid())
        recall (incoming);
}
