#pragma once

#include <juce_graphics/juce_graphics.h>

// Every position on the INFINIVERB panel, in the design's own units.
//
// Copied from the design hand-off (spec.json, frame "INFINIVERB / Panel" >
// "device"). The panel is drawn in these units and scaled as one.
namespace infiniverb
{

constexpr float kWidth  = 1623.0f;
constexpr float kHeight = 422.5f;      // the device itself

// The legs, under the device: exported on their own from the design, transparent
// around them (infiniverb_legs_2x.png). The window's background shows between.
constexpr float kLegsTop      = 422.0f;
constexpr float kLegsHeight   = 40.0f;
constexpr float kWindowHeight = kLegsTop + kLegsHeight;

inline const juce::Colour kPhosphor  { 0xff3df5d0 };
inline const juce::Colour kVfdGlassColour { 0xff040807 };
constexpr float kGhostAlpha = 0.08f;

// Pointer angle = -150 + 300 x normalised value, clockwise from straight up.
constexpr float kPointerStartDegrees = -150.0f;
constexpr float kPointerSweepDegrees = 300.0f;

enum class KnobType { Small, Medium, Gravity };

struct KnobSpec
{
    const char* param;
    KnobType type;
    juce::Point<float> centre;       // where the pointer sprite rotates
    juce::Point<float> hitCentre;
    float hitRadius;
};

inline const KnobSpec kKnobs[] {
    { "inputGain",  KnobType::Small,   {  203.80f, 359.77f }, {  205.94f, 352.52f },  36.55f },
    { "predelay",   KnobType::Small,   {  548.36f, 359.77f }, {  550.50f, 352.52f },  36.55f },
    { "size",       KnobType::Medium,  {  819.45f, 150.74f }, {  815.00f, 144.07f },  66.81f },
    { "feedback",   KnobType::Medium,  { 1306.45f, 150.74f }, { 1302.00f, 144.07f },  66.81f },
    { "modDepth",   KnobType::Small,   {  696.18f, 359.77f }, {  698.32f, 352.52f },  36.55f },
    { "modRate",    KnobType::Small,   {  839.82f, 359.77f }, {  839.73f, 352.52f },  36.55f },
    { "lo",         KnobType::Small,   {  977.71f, 359.77f }, {  979.85f, 352.52f },  36.55f },
    { "hi",         KnobType::Small,   { 1121.35f, 359.77f }, { 1121.26f, 352.52f },  36.55f },
    { "resonance",  KnobType::Small,   { 1283.03f, 359.77f }, { 1285.17f, 352.52f },  36.55f },
    { "mix",        KnobType::Medium,  { 1504.45f, 150.74f }, { 1500.00f, 144.07f },  66.81f },
    { "outputGain", KnobType::Small,   { 1420.52f, 359.77f }, { 1420.43f, 352.52f },  36.55f },
    { "gravity",    KnobType::Gravity, { 1052.98f, 144.08f }, { 1052.98f, 144.08f }, 108.76f },
};

// Push buttons. Their faces are in the artwork; "on" redraws the face
// 1 px lower and 15 % darker.
inline const juce::Rectangle<float> kButtonKill       {  345.83f, 339.21f, 64.76f, 26.56f };
inline const juce::Rectangle<float> kButtonBypass     {   51.53f, 160.52f, 87.15f, 53.63f };
inline const juce::Rectangle<float> kButtonPresetDown {  338.00f, 225.36f, 73.16f, 21.01f };
inline const juce::Rectangle<float> kButtonPresetUp   {  420.83f, 225.36f, 73.16f, 21.01f };
inline const juce::Rectangle<float> kButtonHotSwitchA {  522.01f, 225.36f, 73.16f, 21.01f };
inline const juce::Rectangle<float> kButtonHotSwitchB {  604.84f, 225.36f, 73.16f, 21.01f };

inline const juce::Rectangle<float> kBypassLed { 193.58f, 181.65f, 61.36f, 11.37f };

// The VFD: content is drawn inside the glass, in glass-relative units.
inline const juce::Rectangle<float> kVfdGlass { 344.0f, 64.0f, 328.0f, 108.0f };

constexpr juce::Point<float> kBrowserSize { 820.0f, 400.0f };

} // namespace infiniverb
