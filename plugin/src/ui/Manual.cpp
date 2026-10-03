#include "Manual.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include "BinaryData.h"

juce::File writeManual()
{
    const juce::MemoryBlock pdf (BinaryData::INFINIVERBManual_pdf, (size_t) BinaryData::INFINIVERBManual_pdfSize);

    // Rewritten only when it differs, so an open copy is not replaced under
    // the viewer for nothing.
    auto writeInto = [&pdf] (const juce::File& dir)
    {
        if (! dir.createDirectory())
            return juce::File();

        const auto file = dir.getChildFile ("INFINIVERB Manual.pdf");
        juce::MemoryBlock existing;
        if (file.existsAsFile() && file.loadFileAsData (existing) && existing == pdf)
            return file;

        return file.replaceWithData (pdf.getData(), pdf.getSize()) ? file : juce::File();
    };

   #if JUCE_MAC
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                          .getChildFile ("Library/Application Support/fatbird Studios/INFINIVERB");
   #else
    const auto home = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                          .getChildFile ("fatbird Studios/INFINIVERB");
   #endif

    auto file = writeInto (home);
    if (file == juce::File())     // a host that may not write there: the temporary folder instead
        file = writeInto (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("INFINIVERB"));

    return file;
}

bool openManual()
{
    const auto file = writeManual();
    return file != juce::File() && file.startAsProcess();
}
