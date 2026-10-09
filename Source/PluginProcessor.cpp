#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "macros/TuningMigrations.h"

#include <BinaryData.h>

namespace
{
// The compiled-in tuning file for a macro: tuning/<id>.json.
silo::ZoneTable loadBuiltInTable (std::string_view macroId)
{
    const auto fileName = silo::toString (macroId) + ".json";

    for (int i = 0; i < TuningData::namedResourceListSize; ++i)
    {
        if (fileName != TuningData::originalFilenames[i])
            continue;

        int size = 0;
        const char* data = TuningData::getNamedResource (TuningData::namedResourceList[i], size);

        juce::String error;
        if (auto table = silo::ZoneTable::fromJson (juce::String::fromUTF8 (data, size), &error))
            return std::move (*table);

        jassertfalse;   // a shipped tuning file failed to parse: see `error`
        break;
    }

    jassertfalse;       // no tuning/<id>.json for a registered macro
    return *silo::ZoneTable::fromJson (R"({"version": 1, "anchors": [{"at": 0.0, "name": "Off"}]})");
}

const juce::Identifier tuningType  { "Tuning" };
const juce::Identifier macroType   { "Macro" };
const juce::Identifier idProp      { "id" };
const juce::Identifier versionProp { "version" };
const juce::Identifier seedProp    { "dice_seed" };
const juce::Identifier stateVersionProp { "state_version" };
}

SoundSuiteProcessor::SoundSuiteProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SoundSuite", silo::createParameterLayout())
{
    for (std::size_t i = 0; i < silo::numMacros; ++i)
    {
        zones[i]       = std::make_unique<silo::ZoneTableHolder> (loadBuiltInTable (silo::macros[i].id));
        macroValues[i] = apvts.getRawParameterValue (silo::toString (silo::macros[i].id));
    }

    widthManual = apvts.getRawParameterValue (silo::ids::widthManual.getParamID());
    widthXover  = apvts.getRawParameterValue (silo::ids::widthCrossover.getParamID());

    getTuningState (apvts.state);   // create the Tuning/Macro nodes with current versions and seed 0
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
    setLatencySamples (getChainLatencySamples());
}

double SoundSuiteProcessor::getTailLengthSeconds() const
{
    double tail = 0.0;
    for (auto* m : chain) tail += m->getTailSeconds();
    return tail;
}

int SoundSuiteProcessor::getChainLatencySamples() const
{
    int latency = 0;
    for (auto* m : chain) latency += m->getLatencySamples();
    return latency;
}

void SoundSuiteProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    // Host transport -> the one shared timeline. Modules read `transport`;
    // only clock.advance() below moves time. Gate/Chop (build step 2) is the
    // first module to read it.
    {
        double bpm = 120.0;
        std::optional<double> ppq, barStart;
        int num = 4, den = 4;
        bool playing = false;

        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                bpm     = pos->getBpm().orFallback (120.0);
                playing = pos->getIsPlaying();
                if (auto p = pos->getPpqPosition())             ppq = *p;
                if (auto b = pos->getPpqPositionOfLastBarStart()) barStart = *b;
                if (auto ts = pos->getTimeSignature())         { num = ts->numerator; den = ts->denominator; }
            }

        const auto& transport = clock.update (bpm, ppq, barStart, num, den, playing);
        juce::ignoreUnused (transport);
    }

    // Width / Auto-pan: the macro's zone width times the advanced-view width.
    // The macro only ever widens (zero is bypass); narrowing below 100% lives in
    // the advanced view. Auto-pan arrives in build step 3.
    constexpr auto widthPan = silo::macroIndex ("width_pan");
    const float macroWidth  = (float) zones[widthPan]->get().getNumber (widthId, macroValues[widthPan]->load(), 1.0);
    const float manualWidth = widthManual->load() / 100.0f;
    width.setCrossover (widthXover->load());
    width.setWidth (macroWidth * manualWidth);

    if (buffer.getNumChannels() >= 2)
        width.process (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);

    // gate_chop and space are registered (stable IDs for automation and presets)
    // but not wired yet: build steps 2 and 4.

    clock.advance (numSamples);
}

void SoundSuiteProcessor::reloadTuningIfChanged()
{
   #if JUCE_DEBUG && defined (SOUNDSUITE_TUNING_DIR)
    const juce::File dir (SOUNDSUITE_TUNING_DIR);

    for (std::size_t i = 0; i < silo::numMacros; ++i)
    {
        juce::String error;
        const auto file = dir.getChildFile (silo::toString (silo::macros[i].id) + ".json");
        zones[i]->reloadIfChanged (file, &error);
        if (error.isNotEmpty())
            DBG ("Tuning reload failed for " << file.getFileName() << ": " << error);
    }
   #endif
}

// ---------------------------------------------------------------------------
// Saved state
//
// <SoundSuite state_version="2">
//   <PARAM id="..." value="..."/>               knob positions (APVTS)
//   <Tuning>
//     <Macro id="space" version="3" dice_seed="0"/>
//   </Tuning>
// </SoundSuite>
//
// The tuning version says which zone table the knob positions were set
// against. Sessions from before state_version 2 carry none; they were saved
// against version 1 of every table.
// ---------------------------------------------------------------------------

juce::ValueTree SoundSuiteProcessor::getTuningState (juce::ValueTree root) const
{
    auto tuning = root.getOrCreateChildWithName (tuningType, nullptr);

    for (std::size_t i = 0; i < silo::numMacros; ++i)
    {
        const auto id = silo::toString (silo::macros[i].id);
        auto node = tuning.getChildWithProperty (idProp, id);
        if (! node.isValid())
        {
            node = juce::ValueTree (macroType);
            node.setProperty (idProp, id, nullptr);
            node.setProperty (seedProp, (juce::int64) 0, nullptr);
            tuning.appendChild (node, nullptr);
        }
        node.setProperty (versionProp, zones[i]->get().getVersion(), nullptr);
    }
    return tuning;
}

juce::int64 SoundSuiteProcessor::getDiceSeed (std::size_t macro) const
{
    const auto tuning = apvts.state.getChildWithName (tuningType);
    return (juce::int64) tuning.getChildWithProperty (idProp, silo::toString (silo::macros[macro].id))
                               .getProperty (seedProp, (juce::int64) 0);
}

void SoundSuiteProcessor::setDiceSeed (std::size_t macro, juce::int64 seed)
{
    getTuningState (apvts.state).getChildWithProperty (idProp, silo::toString (silo::macros[macro].id))
                    .setProperty (seedProp, seed, nullptr);
}

void SoundSuiteProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    getTuningState (state);   // stamp the versions the knobs are currently set against
    state.setProperty (stateVersionProp, stateVersion, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SoundSuiteProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto loaded = juce::ValueTree::fromXml (*xml);
    const auto savedTuning = loaded.getChildWithName (tuningType);

    apvts.replaceState (loaded);

    // Compare each macro's saved tuning version with the current table.
    lastLoadMismatches.clear();
    for (std::size_t i = 0; i < silo::numMacros; ++i)
    {
        const auto id      = silo::toString (silo::macros[i].id);
        const int  saved   = (int) savedTuning.getChildWithProperty (idProp, id).getProperty (versionProp, 1);
        const int  current = zones[i]->get().getVersion();

        if (saved == current)
            continue;

        lastLoadMismatches.push_back ({ id, saved, current });

        if (auto* param = apvts.getParameter (id))
        {
            const float migrated = silo::migrateMacroValue (silo::macros[i].id, saved, current, param->getValue());
            param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, migrated));
        }
    }

    getTuningState (apvts.state);   // fill in any macro the session didn't know about; keep saved seeds
}

juce::AudioProcessorEditor* SoundSuiteProcessor::createEditor()
{
    return new SoundSuiteEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SoundSuiteProcessor();
}
