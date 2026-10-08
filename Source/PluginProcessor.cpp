#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <BinaryData.h>

namespace
{
silo::ZoneTable loadBuiltInTable (const char* data, int size)
{
    juce::String error;
    auto table = silo::ZoneTable::fromJson (juce::String::fromUTF8 (data, size), &error);
    jassert (table.has_value());   // a shipped tuning file failed to parse: see `error`
    return std::move (*table);
}
}

SoundSuiteProcessor::SoundSuiteProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      gateChopZones (loadBuiltInTable (TuningData::gate_chop_json, TuningData::gate_chop_jsonSize)),
      widthPanZones (loadBuiltInTable (TuningData::width_pan_json, TuningData::width_pan_jsonSize)),
      spaceZones    (loadBuiltInTable (TuningData::space_json,     TuningData::space_jsonSize)),
      apvts (*this, nullptr, "SoundSuite", silo::createParameterLayout())
{
    gateChop    = apvts.getRawParameterValue (silo::ids::gateChop.getParamID());
    widthPan    = apvts.getRawParameterValue (silo::ids::widthPan.getParamID());
    space       = apvts.getRawParameterValue (silo::ids::space.getParamID());
    widthManual = apvts.getRawParameterValue (silo::ids::widthManual.getParamID());
    widthXover  = apvts.getRawParameterValue (silo::ids::widthCrossover.getParamID());
}

bool SoundSuiteProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SoundSuiteProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    clock.prepare (sampleRate);
    width.prepare (sampleRate, samplesPerBlock);
}

void SoundSuiteProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // Host transport -> clock. Not used by any module yet; Gate/Chop (build step 2)
    // is the first consumer.
    {
        double bpm = 120.0;
        std::optional<double> ppq;
        bool playing = false;

        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                bpm     = pos->getBpm().orFallback (120.0);
                playing = pos->getIsPlaying();
                if (auto p = pos->getPpqPosition()) ppq = *p;
            }

        clock.update (bpm, ppq, playing);
    }

    // Width / Auto-pan: the macro's zone width times the advanced-view width.
    // The macro only ever widens (zero is bypass); narrowing below 100% lives in
    // the advanced view. Auto-pan arrives in build step 3.
    const float macroWidth  = (float) widthPanZones.get().getNumber (widthId, widthPan->load(), 1.0);
    const float manualWidth = widthManual->load() / 100.0f;
    width.setCrossover (widthXover->load());
    width.setWidth (macroWidth * manualWidth);

    if (buffer.getNumChannels() >= 2)
        width.process (buffer.getWritePointer (0), buffer.getWritePointer (1), buffer.getNumSamples());

    // gate_chop and space are registered (stable IDs for automation and presets)
    // but not wired yet: build steps 2 and 4.
    juce::ignoreUnused (gateChop, space);
}

void SoundSuiteProcessor::reloadTuningIfChanged()
{
   #if JUCE_DEBUG && defined (SOUNDSUITE_TUNING_DIR)
    const juce::File dir (SOUNDSUITE_TUNING_DIR);
    juce::String error;

    for (auto [holder, name] : { std::pair { &gateChopZones, "gate_chop.json" },
                                 std::pair { &widthPanZones, "width_pan.json" },
                                 std::pair { &spaceZones,    "space.json" } })
    {
        holder->reloadIfChanged (dir.getChildFile (name), &error);
        if (error.isNotEmpty())
        {
            DBG ("Tuning reload failed for " << name << ": " << error);
            error.clear();
        }
    }
   #endif
}

void SoundSuiteProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("version", 1, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SoundSuiteProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SoundSuiteProcessor::createEditor()
{
    return new SoundSuiteEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SoundSuiteProcessor();
}
