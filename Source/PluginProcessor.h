#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "dsp/Clock.h"
#include "dsp/Module.h"
#include "dsp/Width.h"
#include "macros/MacroRegistry.h"
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

    // Modules run in series, so their tails and latencies add up.
    double getTailLengthSeconds() const override;
    int getChainLatencySamples() const;

    int getNumPrograms() override    { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() { return apvts; }

    // One zone table per macro, in silo::macros order.
    const silo::ZoneTableHolder& getZones (std::size_t macro) const { return *zones[macro]; }

    // Debug builds: re-read tuning/*.json from the source tree if it changed.
    // Called from the editor's timer (message thread).
    void reloadTuningIfChanged();

    // Saved sessions record each macro's tuning version. When a session saved
    // with an older tuning is loaded, it's listed here (for the UI to show) and
    // its knob positions go through silo::migrateMacroValue().
    struct TuningMismatch { juce::String macro; int savedVersion, currentVersion; };
    const std::vector<TuningMismatch>& getLastLoadMismatches() const { return lastLoadMismatches; }

    // Per-macro random seed for the dice button, saved with the session so a
    // rolled result comes back after reload. Message thread only.
    juce::int64 getDiceSeed (std::size_t macro) const;
    void setDiceSeed (std::size_t macro, juce::int64 seed);

    static constexpr int stateVersion = 2;

private:
    // The <Tuning> node under `root`, with a <Macro> per registered macro
    // stamped with its current table version (created if missing).
    juce::ValueTree getTuningState (juce::ValueTree root) const;

    std::array<std::unique_ptr<silo::ZoneTableHolder>, silo::numMacros> zones;
    juce::AudioProcessorValueTreeState apvts;

    std::array<std::atomic<float>*, silo::numMacros> macroValues {};
    std::atomic<float>* widthManual = nullptr;
    std::atomic<float>* widthXover  = nullptr;

    silo::Clock clock;
    silo::Width width;

    // Signal chain, in processing order. Add new modules here.
    std::array<const silo::Module*, 1> chain { &width };

    std::vector<TuningMismatch> lastLoadMismatches;

    const juce::Identifier widthId { "width" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundSuiteProcessor)
};
