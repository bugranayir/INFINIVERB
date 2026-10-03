# The JUCE wrapper: parameter tree, host integration, audio callback.
# All algorithm code lives in rvb1_dsp.

# Copying the built plug-in into the system's plug-in folders is for a
# developer's machine; CI turns it off (on Windows the folder needs admin).
option(RVB1_COPY_PLUGIN_AFTER_BUILD "Install the plug-in into the user's plug-in folders after building" ON)

juce_add_plugin(RVB1
    COMPANY_NAME                "fatbird Studios"
    BUNDLE_ID                   com.fatbirdstudios.rvb1
    PLUGIN_MANUFACTURER_CODE    Fbrd
    PLUGIN_CODE                 Rv01
    PRODUCT_NAME                "INFINIVERB"    # product name; RVB1 stays the code name

    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE     # the preset browser's search and Esc
    COPY_PLUGIN_AFTER_BUILD     ${RVB1_COPY_PLUGIN_AFTER_BUILD}

    # AU for Logic, GarageBand and MainStage, which load nothing else
    # (decision 6.2). The universal binary is RVB1_UNIVERSAL_BINARY. No
    # Standalone: the author's decision, 2026-10-03.
    FORMATS VST3 AU)

target_sources(RVB1 PRIVATE
    src/PluginProcessor.cpp
    src/presets/PresetManager.cpp
    src/presets/HotSwitch.cpp
    src/ui/InfiniverbEditor.cpp
    src/ui/PresetBrowser.cpp
    src/ui/Manual.cpp
    src/ui/Vfd.cpp)

# The panel's artwork, exported from the Figma design at 2x (design/infiniverb/):
# the static plate, the three pointer sprites and the lit bypass LED. The
# display's font, Space Mono Bold (SIL OFL), is embedded when its file is in
# assets/; until then the display falls back to Menlo Bold. The user manual,
# INFINIVERB-Manual.pdf, is rendered from docs/manual/ by scripts/build_manual.sh.
file(GLOB RVB1_ASSET_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/*.png"
                                             "${CMAKE_CURRENT_SOURCE_DIR}/assets/*.ttf"
                                             "${CMAKE_CURRENT_SOURCE_DIR}/assets/*.pdf")
juce_add_binary_data(RVB1Assets SOURCES ${RVB1_ASSET_FILES})
target_link_libraries(RVB1 PRIVATE RVB1Assets)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/SpaceMono-Bold.ttf")
    target_compile_definitions(RVB1 PRIVATE RVB1_HAS_SPACE_MONO=1)
endif()

# The factory bank: one .infiniverb file per preset (presets/factory/), the same
# format the user bank uses, so a sound saved in the plugin can be promoted to
# factory by copying its file here and giving it an order.
file(GLOB RVB1_FACTORY_PRESETS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/presets/factory/*.infiniverb")
juce_add_binary_data(RVB1FactoryPresets HEADER_NAME FactoryPresets.h NAMESPACE FactoryPresets
                     SOURCES ${RVB1_FACTORY_PRESETS})
target_link_libraries(RVB1 PRIVATE RVB1FactoryPresets)

target_compile_definitions(RVB1 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

# JUCE 8.0.12 has no splash screen at all, so there is nothing to disable and
# nothing to comply with. Noted because FatSat's Projucer project still carries
# the old displaySplashScreen setting.

target_link_libraries(RVB1
    PRIVATE
        rvb1_dsp
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Optional additions, built when present.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/CMakeLists.txt")
    add_subdirectory(tools)
endif()
