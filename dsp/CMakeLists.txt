# The reverb algorithm, with no framework dependency of any kind.
#
# REVERB_SPEC.md 7 asks for this separation, so the DSP can be built and
# measured without a host.
#
# Nothing in this target may include a JUCE header.

add_library(rvb1_dsp STATIC
    src/Parameters.cpp
    src/Gravity.cpp
    src/OutputStage.cpp
    src/DelayNetwork.cpp
    src/ReverbEngine.cpp)

target_include_directories(rvb1_dsp PUBLIC include)
target_compile_features(rvb1_dsp PUBLIC cxx_std_17)
