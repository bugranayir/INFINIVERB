#include "PluginProcessor.h"
#include "ui/InfiniverbEditor.h"

namespace
{
    constexpr float kSmoothingSeconds = 0.03f;   // 30 ms, within REVERB_SPEC.md 5.5's 20-50 ms
}

// ---------------------------------------------------------------------------
// Parameter tree
//
// Built by walking rvb1::definition(). The plugin never states a range or a
// default of its own — if a number needs changing it changes in the DSP core's
// table and the plugin follows.
// ---------------------------------------------------------------------------
juce::AudioProcessorValueTreeState::ParameterLayout Rvb1Processor::buildParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int i = 0; i < rvb1::numParams(); ++i)
    {
        const auto& d = rvb1::definition (static_cast<rvb1::Param> (i));

        // Version hint 1 for every parameter added in v1. Anything added later
        // must use a higher hint or AU hosts lose their automation on upgrade.
        const juce::ParameterID id { d.id, 1 };

        switch (d.kind)
        {
            case rvb1::ParamKind::Bool:
                layout.add (std::make_unique<juce::AudioParameterBool> (
                    id, d.name, d.defaultValue > 0.5f));
                break;

            case rvb1::ParamKind::Choice:
            {
                juce::StringArray items;
                for (int c = 0; c < d.numChoices; ++c)
                    items.add (d.choices[c]);

                layout.add (std::make_unique<juce::AudioParameterChoice> (
                    id, d.name, items, static_cast<int> (d.defaultValue)));
                break;
            }

            case rvb1::ParamKind::Float:
            {
                // Whole-number steps throughout, at the author's request:
                // fractional readouts made the panel hard to read during
                // listening tests and nothing here needs finer resolution.
                juce::NormalisableRange<float> range { d.minValue, d.maxValue, d.step };

                // Perceptual mapping (REVERB_SPEC.md 5.5). skewCentre is the
                // value that should sit at the middle of the knob's travel.
                if (d.skewCentre > d.minValue && d.skewCentre < d.maxValue)
                    range.setSkewForCentre (d.skewCentre);

                layout.add (std::make_unique<juce::AudioParameterFloat> (
                    id, d.name, range, d.defaultValue,
                    juce::AudioParameterFloatAttributes()
                        .withLabel (d.suffix)
                        .withStringFromValueFunction ([] (float v, int)
                        {
                            return juce::String (juce::roundToInt (v));
                        })));
                break;
            }
        }
    }

    // Bypass is host integration rather than a DSP parameter, so it is not in
    // the core table. Version hint 1 so it is present from the first release.
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "bypass", 1 }, "Bypass", false));

    return layout;
}

// ---------------------------------------------------------------------------

Rvb1Processor::Rvb1Processor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "RVB1", buildParameterLayout())
{
    for (int i = 0; i < rvb1::numParams(); ++i)
    {
        const auto& d = rvb1::definition (static_cast<rvb1::Param> (i));
        rawValues[static_cast<size_t> (i)] = apvts.getRawParameterValue (d.id);
        jassert (rawValues[static_cast<size_t> (i)] != nullptr);
    }

    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter ("bypass"));
    jassert (bypassParam != nullptr);
}

// Decision 4.11: mono -> stereo and stereo -> stereo. The output is always
// stereo because the wet signal is generated as a stereo pair (decision 4.2).
bool Rvb1Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    const auto in = layouts.getMainInputChannelSet();
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void Rvb1Processor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    sampleRateHz = sampleRate;

    // Allocated with headroom, and with a floor, so the chunking in
    // processBlock almost never has to engage — and cannot allocate if it does
    // (REVERB_SPEC.md 7).
    const int scratchSamples = juce::jmax (2048, maximumExpectedSamplesPerBlock * 2);

    wetBuffer.setSize (juce::jmax (2, getTotalNumOutputChannels()), scratchSamples, false, true, true);
    dryCopy.setSize (juce::jmax (2, getTotalNumOutputChannels()), scratchSamples, false, true, true);
    monoScratch.assign ((size_t) scratchSamples, 0.0f);

    engine.prepare (sampleRate);
    engine.reset();

    inputGain.prepare (sampleRate, kSmoothingSeconds);
    inputGain.setDecibels (raw (rvb1::Param::InputGain));
    inputGain.snapToTarget();

    outputStage.prepare (sampleRate, kSmoothingSeconds);
    outputStage.setMixPercent   (raw (rvb1::Param::Mix));
    outputStage.setOutputTrimDb (raw (rvb1::Param::OutputGain));
    outputStage.snapToTargets();   // start at the current settings, never at zero

    bypassFade.reset (sampleRate, 0.01);
    bypassFade.setCurrentAndTargetValue (bypassParam->get() ? 0.0f : 1.0f);
    engineIdle = false;

    // So the host has a real answer before the first block, not the placeholder.
    // Then everything to its settled value: a render starts right after this,
    // and must not open with the engine gliding in from its defaults.
    pushParametersToEngine();
    engine.snapToTargets();
}

// Everything the engine needs from the parameter tree, pushed once per block.
void Rvb1Processor::pushParametersToEngine() noexcept
{
    // Gravity is the decay control (decision 5.2). The mapping lives in the
    // core, so the tests measure the same curve the plugin plays.
    const auto gravity = rvb1::mapGravity (raw (rvb1::Param::Gravity));

    // Lo and Hi are 0-200 %, with 100 % flat, mapped so the travel is symmetric
    // in dB: 0 % is -24 dB, 100 % is flat, 200 % is +6 dB. The boost end is
    // deliberately much smaller, because inside the loop a boost buys very
    // little before the network's clamp takes it back.
    auto bandGain = [] (float percent)
    {
        const float norm = juce::jlimit (0.0f, 2.0f, percent * 0.01f);
        const float db   = norm <= 1.0f ? -24.0f * (1.0f - norm) : 6.0f * (norm - 1.0f);
        return std::pow (10.0f, db * 0.05f);
    };

    const float loGain = bandGain (raw (rvb1::Param::Lo));
    const float hiGain = bandGain (raw (rvb1::Param::Hi));

    engine.setSize            (juce::jlimit (0.0f, 1.0f, raw (rvb1::Param::Size) * 0.01f));
    engine.setDecaySeconds    (gravity.decaySeconds);
    engine.setEnvelope        (gravity.envelopeAmount, gravity.envelopeWindow, gravity.envelopeSteepness);
    engine.setPredelaySeconds (raw (rvb1::Param::Predelay) * 0.001f);
    engine.setGlobalFeedback  (juce::jlimit (0.0f, 1.0f, raw (rvb1::Param::Feedback) * 0.01f));
    engine.setModDepth        (juce::jlimit (0.0f, 1.0f, raw (rvb1::Param::ModDepth) * 0.01f));
    engine.setModRateHz       (0.1f + 4.9f * juce::jlimit (0.0f, 1.0f, raw (rvb1::Param::ModRate) * 0.01f));
    engine.setBandGains       (loGain, hiGain);
    engine.setResonance       (juce::jlimit (0.0f, 1.0f, raw (rvb1::Param::Resonance) * 0.01f), loGain, hiGain);
    engine.setKilled          (raw (rvb1::Param::Kill) > 0.5f);

    // The coefficient comes from Gravity: mechanism (a) of the negative half.
    engine.setDiffusionCoefficient (gravity.diffusion);

    outputStage.setMixPercent   (raw (rvb1::Param::Mix));
    outputStage.setOutputTrimDb (raw (rvb1::Param::OutputGain));

    tailSeconds.store (engine.getTailSeconds(), std::memory_order_relaxed);
}

// One chunk, guaranteed to fit the scratch buffers.
void Rvb1Processor::processChunk (juce::AudioBuffer<float>& buffer,
                                  int offset, int numSamples, int numOut) noexcept
{
    // Summed to mono before the structure (decision 4.2). The dry path keeps
    // its stereo image; only the tail is mono-derived.
    for (int i = 0; i < numSamples; ++i)
    {
        float mono = 0.0f;
        for (int ch = 0; ch < numOut; ++ch)
            mono += buffer.getSample (ch, offset + i);

        // A NaN or an infinity from upstream would circulate in the loop for
        // ever and silence the tail until the plugin was reloaded. The dry path
        // passes it on, as any plugin would; the reverb never takes it in.
        mono /= (float) juce::jmax (1, numOut);
        monoScratch[(size_t) i] = std::isfinite (mono) ? mono : 0.0f;
    }

    engine.process (monoScratch.data(),
                    wetBuffer.getWritePointer (0),
                    wetBuffer.getWritePointer (numOut > 1 ? 1 : 0),
                    numSamples);

    float* dry[2] { nullptr, nullptr };
    for (int ch = 0; ch < juce::jmin (2, numOut); ++ch)
        dry[ch] = buffer.getWritePointer (ch) + offset;

    outputStage.process (dry, wetBuffer.getArrayOfReadPointers(), juce::jmin (2, numOut), numSamples);
}

void Rvb1Processor::clearState() noexcept
{
    engine.reset();
    wetBuffer.clear();
    std::fill (monoScratch.begin(), monoScratch.end(), 0.0f);
}

void Rvb1Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n      = buffer.getNumSamples();

    // Mono input feeding a stereo output: duplicate first, so everything after
    // this point can treat all output channels the same way.
    if (numIn == 1)
    {
        for (int ch = 1; ch < numOut; ++ch)
            buffer.copyFrom (ch, 0, buffer, 0, 0, n);
    }
    else
    {
        for (int ch = numIn; ch < numOut; ++ch)
            buffer.clear (ch, 0, n);
    }

    // Bypass fades over 10 ms rather than switching: the processed signal and
    // the input differ, so a switch between them is a step. Once fully
    // bypassed the engine is cleared and idles, so coming back starts from
    // silence instead of resuming a tail frozen at the moment of bypass.
    const bool bypassed = bypassParam->get();
    bypassFade.setTargetValue (bypassed ? 0.0f : 1.0f);

    if (bypassed && ! bypassFade.isSmoothing())
    {
        if (! engineIdle)
        {
            clearState();
            engine.fadeInInput();
            engineIdle = true;
        }
        return;
    }

    engineIdle = false;
    const bool fading = bypassFade.isSmoothing();

    inputGain.setDecibels (raw (rvb1::Param::InputGain));
    pushParametersToEngine();

    // A clear request fades the wet path down over this block and then flushes
    // everything. The fade matters: dropping a ringing tail to zero in one
    // sample is a click, and a loud one at the levels these tests run at.
    if (clearRequested.exchange (false, std::memory_order_relaxed))
        clearFadeRemaining = n;

    // Chunked to whatever the scratch buffers were actually allocated for.
    //
    // The previous version wrote n samples into buffers sized exactly to the
    // block length the host declared at prepare time, and trusted the host to
    // never exceed it. Hosts do exceed it — around transport changes, during
    // freeze and bounce, when the device buffer changes underneath them. Every
    // symptom reported at the gate is what that produces: clicks that are
    // granular rather than tonal, land on either channel, differ on every
    // playback, and get worse the more wet signal there is to write.
    //
    // Chunking costs nothing and is correct whatever the host does.
    const int maxChunk = juce::jmax (1, wetBuffer.getNumSamples());

    for (int offset = 0; offset < n; offset += maxChunk)
    {
        const int len = juce::jmin (maxChunk, n - offset);

        if (fading)
            for (int ch = 0; ch < numOut; ++ch)
                dryCopy.copyFrom (ch, 0, buffer, ch, offset, len);

        // Input Gain scales everything, dry included (decision 3.6).
        float* chans[2] { nullptr, nullptr };
        for (int ch = 0; ch < juce::jmin (2, numOut); ++ch)
            chans[ch] = buffer.getWritePointer (ch) + offset;
        inputGain.process (chans, juce::jmin (2, numOut), len);

        processChunk (buffer, offset, len, numOut);

        if (fading)
            for (int i = 0; i < len; ++i)
            {
                const float g = bypassFade.getNextValue();
                for (int ch = 0; ch < numOut; ++ch)
                {
                    const float dry = dryCopy.getSample (ch, i);
                    buffer.setSample (ch, offset + i, dry + g * (buffer.getSample (ch, offset + i) - dry));
                }
            }
    }

    if (clearFadeRemaining > 0)
    {
        for (int ch = 0; ch < numOut; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
                d[i] *= 1.0f - (float) (i + 1) / (float) n;
        }

        engine.reset();
        clearFadeRemaining = 0;
    }
}

juce::AudioProcessorEditor* Rvb1Processor::createEditor()
{
    // The INFINIVERB panel (Figma hand-off in design/infiniverb/).
    return new InfiniverbEditor (*this, apvts);
}

// Session data comes from outside. A value that is not a number would pass
// straight into the parameters and from there into the reverb, so each one
// becomes its parameter's default. Top-level values are in real units; the
// HotSwitch slots below them hold normalised ones.
static void replaceNonFiniteValues (juce::ValueTree tree, juce::AudioProcessorValueTreeState& apvts, bool normalised)
{
    for (auto child : tree)
    {
        if (! child.hasType ("PARAM"))
        {
            replaceNonFiniteValues (child, apvts, true);
            continue;
        }

        if (std::isfinite ((float) child.getProperty ("value")))
            continue;

        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (child.getProperty ("id").toString())))
            child.setProperty ("value", normalised ? p->getDefaultValue() : p->convertFrom0to1 (p->getDefaultValue()), nullptr);
        else
            child.removeProperty ("value", nullptr);
    }
}

void Rvb1Processor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void Rvb1Processor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            replaceNonFiniteValues (tree, apvts, false);
            apvts.replaceState (tree);
            hotSwitch.syncToState();   // the restored switch is the slot in use, not a switch
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Rvb1Processor();
}
