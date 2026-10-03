#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>

#include "../presets/PresetManager.h"

// The preset browser, opened by clicking the display (the Figma component
// "INFINIVERB / Preset Browser"): Bank / Category / Preset columns, search,
// a favourites filter, Save, and New / Rename / Delete for the user bank.
//
// Lives in an overlay that dims the panel to 55 % black; Esc, Cancel, the
// close button or a click outside the browser closes it.
class PresetBrowserOverlay : public juce::Component
{
public:
    explicit PresetBrowserOverlay (PresetManager&);
    ~PresetBrowserOverlay() override;

    void open();
    void close();
    std::function<void()> onPresetChanged;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    class Browser;
    std::unique_ptr<Browser> browser;
};
