#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <limits>
#include <vector>

#include "rvb1/Parameters.h"
#include "rvb1/OutputStage.h"
#include "rvb1/GainStage.h"
#include "rvb1/ReverbEngine.h"
#include "rvb1/Gravity.h"
#include "presets/PresetManager.h"
#include "presets/HotSwitch.h"

// The JUCE side of the plugin: parameter tree, host hooks, and the audio
// callback. It supplies values and buffers; every piece of DSP, including the
// wet/dry balance, lives in the rvb1_dsp core, independent of JUCE.
class Rvb1Processor : public juce::AudioProcessor
{
public:
    Rvb1Processor();
    ~Rvb1Processor() override = default;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    // Every hook the host might use to say "your state is stale" clears it.
    //
    // Which of these a given host calls is not ours to choose, so all of them
    // clear. What they cannot do is clear a tail the host never tells us about.
    // Moving a plugin between tracks in Ableton carries its tail along, and
    // Ableton's own reverbs do exactly the same — the software version of
    // re-patching a hardware unit mid-note.
    void releaseResources() override { clearState(); }
    void reset() override             { clearState(); }
    void numChannelsChanged() override        { clearState(); }
    void processorLayoutsChanged() override   { clearState(); }
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    // How long we keep sounding after the input stops. Hosts use it to decide
    // when a silent plugin may be put to sleep, and how much to add to the end
    // of a render. Computed by the engine from the current settings; most hosts
    // read it when playback starts, not continuously.
    double getTailLengthSeconds() const override { return tailSeconds.load (std::memory_order_relaxed); }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    // Presets are the plugin's own (PresetManager); the host sees one program,
    // and it must have a name — Steinberg's validator fails an empty one.
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Lets the host drive bypass properly rather than guessing.
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorValueTreeState apvts;

    // Built after the parameter tree it reads and writes. The current preset is
    // kept as a property of that tree, so it is saved with the session.
    PresetManager presets { apvts };
    HotSwitch hotSwitch { apvts };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout buildParameterLayout();

    void clearState() noexcept;
    void pushParametersToEngine() noexcept;
    void processChunk (juce::AudioBuffer<float>&, int offset, int numSamples, int numOut) noexcept;

    float raw (rvb1::Param p) const noexcept
    {
        return rawValues[static_cast<size_t> (p)]->load (std::memory_order_relaxed);
    }

    // Cached atomic pointers, resolved once at construction. Looking parameters
    // up by string inside processBlock would allocate and lock.
    std::array<std::atomic<float>*, static_cast<size_t> (rvb1::Param::Count)> rawValues {};
    juce::AudioParameterBool* bypassParam = nullptr;

    // Wet/dry balance, Kill and trim all live in the DSP core, where they can be
    // verified without a host. The plugin only supplies values.
    rvb1::GainStage   inputGain;
    rvb1::OutputStage outputStage;

    // Phase 1 filled this with a copy of the input. Phase 2 is filling it with
    // the reverb network as that network is built up.
    juce::AudioBuffer<float> wetBuffer;

    // The whole reverb, including the predelay and the global feedback loop.
    // All routing lives in the core (decision 4.8, 4.9).
    rvb1::ReverbEngine engine;

    // Mono-summed input, built per block and handed to the engine.
    std::vector<float> monoScratch;

    // Bypass: 1 processed, 0 bypassed, faded between. The input is kept here
    // while a fade runs, since the processed path overwrites it.
    juce::SmoothedValue<float> bypassFade { 1.0f };
    juce::AudioBuffer<float> dryCopy;
    bool engineIdle = false;

    double sampleRateHz = 48000.0;

public:
    // Flushes every buffer so the next listening test starts from silence.
    // A utility, not a parameter: Kill mutes the input and lets the tail ring,
    // which is what Kill is for (decision 4.12), but a voicing session needs
    // the tail gone, not sustained.
    void requestClear() noexcept { clearRequested.store (true, std::memory_order_relaxed); }

private:
    std::atomic<bool> clearRequested { false };

    // Written on the audio thread after each parameter push, read by the host
    // from wherever it asks. Starts pessimistic until the first push.
    std::atomic<double> tailSeconds { std::numeric_limits<double>::infinity() };
    int clearFadeRemaining = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Rvb1Processor)
};
