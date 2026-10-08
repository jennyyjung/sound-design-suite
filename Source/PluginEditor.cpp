#include "PluginEditor.h"

SoundSuiteEditor::SoundSuiteEditor (SoundSuiteProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    auto& state = processor.getState();

    const std::array<std::tuple<const juce::ParameterID*, const char*, const silo::ZoneTableHolder*>, 3> macroDefs {{
        { &silo::ids::gateChop, "Gate / Chop",      &processor.gateChopZones },
        { &silo::ids::widthPan, "Width / Auto-pan", &processor.widthPanZones },
        { &silo::ids::space,    "Space",            &processor.spaceZones },
    }};

    for (size_t i = 0; i < macros.size(); ++i)
    {
        auto& m = macros[i];
        auto [id, name, zones] = macroDefs[i];

        m.zones = zones;
        m.title.setText (name, juce::dontSendNotification);
        m.title.setJustificationType (juce::Justification::centred);
        m.zone.setJustificationType (juce::Justification::centred);
        m.zone.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        m.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id->getParamID(), m.slider);

        addAndMakeVisible (m.slider);
        addAndMakeVisible (m.title);
        addAndMakeVisible (m.zone);
    }

    // Advanced view: every parameter in the Advanced group, in declaration order.
    for (auto* node : processor.getParameterTree().getSubgroups (false))
    {
        if (node->getID() != silo::ids::advancedGroup)
            continue;

        for (auto* param : node->getParameters (true))
        {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param);
            if (ranged == nullptr)
                continue;

            auto row = std::make_unique<AdvancedRow>();
            row->title.setText (ranged->getName (64), juce::dontSendNotification);
            row->slider.setTextValueSuffix (" " + ranged->getLabel());
            row->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, ranged->getParameterID(), row->slider);
            addChildComponent (row->title);
            addChildComponent (row->slider);
            advanced.push_back (std::move (row));
        }
    }

    showParameters.onClick = [this] { setAdvancedVisible (showParameters.getToggleState()); };
    addAndMakeVisible (showParameters);

    setSize (560, 300);
    timerCallback();
    startTimerHz (15);
}

void SoundSuiteEditor::setAdvancedVisible (bool shouldShow)
{
    for (auto& row : advanced)
    {
        row->title.setVisible (shouldShow);
        row->slider.setVisible (shouldShow);
    }
    setSize (560, shouldShow ? 300 + 36 * (int) advanced.size() + 16 : 300);
}

void SoundSuiteEditor::timerCallback()
{
    processor.reloadTuningIfChanged();

    for (auto& m : macros)
        m.zone.setText (m.zones->get().getZoneName ((float) m.slider.getValue()), juce::dontSendNotification);
}

void SoundSuiteEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1c1d21));
}

void SoundSuiteEditor::resized()
{
    auto area = getLocalBounds().reduced (16);

    auto top = area.removeFromTop (240);
    const int knobWidth = top.getWidth() / (int) macros.size();

    for (auto& m : macros)
    {
        auto column = top.removeFromLeft (knobWidth).reduced (8, 0);
        m.title.setBounds (column.removeFromTop (24));
        m.zone.setBounds (column.removeFromBottom (24));
        m.slider.setBounds (column);
    }

    showParameters.setBounds (area.removeFromTop (28).removeFromLeft (160));

    area.removeFromTop (8);
    for (auto& row : advanced)
    {
        auto line = area.removeFromTop (36);
        row->title.setBounds (line.removeFromLeft (140));
        row->slider.setBounds (line);
    }
}
