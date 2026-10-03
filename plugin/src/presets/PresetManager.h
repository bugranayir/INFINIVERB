#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

// Presets: a factory bank compiled in, a user bank on disk, and favourites.
//
// User presets are small XML files under the platform's audio-presets folder
// (~/Library/Audio/Presets/fatbird Studios/INFINIVERB on macOS), one folder per
// category, so they can be backed up or shared by hand. A preset stores every
// parameter that makes up a sound — not Bypass, Kill, HotSwitch or Freeze.
//
// The factory bank is compiled in from presets/factory/: Everyday Space — the
// author's everyday setting and the default for a new instance — then three
// sounds per category, then Init.
class PresetManager
{
public:
    struct Preset
    {
        juce::String name, category;
        bool factory = false;
        juce::File file;               // user presets only
        juce::ValueTree values;        // factory presets only: PARAM children

        juce::String id() const { return (factory ? "factory/" : "user/") + category + "/" + name; }
    };

    explicit PresetManager (juce::AudioProcessorValueTreeState&);

    static const juce::StringArray& categories();

    // Factory first, then user, in a stable order. A preset's number is its
    // position here, from 1.
    const std::vector<Preset>& all() const noexcept { return presets; }
    void rescan();

    int currentIndex() const;
    juce::String currentLabel() const;   // "01 INIT", for the display

    bool load (int index);
    void step (int delta);               // previous / next, wrapping

    // Returns the saved preset's index, or -1. Overwrites a user preset of the
    // same name and category.
    int saveUser (const juce::String& name, const juce::String& category);
    bool renameUser (int index, const juce::String& newName);
    bool deleteUser (int index);

    bool isFavourite (const Preset&) const;
    void setFavourite (const Preset&, bool);

    juce::File userDirectory() const;

private:
    void loadFavourites();
    void saveFavourites() const;
    juce::ValueTree captureValues() const;
    void applyValues (const juce::ValueTree&);
    void setCurrent (const Preset&);

    juce::AudioProcessorValueTreeState& apvts;
    std::vector<Preset> presets;
    juce::StringArray favourites;
};
