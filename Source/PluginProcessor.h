#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "dsp/Clock.h"
#include "dsp/Width.h"
#include "macros/ZoneTable.h"

class SoundSuiteProcessor : public juce::AudioProcessor
{
public:
    SoundSuiteProcessor();
    ~SoundSuiteProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override    { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() { return apvts; }

    // Zone tables, one per macro. Read from the UI for zone names.
    silo::ZoneTableHolder gateChopZones, widthPanZones, spaceZones;

    // Debug builds: re-read tuning/*.json from the source tree if it changed.
    // Called from the editor's timer (message thread).
    void reloadTuningIfChanged();

private:
    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float>* gateChop    = nullptr;
    std::atomic<float>* widthPan    = nullptr;
    std::atomic<float>* space       = nullptr;
    std::atomic<float>* widthManual = nullptr;
    std::atomic<float>* widthXover  = nullptr;

    silo::Clock clock;
    silo::Width width;

    const juce::Identifier widthId { "width" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundSuiteProcessor)
};
