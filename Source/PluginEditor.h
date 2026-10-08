#pragma once

#include "PluginProcessor.h"

// Simple view: one big knob per macro with the current zone's name under it.
// "Show parameters" swaps in the advanced view: every parameter in the
// Advanced group (e.g. stereo width, which can narrow below 100%).
class SoundSuiteEditor : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit SoundSuiteEditor (SoundSuiteProcessor&);
    ~SoundSuiteEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setAdvancedVisible (bool shouldShow);

    struct MacroKnob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
        juce::Label  title, zone;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        std::size_t macro = 0;   // index into silo::macros
    };

    struct AdvancedRow
    {
        juce::Label  title;
        juce::Slider slider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    SoundSuiteProcessor& processor;

    std::vector<std::unique_ptr<MacroKnob>>   macros;   // one per silo::macros entry
    std::vector<std::unique_ptr<AdvancedRow>> advanced;
    juce::ToggleButton                        showParameters { "Show parameters" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundSuiteEditor)
};
