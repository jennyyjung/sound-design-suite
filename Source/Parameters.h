#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace silo::ids
{
// Macros: the simple view. One host parameter each, 0..1, zero = bypass.
inline const juce::ParameterID gateChop   { "gate_chop", 1 };
inline const juce::ParameterID widthPan   { "width_pan", 1 };
inline const juce::ParameterID space      { "space",     1 };

// Advanced view: hand controls for users who want more than the macros.
inline const juce::ParameterID widthManual    { "width_manual", 1 };  // 0..200 %, 100 = unchanged; below 100 narrows
inline const juce::ParameterID widthCrossover { "width_xover",  1 };  // mono-below frequency

inline const juce::String advancedGroup { "advanced" };
}

namespace silo
{
inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;

    auto macros = std::make_unique<AudioProcessorParameterGroup> ("macros", "Macros", "|");
    auto addMacro = [&] (const ParameterID& id, const String& name)
    {
        macros->addChild (std::make_unique<AudioParameterFloat> (id, name, NormalisableRange<float> (0.0f, 1.0f), 0.0f));
    };
    addMacro (ids::gateChop, "Gate / Chop");
    addMacro (ids::widthPan, "Width / Auto-pan");
    addMacro (ids::space,    "Space");

    auto advanced = std::make_unique<AudioProcessorParameterGroup> (ids::advancedGroup, "Advanced", "|");
    advanced->addChild (std::make_unique<AudioParameterFloat> (
        ids::widthManual, "Stereo width",
        NormalisableRange<float> (0.0f, 200.0f, 0.1f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%")));
    advanced->addChild (std::make_unique<AudioParameterFloat> (
        ids::widthCrossover, "Mono below",
        NormalisableRange<float> (60.0f, 250.0f, 1.0f, 0.5f), 120.0f,
        AudioParameterFloatAttributes().withLabel ("Hz")));

    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::move (macros), std::move (advanced));
    return layout;
}
}
