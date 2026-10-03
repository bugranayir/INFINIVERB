#pragma once

// The user manual: a PDF rendered from docs/manual/ by scripts/build_manual.sh
// and embedded at build time, so it is always the one for this build. Opening
// it writes it beside the plug-in's application data and hands it to the
// system's PDF viewer. Returns false if it could not be written or opened.
bool openManual();

// The writing half of openManual: the file it opens, or an empty File if it
// could not be written. Separate so it can be checked without opening a viewer.
#include <juce_core/juce_core.h>
juce::File writeManual();
