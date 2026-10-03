cmake_minimum_required(VERSION 3.22)

# The oldest macOS the plug-in loads on. Left unset, Xcode targets the system
# it builds on — which made 0.1.0-beta1 load on macOS 26 only. 10.13 is the
# oldest the current Xcode deploys to; the arm64 slice is 11.0 regardless.
# Must be set before project().
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version" FORCE)
endif()

# RVB1 — working name. See PROJECT_DECISIONS.md for the product name decision.
# C is required as well as C++: JUCE compiles C sources, and on macOS it also
# needs the Objective-C++ toolchain for the CoreAudio and AppKit wrappers.
project(RVB1 VERSION 0.1.0 LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJC)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Universal binary is a release concern (decision 6.2). Building both slices
# doubles compile time, so development builds stay native.
option(RVB1_UNIVERSAL_BINARY "Build macOS arm64 + x86_64" OFF)
if(APPLE AND RVB1_UNIVERSAL_BINARY)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# JUCE
#
# Decision 6.1 asks for a pinned version so builds are reproducible. FetchContent
# at a fixed tag is that guarantee and is what CI will use. A local checkout is
# accepted as a development convenience — it avoids re-downloading ~200 MB — but
# it is the developer's responsibility to keep it at the pinned version.
# ---------------------------------------------------------------------------
set(RVB1_JUCE_VERSION "8.0.12")
# The commit that tag points at. A tag can be moved; a commit cannot, so a
# fetched JUCE is checked against it before anything is built with it.
set(RVB1_JUCE_COMMIT "29396c22c93392d6738e021b83196283d6e4d850")
set(RVB1_JUCE_DIR "" CACHE PATH "Local JUCE checkout. Leave empty to fetch the pinned version.")

if(RVB1_JUCE_DIR)
    message(STATUS "RVB1: using JUCE from ${RVB1_JUCE_DIR}")
    add_subdirectory(${RVB1_JUCE_DIR} ${CMAKE_BINARY_DIR}/juce EXCLUDE_FROM_ALL)
elseif(EXISTS "/Library/JUCE/CMakeLists.txt")
    message(STATUS "RVB1: using the system JUCE at /Library/JUCE "
                   "(expected ${RVB1_JUCE_VERSION}; set RVB1_JUCE_DIR to override)")
    add_subdirectory(/Library/JUCE ${CMAKE_BINARY_DIR}/juce EXCLUDE_FROM_ALL)
else()
    message(STATUS "RVB1: fetching JUCE ${RVB1_JUCE_VERSION}")
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${RVB1_JUCE_VERSION}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)

    find_package(Git REQUIRED)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
                    WORKING_DIRECTORY "${juce_SOURCE_DIR}"
                    OUTPUT_VARIABLE RVB1_JUCE_FETCHED OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT RVB1_JUCE_FETCHED STREQUAL RVB1_JUCE_COMMIT)
        message(FATAL_ERROR "JUCE ${RVB1_JUCE_VERSION} was fetched at ${RVB1_JUCE_FETCHED}, "
                            "not at the pinned commit ${RVB1_JUCE_COMMIT}. Refusing to build.")
    endif()
endif()

add_subdirectory(dsp)
add_subdirectory(plugin)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()
