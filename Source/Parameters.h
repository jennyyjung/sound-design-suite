#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "macros/MacroRegistry.h"

namespace silo
{
inline juce::String toString (std::string_view s) { return juce::String (s.data(), s.size()); }

// Macros (the simple view): one host parameter each, 0..1, zero = bypass.
// IDs come from silo::macros in MacroRegistry.h.
inline juce::ParameterID macroParameterID (std::size_t index) { return { toString (macros[index].id), 1 }; }
}

namespace silo::ids
{
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

    auto macroGroup = std::make_unique<AudioProcessorParameterGroup> ("macros", "Macros", "|");
    for (std::size_t i = 0; i < numMacros; ++i)
        macroGroup->addChild (std::make_unique<AudioParameterFloat> (macroParameterID (i), toString (macros[i].name),
                                                                     NormalisableRange<float> (0.0f, 1.0f), 0.0f));

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
    layout.add (std::move (macroGroup), std::move (advanced));
    return layout;
}
}
