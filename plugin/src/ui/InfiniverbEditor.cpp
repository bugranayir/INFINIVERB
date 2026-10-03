#include "InfiniverbEditor.h"
#include "../PluginProcessor.h"
#include "BinaryData.h"

#include "rvb1/DelayNetwork.h"
#include "rvb1/Gravity.h"
#include "rvb1/RisingEnvelope.h"

using namespace infiniverb;

namespace
{
    juce::Image asset (const void* data, int size) { return juce::ImageCache::getFromMemory (data, size); }

    // A fixed window at 70 % of the design size: 1623 units is wider than many
    // laptop screens. Not resizable — the author's call.
    constexpr float kScale = 0.7f;

    // How long the display shows a knob after it was last turned.
    constexpr juce::uint32 kFocusMs = 1500;

    const char* knobLabel (const juce::String& param)
    {
        if (param == "inputGain")  return "INPUT";
        if (param == "outputGain") return "LEVEL";
        if (param == "modDepth")   return "DEPTH";
        if (param == "modRate")    return "RATE";
        if (param == "predelay")   return "PREDELAY";
        if (param == "size")       return "SIZE";
        if (param == "feedback")   return "FEEDBACK";
        if (param == "lo")         return "LO";
        if (param == "hi")         return "HI";
        if (param == "resonance")  return "RESONANCE";
        if (param == "mix")        return "MIX";
        return "GRAVITY";
    }
}

// ---- the plate ----------------------------------------------------------------------

class InfiniverbEditor::Panel : public juce::Component
{
public:
    Panel() : artwork (asset (BinaryData::infiniverb_panel_2x_png, BinaryData::infiniverb_panel_2x_pngSize)),
              legs (asset (BinaryData::infiniverb_legs_2x_png, BinaryData::infiniverb_legs_2x_pngSize))
    {
        setSize ((int) std::ceil (kWidth), (int) std::ceil (kWindowHeight));
        setOpaque (true);
    }

    void paint (juce::Graphics& g) override
    {
        // Dark behind the legs: a plugin window cannot be transparent, and the
        // author did not want the design's grey floor (2026-10-02).
        g.fillAll (juce::Colour (0xff111111));
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

        // The legs first, so the device's bottom edge overlaps their strip.
        g.drawImage (legs, { 0.0f, kLegsTop, kWidth, kLegsHeight });
        g.drawImage (artwork, { 0.0f, 0.0f, kWidth, kHeight });
    }

    const juce::Image artwork;   // exported at 2x
    const juce::Image legs;      // exported at 2x, transparent around the feet
};

// ---- knobs: invisible hit areas -----------------------------------------------------
//
// A knob here is only the circle it can be grabbed by; what it looks like is the
// artwork's body plus a pointer sprite drawn by Pointers.

class InfiniverbEditor::Knob : public juce::Slider
{
public:
    explicit Knob (const KnobSpec& s) : juce::Slider (RotaryVerticalDrag, NoTextBox), spec (s)
    {
        setBounds (juce::Rectangle<float> (s.hitRadius * 2.0f, s.hitRadius * 2.0f).withCentre (s.hitCentre).toNearestInt());
        setPopupDisplayEnabled (false, false, nullptr);
        setMouseDragSensitivity (s.type == KnobType::Gravity ? 360 : 240);
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }

    void paint (juce::Graphics&) override {}

    bool hitTest (int x, int y) override
    {
        const auto c = getLocalBounds().toFloat().getCentre();
        return c.getDistanceFrom ({ (float) x, (float) y }) <= spec.hitRadius;
    }

    // Gravity's centre detent: near zero, it is zero.
    double snapValue (double attempted, DragMode mode) override
    {
        if (spec.type == KnobType::Gravity && mode != notDragging && std::abs (attempted) < 3.0)
            return 0.0;
        return attempted;
    }

    const KnobSpec spec;
};

// ---- pointers ------------------------------------------------------------------------

class InfiniverbEditor::Pointers : public juce::Component
{
public:
    Pointers()
        : small   (asset (BinaryData::pointer_small_2x_png,   BinaryData::pointer_small_2x_pngSize)),
          medium  (asset (BinaryData::pointer_medium_2x_png,  BinaryData::pointer_medium_2x_pngSize)),
          gravity (asset (BinaryData::pointer_gravity_2x_png, BinaryData::pointer_gravity_2x_pngSize)),
          ledOn   (asset (BinaryData::led_on_2x_png,          BinaryData::led_on_2x_pngSize))
    {
        setInterceptsMouseClicks (false, false);
    }

    void add (Knob* k) { knobs.push_back (k); }

    void setBypassed (bool b)
    {
        if (b != bypassed) { bypassed = b; repaint (kBypassLed.expanded (20.0f).toNearestInt()); }
    }

    void repaintKnob (const Knob& k)
    {
        const auto& img = spriteFor (k.spec.type);
        const float half = (float) img.getWidth() * 0.25f + 2.0f;   // @2x sprite, drawn at half size
        repaint (juce::Rectangle<float> (half * 2.0f, half * 2.0f).withCentre (k.spec.centre).toNearestInt());
    }

    void paint (juce::Graphics& g) override
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

        for (auto* k : knobs)
        {
            // -150 deg + 300 deg x the normalised (skewed) position, clockwise from up.
            const double proportion = k->valueToProportionOfLength (k->getValue());
            const float angle = juce::degreesToRadians (kPointerStartDegrees + kPointerSweepDegrees * (float) proportion);
            const auto& img = spriteFor (k->spec.type);

            g.drawImageTransformed (img, juce::AffineTransform::translation (-(float) img.getWidth() * 0.5f, -(float) img.getHeight() * 0.5f)
                                             .scaled (0.5f)
                                             .rotated (angle)
                                             .translated (k->spec.centre));
        }

        if (bypassed)
        {
            const auto c = kBypassLed.getCentre();
            const float w = (float) ledOn.getWidth() * 0.5f, h = (float) ledOn.getHeight() * 0.5f;
            g.drawImage (ledOn, juce::Rectangle<float> (w, h).withCentre (c));
        }
    }

private:
    const juce::Image& spriteFor (KnobType t) const
    {
        return t == KnobType::Gravity ? gravity : (t == KnobType::Medium ? medium : small);
    }

    const juce::Image small, medium, gravity, ledOn;
    std::vector<Knob*> knobs;
    bool bypassed = false;
};

// ---- push buttons ----------------------------------------------------------------------
//
// The faces are in the artwork. Pressed — or latched on — the face is redrawn
// 1 px lower and 15 % darker, as the design specifies.

class InfiniverbEditor::PushButton : public juce::Button
{
public:
    PushButton (const juce::String& name, juce::Rectangle<float> r, const juce::Image& art, bool latching)
        : juce::Button (name), rect (r), artwork (art)
    {
        setClickingTogglesState (latching);
        setBounds (rect.toNearestInt());
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool, bool down) override
    {
        if (! (down || getToggleState()))
            return;

        const auto origin = rect.getPosition() - getPosition().toFloat();
        g.reduceClipRegion (juce::Rectangle<float> (origin.x, origin.y, rect.getWidth(), rect.getHeight()).toNearestInt());
        g.drawImageTransformed (artwork, juce::AffineTransform::scale (0.5f)
                                             .translated (-rect.getX() + origin.x, -rect.getY() + origin.y + 1.0f));
        g.fillAll (juce::Colours::black.withAlpha (0.15f));
    }

private:
    juce::Rectangle<float> rect;
    const juce::Image& artwork;
};

// ---- the editor ------------------------------------------------------------------------

InfiniverbEditor::InfiniverbEditor (Rvb1Processor& p, juce::AudioProcessorValueTreeState& state)
    : juce::AudioProcessorEditor (p), owner (p), apvts (state)
{
    panel = std::make_unique<Panel>();
    addAndMakeVisible (*panel);

    pointers = std::make_unique<Pointers>();
    pointers->setBounds (panel->getLocalBounds());

    for (const auto& spec : kKnobs)
    {
        auto knob = std::make_unique<Knob> (spec);
        auto* raw = knob.get();

        if (auto* param = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (spec.param)))
            raw->setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));

        panel->addAndMakeVisible (*raw);
        knobAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, spec.param, *raw));
        raw->onValueChange = [this, raw]
        {
            pointers->repaintKnob (*raw);

            // Turned by hand (dragged, or the wheel under the pointer): show it.
            if (raw->isMouseButtonDown() || raw->isMouseOver())
            {
                focusParam = raw->spec.param;
                focusTime = juce::Time::getMillisecondCounter();
            }
        };
        pointers->add (raw);
        knobs.push_back (std::move (knob));
    }

    panel->addAndMakeVisible (*pointers);

    kill       = std::make_unique<PushButton> ("Kill",   kButtonKill,       panel->artwork, true);
    bypass     = std::make_unique<PushButton> ("Bypass", kButtonBypass,     panel->artwork, true);
    presetDown = std::make_unique<PushButton> ("Preset down", kButtonPresetDown, panel->artwork, false);
    presetUp   = std::make_unique<PushButton> ("Preset up",   kButtonPresetUp,   panel->artwork, false);

    for (auto* b : { kill.get(), bypass.get(), presetDown.get(), presetUp.get() })
        panel->addAndMakeVisible (*b);

    killAttachment   = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "kill", *kill);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "bypass", *bypass);
    presetDown->onClick = [this] { owner.presets.step (-1); };
    presetUp->onClick   = [this] { owner.presets.step (+1); };

    // HotSwitch: A and B are a two-position selector. The selected one is shown
    // pressed; the switching itself is the processor's (presets/HotSwitch).
    hotA = std::make_unique<PushButton> ("HotSwitch A", kButtonHotSwitchA, panel->artwork, false);
    hotB = std::make_unique<PushButton> ("HotSwitch B", kButtonHotSwitchB, panel->artwork, false);
    panel->addAndMakeVisible (*hotA);
    panel->addAndMakeVisible (*hotB);
    hotA->onClick = [this] { selectSlot (false); };
    hotB->onClick = [this] { selectSlot (true); };

    panel->addAndMakeVisible (display);
    browser = std::make_unique<PresetBrowserOverlay> (owner.presets);
    browser->setBounds (panel->getLocalBounds());
    panel->addChildComponent (*browser);
    display.onClick = [this] { browser->open(); };

    setResizable (false, false);
    setSize (juce::roundToInt (kWidth * kScale), juce::roundToInt (kWindowHeight * kScale));

    timerCallback();
    startTimerHz (30);
}

InfiniverbEditor::~InfiniverbEditor() = default;

void InfiniverbEditor::resized()
{
    panel->setTransform (juce::AffineTransform::scale ((float) getWidth() / kWidth));
}

Vfd::State InfiniverbEditor::displayState() const
{
    Vfd::State s;
    const float gravity = apvts.getRawParameterValue ("gravity")->load();
    const float size    = apvts.getRawParameterValue ("size")->load() * 0.01f;
    const float predelayMs = apvts.getRawParameterValue ("predelay")->load();
    const auto t = rvb1::mapGravity (gravity);

    s.preset = owner.presets.currentLabel();

    if (focusParam.isNotEmpty() && focusParam != "gravity"
        && juce::Time::getMillisecondCounter() - focusTime < kFocusMs)
    {
        s.focusLabel = knobLabel (focusParam);
        s.focusValue = formatValue (focusParam, apvts.getRawParameterValue (focusParam)->load());
    }
    s.gravity = juce::roundToInt (gravity);
    s.decaySeconds = t.decaySeconds;
    s.kill = apvts.getRawParameterValue ("kill")->load() > 0.5f;
    s.onB  = apvts.getRawParameterValue ("hotswitch")->load() > 0.5f;
    s.dim  = apvts.getRawParameterValue ("bypass")->load() > 0.5f;

    // The envelope the settings produce, from the same mapping and Size scaling
    // the engine uses: predelay gap, onset, decay — or a rising swell and its
    // fall when Gravity is negative. Six seconds across, linear amplitude.
    const double reference = rvb1::DelayNetwork::sizeSecondsFor (0.3f);
    const float window = std::min ((float) rvb1::RisingEnvelope::kMaxWindowSeconds,
                                   t.envelopeWindow * (float) std::sqrt (rvb1::DelayNetwork::sizeSecondsFor (size) / reference));
    constexpr float span = 6.0f;
    std::array<float, Vfd::kEnvelopeCols> level {};
    float peak = 1.0e-9f;

    for (int c = 0; c < Vfd::kEnvelopeCols; ++c)
    {
        const float time = span * ((float) c + 0.5f) / (float) Vfd::kEnvelopeCols - predelayMs * 0.001f;
        float a = 0.0f;
        if (time >= 0.0f)
        {
            const float decay = std::pow (10.0f, -3.0f * time / t.decaySeconds);
            const float rise  = time < window ? std::pow (time / window, t.envelopeSteepness)
                                              : std::pow (10.0f, -3.0f * (time - window) / t.decaySeconds);
            a = (1.0f - t.envelopeAmount) * decay + t.envelopeAmount * rise;
        }
        level[(size_t) c] = a;
        peak = std::max (peak, a);
    }

    for (int c = 0; c < Vfd::kEnvelopeCols; ++c)
    {
        const float a = level[(size_t) c] / peak;
        s.envelope[(size_t) c] = a <= 0.005f ? 0 : juce::jlimit (1, Vfd::kEnvelopeRows, juce::roundToInt (a * Vfd::kEnvelopeRows));
    }

    return s;
}

// The value with its unit, as the display shows it. Gains are signed; Lo and Hi
// are shown as the gain their shelf applies, the same mapping the processor uses
// (-24 dB at the bottom, flat at the centre, +6 dB at the top).
juce::String InfiniverbEditor::formatValue (const juce::String& param, float v)
{
    auto signedText = [] (float x, int decimals)
    {
        const auto t = juce::String (std::abs (x), decimals);
        const bool zero = t.getDoubleValue() == 0.0;
        return (zero ? juce::String() : (x > 0.0f ? "+" : "-")) + t;
    };

    if (param == "inputGain" || param == "outputGain")
        return signedText ((float) juce::roundToInt (v), 0) + "dB";

    if (param == "lo" || param == "hi")
    {
        const float norm = juce::jlimit (0.0f, 2.0f, v * 0.01f);
        const float db = norm <= 1.0f ? -24.0f * (1.0f - norm) : 6.0f * (norm - 1.0f);
        return signedText (db, 1) + "dB";
    }

    if (param == "predelay")
        return juce::String (juce::roundToInt (v)) + "ms";

    return juce::String (juce::roundToInt (v)) + "%";
}

void InfiniverbEditor::selectSlot (bool b)
{
    if (auto* p = apvts.getParameter ("hotswitch"))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (b ? 1.0f : 0.0f);
        p->endChangeGesture();
    }
}

void InfiniverbEditor::timerCallback()
{
    const bool onB = apvts.getRawParameterValue ("hotswitch")->load() > 0.5f;
    hotA->setToggleState (! onB, juce::dontSendNotification);
    hotB->setToggleState (onB, juce::dontSendNotification);

    display.setState (displayState());
    pointers->setBypassed (apvts.getRawParameterValue ("bypass")->load() > 0.5f);
}
